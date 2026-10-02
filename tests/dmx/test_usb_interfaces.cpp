// USB DMX interfaces without hardware. On Linux a pseudo-terminal stands in for the
// widget: the interface opens the pty's slave side, the test plays the device on the
// master side. Error reporting for a missing device is tested on every platform.

#include "DmxTestUtil.h"

#include "dmx/interfaces/EnttecProInterface.h"
#include "dmx/interfaces/OpenDmxInterface.h"
#include "dmx/platform/SerialPort.h"
#include "dmx/protocol/EnttecPro.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cstdlib>
#endif

using namespace dmxviz::dmx;
using namespace std::chrono_literals;

TEST_CASE("usb interfaces: missing device is reported by start()") {
#ifdef _WIN32
    const std::string missing = "COM250";
#else
    const std::string missing = "/dev/dmxviz-no-such-device";
#endif
    EnttecProInterface pro;
    pro.setPortPath(missing);
    std::string error;
    CHECK_FALSE(pro.start(error));
    CHECK(error.find(missing) != std::string::npos);
    CHECK(pro.state() == InterfaceState::Error);

    OpenDmxInterface open;
    CHECK_FALSE(open.start(error));  // no port configured
    CHECK(open.state() == InterfaceState::Error);
}

TEST_CASE("usb interfaces: config JSON") {
    EnttecProInterface pro;
    pro.setPortPath("/dev/serial/by-id/usb-ENTTEC_DMX_USB_PRO_EN1-if00-port0");
    pro.setInputUniverse(7);
    EnttecProInterface pro2;
    std::string error;
    REQUIRE_MESSAGE(pro2.loadConfig(pro.saveConfig(), error), error);
    CHECK(pro2.portPath() == pro.portPath());
    CHECK(pro2.inputUniverse() == 7);
    CHECK(pro2.summary().find("ENTTEC") != std::string::npos);
    CHECK_FALSE(pro2.loadConfig({{"inputUniverse", 0}}, error));

    OpenDmxInterface open;
    open.setPortPath("COM4");
    open.setRefreshRate(100);  // clamped
    CHECK(open.refreshRate() == OpenDmxInterface::kMaxRefreshRate);
    OpenDmxInterface open2;
    REQUIRE_MESSAGE(open2.loadConfig(open.saveConfig(), error), error);
    CHECK(open2.portPath() == "COM4");
    CHECK(open2.refreshRate() == OpenDmxInterface::kMaxRefreshRate);
    CHECK_FALSE(open2.caps().input);
    CHECK(open2.caps().output);
}

TEST_CASE("usb interfaces: serial port enumeration does not crash") {
    for (const SerialPortInfo& port : listSerialPorts()) CHECK_FALSE(port.path.empty());
}

#ifndef _WIN32

namespace {

// The "device" end of a pseudo-terminal.
struct FakeDevice {
    int master = -1;
    std::string slavePath;

    FakeDevice() {
        master = posix_openpt(O_RDWR | O_NOCTTY);
        REQUIRE(master >= 0);
        REQUIRE(grantpt(master) == 0);
        REQUIRE(unlockpt(master) == 0);
        slavePath = ptsname(master);
        termios raw{};
        tcgetattr(master, &raw);
        cfmakeraw(&raw);
        tcsetattr(master, TCSANOW, &raw);
    }
    ~FakeDevice() { closeMaster(); }

    void closeMaster() {
        if (master >= 0) ::close(master);
        master = -1;
    }
    void write(const std::vector<std::uint8_t>& bytes) {
        REQUIRE(::write(master, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
    }
    // Reads whatever arrives within `timeout`.
    std::vector<std::uint8_t> read(std::chrono::milliseconds timeout) {
        std::vector<std::uint8_t> out;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            pollfd pfd{master, POLLIN, 0};
            if (::poll(&pfd, 1, 5) <= 0) continue;
            std::uint8_t buffer[1024];
            const ssize_t n = ::read(master, buffer, sizeof buffer);
            if (n <= 0) break;
            out.insert(out.end(), buffer, buffer + n);
        }
        return out;
    }
};

std::vector<std::uint8_t> framed(std::uint8_t label, const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> out(payload.size() + enttec::kFrameOverhead);
    enttec::encodeMessage(label, payload, out);
    return out;
}

}  // namespace

TEST_CASE("enttec pro interface: receive mode, input and output over a pty") {
    FakeDevice device;
    UniverseStore store;
    EnttecProInterface pro;
    pro.setPortPath(device.slavePath);
    pro.setInputUniverse(3);
    pro.setLabel("Pro");
    pro.attach(&store, 4);
    std::string error;
    REQUIRE_MESSAGE(pro.start(error), error);

    // On open the interface asks for every received frame: label 8 with 0.
    const auto hello = device.read(50ms);
    CHECK(hello == std::vector<std::uint8_t>{0x7E, 8, 1, 0, 0, 0xE7});

    // The widget reports a frame, split by noise and arriving in pieces.
    std::vector<std::uint8_t> stream = {0x00, 0x13};
    const auto frame = framed(enttec::label::kReceivedDmx, {0x00, 0x00, 10, 20, 30});
    stream.insert(stream.end(), frame.begin(), frame.end());
    device.write({stream.begin(), stream.begin() + 4});
    std::this_thread::sleep_for(5ms);
    device.write({stream.begin() + 4, stream.end()});
    REQUIRE(dmxtest::waitForChannel(store, 3, 3, 30));

    // Change-of-state update for channel 2.
    device.write(framed(enttec::label::kReceivedDmxChange, {0, 0x04, 0, 0, 0, 0, 99}));
    REQUIRE(dmxtest::waitForChannel(store, 3, 2, 99));
    DmxSnapshot snap;
    store.snapshot(snap);
    CHECK(snap.channel(3, 1) == 10);
    REQUIRE(snap.info(3));
    CHECK(snap.info(3)->sources[0].name == "Pro");
    CHECK(snap.info(3)->sources[0].protocol == Protocol::EnttecPro);

    // Output: a label 6 message with start code 0 and our data.
    UniverseData out{};
    out[0] = 1;
    out[511] = 2;
    pro.send(1, out);
    enttec::WidgetParser parser;
    std::vector<std::uint8_t> sent;
    REQUIRE(dmxtest::waitFor([&] {
        parser.feed(device.read(10ms), [&](const enttec::WidgetMessage& m) {
            if (m.label == enttec::label::kSendDmx) sent.assign(m.payload.begin(), m.payload.end());
        });
        return !sent.empty();
    }));
    REQUIRE(sent.size() == 513);
    CHECK(sent[0] == 0);
    CHECK(sent[1] == 1);
    CHECK(sent[512] == 2);

    // Unplugging: the interface reports the error, keeps retrying and still stops quickly.
    device.closeMaster();
    CHECK(dmxtest::waitFor([&] { return pro.state() == InterfaceState::Error; }));
    const auto before = std::chrono::steady_clock::now();
    pro.stop();
    CHECK(std::chrono::steady_clock::now() - before < 200ms);
    store.snapshot(snap);
    CHECK(snap.universe(3) == nullptr);
}

TEST_CASE("open dmx interface: continuous frames over a pty") {
    FakeDevice device;
    OpenDmxInterface open;
    open.setPortPath(device.slavePath);
    open.setRefreshRate(40);
    UniverseData data{};
    data[0] = 0xAA;
    data[511] = 0x55;
    open.send(1, data);
    std::string error;
    REQUIRE_MESSAGE(open.start(error), error);

    // Several complete frames of start code + 512 slots arrive on their own.
    std::vector<std::uint8_t> bytes;
    REQUIRE(dmxtest::waitFor([&] {
        const auto chunk = device.read(10ms);
        bytes.insert(bytes.end(), chunk.begin(), chunk.end());
        return bytes.size() >= 3 * 513;
    }));
    CHECK(bytes.size() % 513 == 0);
    CHECK(bytes[0] == 0);
    CHECK(bytes[1] == 0xAA);
    CHECK(bytes[512] == 0x55);
    CHECK(bytes[513] == 0);
    CHECK(open.status().packetsOut >= 3);

    const auto before = std::chrono::steady_clock::now();
    open.stop();
    CHECK(std::chrono::steady_clock::now() - before < 200ms);
    CHECK(open.state() == InterfaceState::Stopped);
}

#endif  // !_WIN32
