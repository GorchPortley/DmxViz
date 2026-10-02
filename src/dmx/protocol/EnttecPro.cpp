#include "dmx/protocol/EnttecPro.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace dmxviz::dmx::enttec {

std::size_t encodeMessage(std::uint8_t messageLabel, std::span<const std::uint8_t> payload,
                          std::span<std::uint8_t> out) {
    if (payload.size() > kMaxPayload || out.size() < payload.size() + kFrameOverhead) return 0;
    out[0] = kStartOfMessage;
    out[1] = messageLabel;
    out[2] = static_cast<std::uint8_t>(payload.size() & 0xFF);
    out[3] = static_cast<std::uint8_t>(payload.size() >> 8);
    std::copy(payload.begin(), payload.end(), out.begin() + 4);
    out[4 + payload.size()] = kEndOfMessage;
    return payload.size() + kFrameOverhead;
}

std::size_t encodeSendDmx(std::span<const std::uint8_t> channels, std::span<std::uint8_t> out) {
    if (channels.size() > kUniverseSize) return 0;
    const std::size_t slots = std::max(channels.size(), kMinSendDmxSlots);
    const std::size_t payloadSize = 1 + slots;
    if (out.size() < payloadSize + kFrameOverhead) return 0;
    // Build the payload in place to avoid a temporary buffer.
    out[0] = kStartOfMessage;
    out[1] = label::kSendDmx;
    out[2] = static_cast<std::uint8_t>(payloadSize & 0xFF);
    out[3] = static_cast<std::uint8_t>(payloadSize >> 8);
    out[4] = 0;  // DMX start code
    std::copy(channels.begin(), channels.end(), out.begin() + 5);
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(5 + channels.size()),
              out.begin() + static_cast<std::ptrdiff_t>(5 + slots), std::uint8_t{0});
    out[4 + payloadSize] = kEndOfMessage;
    return payloadSize + kFrameOverhead;
}

std::size_t encodeReceiveDmxOnChange(bool onlyChanges, std::span<std::uint8_t> out) {
    const std::uint8_t mode = static_cast<std::uint8_t>(onlyChanges ? 1 : 0);
    return encodeMessage(label::kReceiveDmxOnChange, std::span<const std::uint8_t>(&mode, 1), out);
}

std::optional<ReceivedDmx> decodeReceivedDmx(const WidgetMessage& message) {
    // Status byte + start code at least; up to 512 channels.
    if (message.label != label::kReceivedDmx || message.payload.size() < 2 ||
        message.payload.size() > 2 + kUniverseSize)
        return std::nullopt;
    ReceivedDmx result;
    result.status = message.payload[0];
    result.startCode = message.payload[1];
    result.channels = message.payload.subspan(2);
    return result;
}

std::optional<ChangeOfState> decodeChangeOfState(const WidgetMessage& message) {
    if (message.label != label::kReceivedDmxChange || message.payload.size() < 6) return std::nullopt;
    ChangeOfState result;
    result.firstByteIndex = static_cast<std::uint16_t>(message.payload[0] * 8);
    std::copy_n(message.payload.begin() + 1, 5, result.changedBits.begin());
    result.changedValues = message.payload.subspan(6);

    int setBits = 0;
    for (const std::uint8_t b : result.changedBits) setBits += std::popcount(b);
    if (result.changedValues.size() != static_cast<std::size_t>(setBits)) return std::nullopt;
    return result;
}

bool applyChangeOfState(const ChangeOfState& change, UniverseData& frame) {
    // Validate first so a bad packet never half-updates the frame.
    std::size_t valueIndex = 0;
    for (std::size_t bit = 0; bit < 40; ++bit) {
        if ((change.changedBits[bit / 8] & (1u << (bit % 8))) == 0) continue;
        if (valueIndex >= change.changedValues.size()) return false;
        const std::size_t byteIndex = change.firstByteIndex + bit;
        if (byteIndex == 0 && change.changedValues[valueIndex] != 0) return false;  // non-DMX start code
        if (byteIndex > kUniverseSize) return false;
        ++valueIndex;
    }

    valueIndex = 0;
    for (std::size_t bit = 0; bit < 40; ++bit) {
        if ((change.changedBits[bit / 8] & (1u << (bit % 8))) == 0) continue;
        const std::size_t byteIndex = change.firstByteIndex + bit;
        const std::uint8_t value = change.changedValues[valueIndex++];
        if (byteIndex >= 1) frame[byteIndex - 1] = value;  // index 0 is the start code
    }
    return true;
}

// ---- WidgetParser -----------------------------------------------------------------------

void WidgetParser::reset() {
    size_ = 0;
    consumed_ = 0;
}

void WidgetParser::push(std::uint8_t byte) {
    if (size_ == 0 && byte != kStartOfMessage) {
        ++discardedBytes_;  // hunting for a start byte
        return;
    }
    // next() runs after every byte, so a full buffer always holds a complete message that
    // was consumed already; this guard only protects against misuse.
    if (size_ == buffer_.size()) resync();
    buffer_[size_++] = byte;
}

std::optional<WidgetMessage> WidgetParser::next() {
    if (consumed_ > 0) {
        dropFront(consumed_);  // the handler is done with the previous message
        consumed_ = 0;
    }
    for (;;) {
        if (size_ == 0) return std::nullopt;
        if (buffer_[0] != kStartOfMessage) {
            resync();
            continue;
        }
        if (size_ < 4) return std::nullopt;  // need label and length
        const std::size_t length = std::size_t{buffer_[2]} | (std::size_t{buffer_[3]} << 8);
        if (length > kMaxPayload) {
            ++framingErrors_;
            resync();
            continue;
        }
        if (size_ < length + kFrameOverhead) return std::nullopt;  // wait for more bytes
        if (buffer_[length + 4] != kEndOfMessage) {
            ++framingErrors_;
            resync();
            continue;
        }
        consumed_ = length + kFrameOverhead;
        return WidgetMessage{buffer_[1], std::span<const std::uint8_t>(buffer_.data() + 4, length)};
    }
}

void WidgetParser::dropFront(std::size_t count) {
    count = std::min(count, size_);
    std::memmove(buffer_.data(), buffer_.data() + count, size_ - count);
    size_ -= count;
}

void WidgetParser::resync() {
    // Skip the current (bogus) start byte and everything up to the next candidate.
    std::size_t nextStart = 1;
    while (nextStart < size_ && buffer_[nextStart] != kStartOfMessage) ++nextStart;
    discardedBytes_ += nextStart;
    dropFront(nextStart);
}

}  // namespace dmxviz::dmx::enttec
