#pragma once
// SerialPort: a thin wrapper around a serial device for USB DMX interfaces, plus port
// enumeration.
//
// Linux uses termios2 + BOTHER so the non-standard DMX rate of 250000 baud works on
// every driver; Windows uses CreateFile + DCB. Reads take a timeout so IO threads can
// stop promptly. All calls report errors via return values and lastError().
// Not thread-safe: one IO thread owns a port.

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dmxviz::dmx {

struct SerialPortInfo {
    std::string path;         // what to open and save: a stable /dev/serial/by-id/... link or "COM3"
    std::string systemName;   // "/dev/ttyUSB0", "COM3"
    std::string description;  // e.g. "DMX USB PRO (ENTTEC)" when the system knows it
};

// Lists serial ports, USB adapters first (Linux: /dev/serial/by-id, /dev/ttyUSB*,
// /dev/ttyACM*; Windows: the "Ports" device class via SetupAPI).
std::vector<SerialPortInfo> listSerialPorts();

// Line settings. Always 8 data bits, no parity, no flow control.
struct SerialSettings {
    int baudRate = 250000;
    bool twoStopBits = true;
};

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    bool open(const std::string& path, const SerialSettings& settings, std::string& error);
    void close();
    bool isOpen() const;

    // Reads whatever is available, waiting up to `timeout` for the first byte.
    // Returns the number of bytes read, 0 on timeout, -1 on error (e.g. unplugged).
    int read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout);
    // Writes all bytes (blocking, with an internal timeout). False on error.
    bool write(std::span<const std::uint8_t> data);
    // Holds the line in the "break" state (DMX frames start with a break).
    bool setBreak(bool on);
    // Blocks until everything written has left the UART.
    bool drain();
    // Drops unread input and unsent output.
    void discardBuffers();

    const std::string& lastError() const { return lastError_; }

private:
    bool fail(const std::string& what);

    std::intptr_t handle_ = -1;  // fd on Linux, HANDLE on Windows (-1 == INVALID_HANDLE_VALUE)
    std::string lastError_;
#ifdef _WIN32
    long long readTimeoutMs_ = -1;  // cached COMMTIMEOUTS setting
#endif
};

}  // namespace dmxviz::dmx
