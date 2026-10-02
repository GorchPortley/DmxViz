#include "dmx/platform/UdpSocket.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#endif

#include <algorithm>
#include <format>
#include <system_error>
#include <utility>

namespace dmxviz::dmx {
namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
int lastSocketError() { return WSAGetLastError(); }
#else
using NativeSocket = int;
int lastSocketError() { return errno; }
#endif

NativeSocket native(std::uintptr_t handle) { return static_cast<NativeSocket>(handle); }

// The OS message for a socket error code (Winsock codes are Win32 error codes too).
std::string errorText(int code) { return std::system_category().message(code); }

sockaddr_in toSockaddr(const Endpoint& endpoint) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(endpoint.port);
    addr.sin_addr.s_addr = htonl(endpoint.address.value);
    return addr;
}

Endpoint fromSockaddr(const sockaddr_in& addr) {
    return Endpoint{Ipv4Address{ntohl(addr.sin_addr.s_addr)}, ntohs(addr.sin_port)};
}

constexpr auto kSockaddrLength = static_cast<socklen_t>(sizeof(sockaddr_in));

}  // namespace

bool initNetworking() {
#ifdef _WIN32
    static const bool ok = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
#else
    return true;
#endif
}

UdpSocket::~UdpSocket() { close(); }

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : handle_(std::exchange(other.handle_, kInvalid)) {}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, kInvalid);
    }
    return *this;
}

bool UdpSocket::open(const Endpoint& local, bool reuseAddress, std::string& error) {
    close();
    if (!initNetworking()) {
        error = "cannot initialise networking (WSAStartup failed)";
        return false;
    }
#ifdef _WIN32
    const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        error = std::format("cannot create UDP socket: {}", errorText(lastSocketError()));
        return false;
    }
#else
    const int s = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    if (s < 0) {
        error = std::format("cannot create UDP socket: {}", errorText(lastSocketError()));
        return false;
    }
#endif
    handle_ = static_cast<std::uintptr_t>(s);

    if (reuseAddress && !setOption(SOL_SOCKET, SO_REUSEADDR, 1, "SO_REUSEADDR", error)) {
        close();
        return false;
    }

    // Non-blocking: receive() waits with poll() instead, so it can time out.
#ifdef _WIN32
    u_long nonBlocking = 1;
    // FIONBIO is an unsigned constant while the parameter is a long: cast explicitly.
    const bool nonBlockingOk = ioctlsocket(s, static_cast<long>(FIONBIO), &nonBlocking) == 0;
#else
    const int flags = fcntl(s, F_GETFL, 0);
    const bool nonBlockingOk = flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
    if (!nonBlockingOk) {
        error = std::format("cannot make socket non-blocking: {}", errorText(lastSocketError()));
        close();
        return false;
    }

    const sockaddr_in addr = toSockaddr(local);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), kSockaddrLength) != 0) {
        error = std::format("cannot bind UDP {}: {}", local.toString(), errorText(lastSocketError()));
        close();
        return false;
    }
    return true;
}

void UdpSocket::close() {
    if (handle_ == kInvalid) return;
#ifdef _WIN32
    ::closesocket(native(handle_));
#else
    ::close(native(handle_));
#endif
    handle_ = kInvalid;
}

bool UdpSocket::isOpen() const { return handle_ != kInvalid; }

Endpoint UdpSocket::localEndpoint() const {
    if (!isOpen()) return {};
    sockaddr_in addr{};
    socklen_t length = kSockaddrLength;
    if (::getsockname(native(handle_), reinterpret_cast<sockaddr*>(&addr), &length) != 0) return {};
    return fromSockaddr(addr);
}

bool UdpSocket::setOption(int level, int name, int value, const char* what, std::string& error) {
    if (!isOpen()) {
        error = "socket is not open";
        return false;
    }
    if (::setsockopt(native(handle_), level, name, reinterpret_cast<const char*>(&value),
                     static_cast<socklen_t>(sizeof value)) != 0) {
        error = std::format("cannot set {}: {}", what, errorText(lastSocketError()));
        return false;
    }
    return true;
}

bool UdpSocket::setBroadcast(bool enable, std::string& error) {
    return setOption(SOL_SOCKET, SO_BROADCAST, enable ? 1 : 0, "SO_BROADCAST", error);
}

bool UdpSocket::membership(int option, Ipv4Address group, Ipv4Address interfaceAddress, std::string& error) {
    if (!isOpen()) {
        error = "socket is not open";
        return false;
    }
    ip_mreq request{};
    request.imr_multiaddr.s_addr = htonl(group.value);
    request.imr_interface.s_addr = htonl(interfaceAddress.value);
    if (::setsockopt(native(handle_), IPPROTO_IP, option, reinterpret_cast<const char*>(&request),
                     static_cast<socklen_t>(sizeof request)) != 0) {
        error = std::format("multicast {} {} on {}: {}", option == IP_ADD_MEMBERSHIP ? "join" : "leave",
                            group.toString(), interfaceAddress.toString(), errorText(lastSocketError()));
        return false;
    }
    return true;
}

bool UdpSocket::joinMulticast(Ipv4Address group, Ipv4Address interfaceAddress, std::string& error) {
    return membership(IP_ADD_MEMBERSHIP, group, interfaceAddress, error);
}

bool UdpSocket::leaveMulticast(Ipv4Address group, Ipv4Address interfaceAddress, std::string& error) {
    return membership(IP_DROP_MEMBERSHIP, group, interfaceAddress, error);
}

bool UdpSocket::setMulticastInterface(Ipv4Address interfaceAddress, std::string& error) {
    if (!isOpen()) {
        error = "socket is not open";
        return false;
    }
    in_addr addr{};
    addr.s_addr = htonl(interfaceAddress.value);
    if (::setsockopt(native(handle_), IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&addr),
                     static_cast<socklen_t>(sizeof addr)) != 0) {
        error = std::format("cannot set multicast interface {}: {}", interfaceAddress.toString(),
                            errorText(lastSocketError()));
        return false;
    }
    return true;
}

bool UdpSocket::setMulticastLoopback(bool enable, std::string& error) {
    return setOption(IPPROTO_IP, IP_MULTICAST_LOOP, enable ? 1 : 0, "IP_MULTICAST_LOOP", error);
}

bool UdpSocket::setMulticastTtl(int ttl, std::string& error) {
    return setOption(IPPROTO_IP, IP_MULTICAST_TTL, std::clamp(ttl, 1, 255), "IP_MULTICAST_TTL", error);
}

bool UdpSocket::sendTo(std::span<const std::uint8_t> data, const Endpoint& destination, std::string* error) {
    if (!isOpen()) {
        if (error) *error = "socket is not open";
        return false;
    }
    const sockaddr_in addr = toSockaddr(destination);
#ifdef _WIN32
    const int sent = ::sendto(native(handle_), reinterpret_cast<const char*>(data.data()),
                              static_cast<int>(data.size()), 0, reinterpret_cast<const sockaddr*>(&addr),
                              kSockaddrLength);
#else
    const ssize_t sent = ::sendto(native(handle_), data.data(), data.size(), 0,
                                  reinterpret_cast<const sockaddr*>(&addr), kSockaddrLength);
#endif
    if (sent < 0 || static_cast<std::size_t>(sent) != data.size()) {
        if (error) *error = std::format("send to {} failed: {}", destination.toString(), errorText(lastSocketError()));
        return false;
    }
    return true;
}

int UdpSocket::receive(std::span<std::uint8_t> buffer, Endpoint& from, std::chrono::milliseconds timeout) {
    if (!isOpen()) return -1;
    const int timeoutMs = static_cast<int>(std::clamp<std::chrono::milliseconds::rep>(timeout.count(), 0, 60000));

#ifdef _WIN32
    WSAPOLLFD pfd{};
    pfd.fd = native(handle_);
    pfd.events = POLLRDNORM;
    const int ready = WSAPoll(&pfd, 1, timeoutMs);
    if (ready == SOCKET_ERROR) return WSAGetLastError() == WSAEINTR ? 0 : -1;
#else
    pollfd pfd{};
    pfd.fd = native(handle_);
    pfd.events = POLLIN;
    const int ready = ::poll(&pfd, 1, timeoutMs);
    if (ready < 0) return errno == EINTR ? 0 : -1;
#endif
    if (ready == 0) return 0;

    sockaddr_in addr{};
    socklen_t length = kSockaddrLength;
#ifdef _WIN32
    const int n = ::recvfrom(native(handle_), reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()),
                             0, reinterpret_cast<sockaddr*>(&addr), &length);
    if (n == SOCKET_ERROR) {
        const int code = WSAGetLastError();
        // WSAECONNRESET: an earlier send hit a closed port (ICMP); WSAEMSGSIZE: oversized
        // datagram, truncated. Neither is a problem for the socket itself.
        if (code == WSAEWOULDBLOCK || code == WSAECONNRESET || code == WSAEMSGSIZE || code == WSAEINTR) return 0;
        return -1;
    }
#else
    const ssize_t n = ::recvfrom(native(handle_), buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&addr),
                                 &length);
    if (n < 0) {
        const int code = errno;
        if (code == EAGAIN || code == EWOULDBLOCK || code == EINTR || code == ECONNREFUSED) return 0;
        return -1;
    }
#endif
    from = fromSockaddr(addr);
    return static_cast<int>(n);
}

}  // namespace dmxviz::dmx
