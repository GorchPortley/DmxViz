#include "dmx/platform/NetworkInterfaces.h"

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
#include <iphlpapi.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <sys/socket.h>

#include <map>
#endif

#include <algorithm>
#include <cstring>
#include <format>

namespace dmxviz::dmx {
namespace {

void finish(NetworkInterfaceInfo& info) {
    info.broadcast = Ipv4Address{info.address.value | ~info.netmask.value};
    info.loopback = info.loopback || info.address.isLoopback();
}

#ifdef _WIN32
Ipv4Address maskFromPrefix(unsigned prefixLength) {
    if (prefixLength == 0) return Ipv4Address{0};
    if (prefixLength >= 32) return Ipv4Address{0xFFFFFFFFu};
    return Ipv4Address{0xFFFFFFFFu << (32 - prefixLength)};
}

std::string toUtf8(const wchar_t* text) {
    if (!text || !*text) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}
#endif

}  // namespace

std::string NetworkInterfaceInfo::label() const {
    return std::format("{} ({})", name, address.toString());
}

#ifdef _WIN32

std::vector<NetworkInterfaceInfo> listNetworkInterfaces() {
    std::vector<NetworkInterfaceInfo> result;
    initNetworking();

    // The required buffer size can change between calls, so retry a few times.
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer;
    ULONG status = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 4 && status == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buffer.resize(size);
        status =
            GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                 nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (status != NO_ERROR) return result;

    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter; adapter = adapter->Next) {
        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            const sockaddr* sa = unicast->Address.lpSockaddr;
            if (!sa || sa->sa_family != AF_INET) continue;
            NetworkInterfaceInfo info;
            info.name = toUtf8(adapter->FriendlyName);
            info.address = Ipv4Address{ntohl(reinterpret_cast<const sockaddr_in*>(sa)->sin_addr.s_addr)};
            info.netmask = maskFromPrefix(unicast->OnLinkPrefixLength);
            info.loopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
            info.up = adapter->OperStatus == IfOperStatusUp;
            if (adapter->PhysicalAddressLength == 6) std::memcpy(info.mac.data(), adapter->PhysicalAddress, 6);
            finish(info);
            result.push_back(std::move(info));
        }
    }
    return result;
}

#else

std::vector<NetworkInterfaceInfo> listNetworkInterfaces() {
    std::vector<NetworkInterfaceInfo> result;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return result;

    // MAC addresses come as separate AF_PACKET entries with the same interface name.
    std::map<std::string, std::array<std::uint8_t, 6> > macs;
    for (ifaddrs* it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_PACKET) continue;
        const auto* ll = reinterpret_cast<const sockaddr_ll*>(it->ifa_addr);
        if (ll->sll_halen != 6) continue;
        std::array<std::uint8_t, 6> mac{};
        std::memcpy(mac.data(), ll->sll_addr, 6);
        macs[it->ifa_name] = mac;
    }

    for (ifaddrs* it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        NetworkInterfaceInfo info;
        info.name = it->ifa_name;
        info.address = Ipv4Address{ntohl(reinterpret_cast<const sockaddr_in*>(it->ifa_addr)->sin_addr.s_addr)};
        if (it->ifa_netmask)
            info.netmask = Ipv4Address{ntohl(reinterpret_cast<const sockaddr_in*>(it->ifa_netmask)->sin_addr.s_addr)};
        info.loopback = (it->ifa_flags & IFF_LOOPBACK) != 0;
        info.up = (it->ifa_flags & IFF_UP) != 0;
        if (const auto mac = macs.find(info.name); mac != macs.end()) info.mac = mac->second;
        finish(info);
        result.push_back(std::move(info));
    }
    freeifaddrs(list);
    return result;
}

#endif

std::optional<NetworkInterfaceInfo> findNetworkInterface(Ipv4Address address) {
    for (NetworkInterfaceInfo& info : listNetworkInterfaces())
        if (info.address == address) return std::move(info);
    return std::nullopt;
}

}  // namespace dmxviz::dmx
