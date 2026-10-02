#pragma once
// UdpSocket: a thin, move-only wrapper around an IPv4 UDP socket (BSD sockets on
// Linux, Winsock on Windows).
//
// It offers only what the DMX protocols need: bind, broadcast, multicast membership,
// sendTo, and a receive with a timeout so IO threads can check their stop flag
// regularly. The socket is non-blocking. Errors are reported as bool + message.

#include "dmx/NetAddress.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>

namespace dmxviz::dmx {

// Winsock needs WSAStartup() once per process; elsewhere this does nothing. Called
// automatically by UdpSocket::open() and listNetworkInterfaces().
bool initNetworking();

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Creates the socket and binds it to `local` (port 0 picks a free port).
    // `reuseAddress` lets several programs share a port, which is common for Art-Net
    // and sACN receivers on the same machine.
    bool open(const Endpoint& local, bool reuseAddress, std::string& error);
    void close();
    bool isOpen() const;
    // The address and port actually bound (useful after binding port 0).
    Endpoint localEndpoint() const;

    bool setBroadcast(bool enable, std::string& error);
    // Multicast membership on the NIC with address `interfaceAddress` (0.0.0.0 = let the OS choose).
    bool joinMulticast(Ipv4Address group, Ipv4Address interfaceAddress, std::string& error);
    bool leaveMulticast(Ipv4Address group, Ipv4Address interfaceAddress, std::string& error);
    // NIC used for outgoing multicast.
    bool setMulticastInterface(Ipv4Address interfaceAddress, std::string& error);
    bool setMulticastLoopback(bool enable, std::string& error);
    bool setMulticastTtl(int ttl, std::string& error);

    // Sends one datagram. Returns false on error (the message is in `error` if given).
    bool sendTo(std::span<const std::uint8_t> data, const Endpoint& destination, std::string* error = nullptr);

    // Waits up to `timeout` for one datagram. Returns its size (> 0), 0 on timeout or a
    // harmless condition (e.g. an ICMP "port unreachable" echo on Windows), -1 on error.
    int receive(std::span<std::uint8_t> buffer, Endpoint& from, std::chrono::milliseconds timeout);

private:
    static constexpr std::uintptr_t kInvalid = ~std::uintptr_t{0};
    bool setOption(int level, int name, int value, const char* what, std::string& error);
    bool membership(int option, Ipv4Address group, Ipv4Address interfaceAddress, std::string& error);

    std::uintptr_t handle_ = kInvalid;  // int fd on Linux, SOCKET on Windows
};

}  // namespace dmxviz::dmx
