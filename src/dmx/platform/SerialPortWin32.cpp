// Windows implementation of SerialPort and listSerialPorts().
#include "dmx/platform/SerialPort.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <setupapi.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <system_error>

namespace dmxviz::dmx {
namespace {

// {4D36E978-E325-11CE-BFC1-08002BE10318}: the "Ports (COM & LPT)" device setup class.
// Defined here so we do not depend on <devguid.h>/<initguid.h> include-order tricks.
const GUID kPortsClassGuid = {0x4d36e978, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

HANDLE toHandle(std::intptr_t handle) {
    return reinterpret_cast<HANDLE>(handle);
}

std::string lastErrorText() {
    return std::system_category().message(static_cast<int>(GetLastError()));
}

std::string toUtf8(const wchar_t* text) {
    if (!text || !*text) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

int comNumber(const std::string& name) {
    return std::atoi(name.c_str() + 3);
}  // "COM12" -> 12

}  // namespace

std::vector<SerialPortInfo> listSerialPorts() {
    std::vector<SerialPortInfo> ports;
    HDEVINFO devices = SetupDiGetClassDevsW(&kPortsClassGuid, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return ports;

    SP_DEVINFO_DATA device{};
    device.cbSize = static_cast<DWORD>(sizeof(device));
    for (DWORD index = 0; SetupDiEnumDeviceInfo(devices, index, &device); ++index) {
        HKEY key = SetupDiOpenDevRegKey(devices, &device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE) continue;
        wchar_t portName[64] = {};
        DWORD size = static_cast<DWORD>(sizeof(portName) - sizeof(wchar_t));  // keep a terminating NUL
        DWORD type = 0;
        const LONG rc = RegQueryValueExW(key, L"PortName", nullptr, &type, reinterpret_cast<BYTE*>(portName), &size);
        RegCloseKey(key);
        if (rc != ERROR_SUCCESS || type != REG_SZ) continue;

        SerialPortInfo info;
        info.path = toUtf8(portName);
        if (!info.path.starts_with("COM")) continue;  // skip printer (LPT) ports
        info.systemName = info.path;
        wchar_t friendly[256] = {};
        if (SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_FRIENDLYNAME, nullptr,
                                              reinterpret_cast<BYTE*>(friendly),
                                              static_cast<DWORD>(sizeof(friendly) - sizeof(wchar_t)), nullptr))
            info.description = toUtf8(friendly);
        ports.push_back(std::move(info));
    }
    SetupDiDestroyDeviceInfoList(devices);

    std::sort(ports.begin(), ports.end(),
              [](const SerialPortInfo& a, const SerialPortInfo& b) { return comNumber(a.path) < comNumber(b.path); });
    return ports;
}

SerialPort::~SerialPort() {
    close();
}

bool SerialPort::fail(const std::string& what) {
    lastError_ = std::format("{}: {}", what, lastErrorText());
    return false;
}

bool SerialPort::open(const std::string& path, const SerialSettings& settings, std::string& error) {
    close();
    // The \\.\ prefix is required for COM10 and above and harmless below.
    const std::string devicePath = path.starts_with("\\\\.\\") ? path : "\\\\.\\" + path;
    HANDLE h = CreateFileA(devicePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = std::format("cannot open {}: {}", path, lastErrorText());
        return false;
    }
    handle_ = reinterpret_cast<std::intptr_t>(h);
    readTimeoutMs_ = -1;
    SetupComm(h, 4096, 4096);

    DCB dcb{};
    dcb.DCBlength = static_cast<DWORD>(sizeof(dcb));
    if (!GetCommState(h, &dcb)) {
        error = std::format("{} is not a serial port: {}", path, lastErrorText());
        close();
        return false;
    }
    dcb.BaudRate = static_cast<DWORD>(settings.baudRate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = static_cast<BYTE>(settings.twoStopBits ? TWOSTOPBITS : ONESTOPBIT);
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;  // RTS low: FTDI Open DMX adapters need it
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(h, &dcb)) {
        error = std::format("cannot set {} baud on {}: {}", settings.baudRate, path, lastErrorText());
        close();
        return false;
    }
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    lastError_.clear();
    return true;
}

void SerialPort::close() {
    if (handle_ == -1) return;
    CloseHandle(toHandle(handle_));
    handle_ = -1;
}

bool SerialPort::isOpen() const {
    return handle_ != -1;
}

int SerialPort::read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) {
    if (!isOpen()) return -1;
    HANDLE h = toHandle(handle_);
    const long long timeoutMs = std::clamp<long long>(timeout.count(), 1, 60000);
    if (timeoutMs != readTimeoutMs_) {
        // MAXDWORD/MAXDWORD/constant: return at once if bytes are waiting, otherwise wait
        // up to `constant` ms for the first byte.
        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(timeoutMs);
        timeouts.WriteTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 1000;
        if (!SetCommTimeouts(h, &timeouts)) {
            fail("set timeouts");
            return -1;
        }
        readTimeoutMs_ = timeoutMs;
    }
    DWORD received = 0;
    if (!ReadFile(h, buffer.data(), static_cast<DWORD>(buffer.size()), &received, nullptr)) {
        fail("read");
        return -1;
    }
    return static_cast<int>(received);
}

bool SerialPort::write(std::span<const std::uint8_t> data) {
    if (!isOpen()) return false;
    DWORD written = 0;
    if (!WriteFile(toHandle(handle_), data.data(), static_cast<DWORD>(data.size()), &written, nullptr))
        return fail("write");
    if (written != data.size()) {
        lastError_ = "write timed out";
        return false;
    }
    return true;
}

bool SerialPort::setBreak(bool on) {
    if (!isOpen()) return false;
    const BOOL ok = on ? SetCommBreak(toHandle(handle_)) : ClearCommBreak(toHandle(handle_));
    return ok || fail("break");
}

bool SerialPort::drain() {
    if (!isOpen()) return false;
    return FlushFileBuffers(toHandle(handle_)) || fail("drain");
}

void SerialPort::discardBuffers() {
    if (isOpen()) PurgeComm(toHandle(handle_), PURGE_RXCLEAR | PURGE_TXCLEAR);
}

}  // namespace dmxviz::dmx

#endif  // _WIN32
