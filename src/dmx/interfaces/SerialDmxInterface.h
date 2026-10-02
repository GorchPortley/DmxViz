#pragma once
// SerialDmxInterface: shared plumbing for USB-serial DMX interfaces (Enttec Pro, Open DMX).
//
// It opens the configured serial port in start() (so a missing device is reported to the
// caller right away), runs the IO thread that calls the subclass's ioStep() in a loop,
// and when the device disappears it closes the port and tries to reopen it once per
// second until stop(). Subclasses only implement the protocol.
//
// Subclasses must call stop() in their own destructor: the IO thread calls their virtual
// functions, so it has to end before their members are destroyed.

#include "dmx/DmxInterface.h"
#include "dmx/platform/SerialPort.h"

#include <atomic>
#include <string>
#include <thread>

namespace dmxviz::dmx {

class SerialDmxInterface : public DmxInterface {
public:
    bool start(std::string& error) override;
    void stop() override;

    // Device to open: a /dev/serial/by-id/... path, /dev/ttyUSB0 or COM3 (see listSerialPorts()).
    const std::string& portPath() const { return portPath_; }
    void setPortPath(std::string path) { portPath_ = std::move(path); }  // applies at the next start()
    std::string summary() const override;

protected:
    virtual SerialSettings serialSettings() const = 0;
    // Runs right after the port opened (also after a reconnect), before ioStep().
    virtual bool onPortOpened(SerialPort& /*port*/) { return true; }
    // One unit of IO work (it must not block for more than ~50 ms). Return false on an
    // IO error: the port is closed and reopened later.
    virtual bool ioStep(SerialPort& port) = 0;

    bool stopRequested() const { return stopRequested_.load(); }
    // The path in use since start() (safe to read from the IO thread).
    const std::string& activePortPath() const { return activePortPath_; }

private:
    bool openPort(std::string& error);
    void run();

    std::string portPath_;
    std::string activePortPath_;
    SerialPort port_;  // owned by the IO thread once started
    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
};

}  // namespace dmxviz::dmx
