#pragma once
// Helpers shared by the interfaces' saveConfig()/loadConfig(): addresses, endpoint lists
// and universe lists as JSON strings/arrays. Internal to the dmx module.
//
// The read helpers return false and set `error` for invalid values. Type mismatches in
// nlohmann::json throw; every loadConfig() catches those and turns them into an error.

#include "dmx/NetAddress.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace dmxviz::dmx::config {

// Settings come from project files, so lists and numbers are bounded here instead of trusted.
inline constexpr std::size_t kMaxListEntries = 4096;
inline constexpr int kMaxUniverseOffset = 0xFFFF;  // larger offsets cannot map any universe anyway

// Integer within [minValue, maxValue]. A JSON number outside the range (or a float such as
// 1e300, whose conversion to int would be undefined behaviour) is rejected, not truncated.
inline bool readInt(const nlohmann::json& json, const char* key, long long minValue, long long maxValue, int& out,
                    std::string& error) {
    if (!json.contains(key)) return true;  // keep the default
    const nlohmann::json& value = json.at(key);
    if (!value.is_number()) {
        error = std::format("'{}' must be a number", key);
        return false;
    }
    const double number = value.get<double>();
    if (!(number >= static_cast<double>(minValue) && number <= static_cast<double>(maxValue))) {  // also rejects NaN
        error = std::format("'{}' must be between {} and {}", key, minValue, maxValue);
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

inline bool readAddress(const nlohmann::json& json, const char* key, Ipv4Address& out, std::string& error) {
    if (!json.contains(key)) return true;  // keep the default
    const auto parsed = Ipv4Address::parse(json.at(key).get<std::string>());
    if (!parsed) {
        error = std::format("'{}' is not an IPv4 address", key);
        return false;
    }
    out = *parsed;
    return true;
}

inline nlohmann::json endpointsToJson(const std::vector<Endpoint>& endpoints) {
    nlohmann::json list = nlohmann::json::array();
    for (const Endpoint& e : endpoints) list.push_back(e.toString());
    return list;
}

// Entries are "a.b.c.d" (port = defaultPort) or "a.b.c.d:port".
inline bool readEndpoints(const nlohmann::json& json, const char* key, std::uint16_t defaultPort,
                          std::vector<Endpoint>& out, std::string& error) {
    if (!json.contains(key)) return true;
    if (json.at(key).size() > kMaxListEntries) {
        error = std::format("'{}' has more than {} entries", key, kMaxListEntries);
        return false;
    }
    std::vector<Endpoint> result;
    for (const auto& item : json.at(key)) {
        const auto endpoint = Endpoint::parse(item.get<std::string>(), defaultPort);
        if (!endpoint) {
            error = std::format("'{}' contains an invalid address", key);
            return false;
        }
        result.push_back(*endpoint);
    }
    out = std::move(result);
    return true;
}

// Universe numbers within [minValue, maxValue].
inline bool readUniverses(const nlohmann::json& json, const char* key, int minValue, int maxValue,
                          std::vector<std::uint16_t>& out, std::string& error) {
    if (!json.contains(key)) return true;
    if (json.at(key).size() > kMaxListEntries) {
        error = std::format("'{}' has more than {} entries", key, kMaxListEntries);
        return false;
    }
    std::vector<std::uint16_t> result;
    for (const auto& item : json.at(key)) {
        const int value = item.get<int>();
        if (value < minValue || value > maxValue) {
            error = std::format("'{}' contains universe {} (allowed {}..{})", key, value, minValue, maxValue);
            return false;
        }
        result.push_back(static_cast<std::uint16_t>(value));
    }
    out = std::move(result);
    return true;
}

}  // namespace dmxviz::dmx::config
