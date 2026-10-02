// Linux implementation of SerialPort and listSerialPorts().
#include "dmx/platform/SerialPort.h"

#ifndef _WIN32

// struct termios2, TCSETS2 and BOTHER (arbitrary baud rates such as DMX's 250000) come
// from the kernel headers. <asm/termbits.h> clashes with glibc's <termios.h>, so this
// file talks to the tty with ioctl() only and never includes <termios.h>.
#include <asm/termbits.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <system_error>

namespace dmxviz::dmx {
namespace {

namespace fs = std::filesystem;

std::string errnoText(int code) { return std::system_category().message(code); }

std::string readFirstLine(const fs::path& path) {
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return line;
}

// "DMX USB PRO (ENTTEC)" from sysfs for a tty name like "ttyUSB0" or "ttyACM0".
std::string sysfsDescription(const std::string& ttyName) {
    std::error_code ec;
    const fs::path device = fs::path("/sys/class/tty") / ttyName / "device";
    // ttyACM: "device" is the USB interface, its parent the USB device.
    // ttyUSB: "device" is the usb-serial port, its grandparent the USB device.
    for (const char* up : {"..", "../.."}) {
        const fs::path usb = fs::canonical(device / up, ec);
        if (ec) continue;
        const std::string product = readFirstLine(usb / "product");
        if (product.empty()) continue;
        const std::string manufacturer = readFirstLine(usb / "manufacturer");
        return manufacturer.empty() ? product : std::format("{} ({})", product, manufacturer);
    }
    return {};
}

// Directory listing that never throws (missing directories just give no entries).
std::vector<fs::path> listDirectory(const fs::path& dir) {
    std::vector<fs::path> entries;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) entries.push_back(it->path());
    std::sort(entries.begin(), entries.end());
    return entries;
}

}  // namespace

std::vector<SerialPortInfo> listSerialPorts() {
    std::vector<SerialPortInfo> ports;
    std::set<std::string> listed;  // device nodes already covered by a by-id link

    // Stable names first, e.g. usb-ENTTEC_DMX_USB_PRO_EN123456-if00-port0 -> ../../ttyUSB0.
    // Saving these in the configuration survives re-plugging into another USB port.
    for (const fs::path& link : listDirectory("/dev/serial/by-id")) {
        std::error_code ec;
        const fs::path target = fs::canonical(link, ec);
        if (ec) continue;
        SerialPortInfo info;
        info.path = link.string();
        info.systemName = target.string();
        info.description = sysfsDescription(target.filename().string());
        if (info.description.empty()) info.description = link.filename().string();
        listed.insert(info.systemName);
        ports.push_back(std::move(info));
    }

    for (const fs::path& node : listDirectory("/dev")) {
        const std::string name = node.filename().string();
        if (!name.starts_with("ttyUSB") && !name.starts_with("ttyACM")) continue;
        if (listed.contains(node.string())) continue;
        SerialPortInfo info;
        info.path = node.string();
        info.systemName = node.string();
        info.description = sysfsDescription(name);
        ports.push_back(std::move(info));
    }
    return ports;
}

SerialPort::~SerialPort() { close(); }

bool SerialPort::fail(const std::string& what) {
    lastError_ = std::format("{}: {}", what, errnoText(errno));
    return false;
}

bool SerialPort::open(const std::string& path, const SerialSettings& settings, std::string& error) {
    close();
    const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        error = std::format("cannot open {}: {}", path, errnoText(errno));
        return false;
    }
    handle_ = fd;
    ::ioctl(fd, TIOCEXCL);  // keep other programs off the port while we use it

    termios2 tio{};
    if (::ioctl(fd, TCGETS2, &tio) != 0) {
        error = std::format("{} is not a serial port: {}", path, errnoText(errno));
        close();
        return false;
    }
    // Raw 8 bit, no parity, no flow control, custom baud rate (BOTHER) in both directions.
    tio.c_iflag = IGNBRK;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cflag = CS8 | CREAD | CLOCAL | BOTHER | (BOTHER << IBSHIFT);
    if (settings.twoStopBits) tio.c_cflag |= CSTOPB;
    tio.c_ispeed = static_cast<speed_t>(settings.baudRate);
    tio.c_ospeed = static_cast<speed_t>(settings.baudRate);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (::ioctl(fd, TCSETS2, &tio) != 0) {
        error = std::format("cannot set {} baud on {}: {}", settings.baudRate, path, errnoText(errno));
        close();
        return false;
    }

    // RTS low: FTDI-based Open DMX adapters need it to enable their line driver.
    int rts = TIOCM_RTS;
    ::ioctl(fd, TIOCMBIC, &rts);
    ::ioctl(fd, TCFLSH, TCIOFLUSH);
    lastError_.clear();
    return true;
}

void SerialPort::close() {
    if (handle_ < 0) return;
    ::close(static_cast<int>(handle_));
    handle_ = -1;
}

bool SerialPort::isOpen() const { return handle_ >= 0; }

int SerialPort::read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) {
    if (!isOpen()) return -1;
    pollfd pfd{};
    pfd.fd = static_cast<int>(handle_);
    pfd.events = POLLIN;
    const int timeoutMs = static_cast<int>(std::clamp<std::chrono::milliseconds::rep>(timeout.count(), 0, 60000));
    const int ready = ::poll(&pfd, 1, timeoutMs);
    if (ready < 0) {
        if (errno == EINTR) return 0;
        fail("poll");
        return -1;
    }
    if (ready == 0) return 0;
    if ((pfd.revents & POLLIN) == 0) {
        lastError_ = "device disconnected";
        return -1;
    }
    const ssize_t n = ::read(pfd.fd, buffer.data(), buffer.size());
    if (n < 0) {
        if (errno == EAGAIN || errno == EINTR) return 0;
        fail("read");
        return -1;
    }
    if (n == 0) {  // readable but no data: the device went away
        lastError_ = "device disconnected";
        return -1;
    }
    return static_cast<int>(n);
}

bool SerialPort::write(std::span<const std::uint8_t> data) {
    if (!isOpen()) return false;
    const int fd = static_cast<int>(handle_);
    std::size_t done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
        if (n > 0) {
            done += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno != EAGAIN && errno != EINTR) return fail("write");
        // Output buffer full: wait until the UART has room again.
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        if (::poll(&pfd, 1, 1000) <= 0) {
            lastError_ = "write timed out";
            return false;
        }
    }
    return true;
}

bool SerialPort::setBreak(bool on) {
    if (!isOpen()) return false;
    return ::ioctl(static_cast<int>(handle_), on ? TIOCSBRK : TIOCCBRK) == 0 || fail("break");
}

bool SerialPort::drain() {
    if (!isOpen()) return false;
    return ::ioctl(static_cast<int>(handle_), TCSBRK, 1) == 0 || fail("drain");  // TCSBRK with 1 == tcdrain()
}

void SerialPort::discardBuffers() {
    if (isOpen()) ::ioctl(static_cast<int>(handle_), TCFLSH, TCIOFLUSH);
}

}  // namespace dmxviz::dmx

#endif  // !_WIN32
