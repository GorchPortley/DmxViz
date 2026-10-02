#pragma once
// Enttec DMX USB Pro widget protocol (also spoken by DMXking ultraDMX and other
// "Pro-compatible" interfaces): encoding, decoding and an incremental stream parser.
//
// Every message on the serial link is framed as
//     0x7E  label  length-LSB  length-MSB  payload[length]  0xE7
// Labels we use: 6 = send DMX, 8 = choose receive mode, 5 = received DMX packet,
// 9 = received DMX change-of-state.
// Reference: Enttec "DMX USB Pro Widget API Specification" 1.44.

#include "dmx/DmxTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace dmxviz::dmx::enttec {

constexpr std::uint8_t kStartOfMessage = 0x7E;
constexpr std::uint8_t kEndOfMessage = 0xE7;
constexpr std::size_t kMaxPayload = 600;
constexpr std::size_t kFrameOverhead = 5;  // start, label, two length bytes, end
constexpr std::size_t kMaxMessageSize = kMaxPayload + kFrameOverhead;
constexpr std::size_t kMinSendDmxSlots = 24;  // the widget wants at least 24 channels per frame

namespace label {
constexpr std::uint8_t kGetWidgetParameters = 3;
constexpr std::uint8_t kReceivedDmx = 5;
constexpr std::uint8_t kSendDmx = 6;
constexpr std::uint8_t kReceiveDmxOnChange = 8;
constexpr std::uint8_t kReceivedDmxChange = 9;
constexpr std::uint8_t kGetSerialNumber = 10;
}  // namespace label

// Frames `payload` with `messageLabel`. Returns bytes written, 0 if it does not fit.
std::size_t encodeMessage(std::uint8_t messageLabel, std::span<const std::uint8_t> payload,
                          std::span<std::uint8_t> out);

// Label 6: start code 0 followed by the channels (padded with zeros to at least 24).
std::size_t encodeSendDmx(std::span<const std::uint8_t> channels, std::span<std::uint8_t> out);

// Label 8: false = send every received DMX frame (label 5); true = only send changes (label 9).
std::size_t encodeReceiveDmxOnChange(bool onlyChanges, std::span<std::uint8_t> out);

// One complete message as delivered by the parser. `payload` points into the parser's
// buffer and is valid only until the parser is fed again.
struct WidgetMessage {
    std::uint8_t label = 0;
    std::span<const std::uint8_t> payload;
};

// Label 5 payload: a status byte, then the received frame beginning with the start code.
struct ReceivedDmx {
    std::uint8_t status = 0;  // bit 0: receive queue overflow, bit 1: receive overrun
    std::uint8_t startCode = 0;
    std::span<const std::uint8_t> channels;

    bool valid() const { return status == 0; }
};
std::optional<ReceivedDmx> decodeReceivedDmx(const WidgetMessage& message);

// Label 9 payload: which of 40 consecutive bytes of the frame changed, and their new values.
// Byte index 0 of the frame is the start code, so index k is DMX channel k.
struct ChangeOfState {
    std::uint16_t firstByteIndex = 0;  // payload[0] * 8
    std::array<std::uint8_t, 5> changedBits{};
    std::span<const std::uint8_t> changedValues;  // one value per set bit, in bit order
};
std::optional<ChangeOfState> decodeChangeOfState(const WidgetMessage& message);

// Writes the changed values into `frame`. Returns false (and leaves `frame` untouched)
// if the packet is inconsistent or reports a non-zero start code (not dimmer data).
bool applyChangeOfState(const ChangeOfState& change, UniverseData& frame);

// Incremental parser for the byte stream coming from a widget.
//
// Serial reads return arbitrary chunks: a message may be split over several reads, and
// after opening a port (or a glitch) the stream may start mid-message. The parser
// buffers bytes until a complete, well-framed message is present. When a candidate
// message turns out to be bad (length too large, missing end byte) it throws away only
// the bogus start byte and rescans the buffered bytes, so a real message hidden behind
// garbage is still found.
class WidgetParser {
public:
    // Feeds bytes and calls handler(const WidgetMessage&) for each complete message.
    template <typename Handler>
    void feed(std::span<const std::uint8_t> data, Handler&& handler) {
        for (const std::uint8_t byte : data) {
            push(byte);
            while (const std::optional<WidgetMessage> message = next()) handler(*message);
        }
    }

    void reset();

    // Diagnostics: bytes skipped while searching for a start byte, and broken messages.
    std::size_t discardedBytes() const { return discardedBytes_; }
    std::size_t framingErrors() const { return framingErrors_; }

private:
    void push(std::uint8_t byte);
    std::optional<WidgetMessage> next();
    void dropFront(std::size_t count);
    void resync();  // drop the current start byte and look for the next one

    std::array<std::uint8_t, kMaxMessageSize> buffer_{};
    std::size_t size_ = 0;
    std::size_t consumed_ = 0;  // length of the message returned by the last next()
    std::size_t discardedBytes_ = 0;
    std::size_t framingErrors_ = 0;
};

}  // namespace dmxviz::dmx::enttec
