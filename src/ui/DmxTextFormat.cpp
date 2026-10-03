#include "ui/DmxTextFormat.h"

#include <algorithm>
#include <charconv>
#include <format>

namespace dmxviz::ui {

namespace {

bool isSeparator(char c) {
    return c == ',' || c == ' ' || c == ';' || c == '\t';
}

// Splits on commas and white space; empty pieces are dropped.
std::vector<std::string_view> splitTokens(std::string_view text) {
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isSeparator(text[i])) ++i;
        const std::size_t start = i;
        while (i < text.size() && !isSeparator(text[i])) ++i;
        if (i > start) tokens.push_back(text.substr(start, i - start));
    }
    return tokens;
}

bool parseInt(std::string_view text, int& out) {
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, out);
    return result.ec == std::errc() && result.ptr == end;
}

}  // namespace

std::string formatUniverseList(std::span<const std::uint16_t> universes) {
    std::vector<std::uint16_t> sorted(universes.begin(), universes.end());
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    std::string text;
    for (std::size_t i = 0; i < sorted.size();) {
        std::size_t last = i;
        while (last + 1 < sorted.size() && sorted[last + 1] == sorted[last] + 1) ++last;
        if (!text.empty()) text += ", ";
        if (last - i >= 2)
            text += std::format("{}-{}", sorted[i], sorted[last]);
        else if (last - i == 1)
            text += std::format("{}, {}", sorted[i], sorted[last]);
        else
            text += std::format("{}", sorted[i]);
        i = last + 1;
    }
    return text;
}

bool parseUniverseList(std::string_view text, int minValue, int maxValue, std::vector<std::uint16_t>& out) {
    std::vector<std::uint16_t> result;
    for (const std::string_view token : splitTokens(text)) {
        int first = 0;
        int last = 0;
        const std::size_t dash = token.find('-');
        if (dash == std::string_view::npos) {
            if (!parseInt(token, first)) return false;
            last = first;
        } else if (!parseInt(token.substr(0, dash), first) || !parseInt(token.substr(dash + 1), last)) {
            return false;
        }
        if (first > last || first < minValue || last > maxValue) return false;
        for (int u = first; u <= last; ++u) result.push_back(static_cast<std::uint16_t>(u));
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    out = std::move(result);
    return true;
}

std::string formatEndpointList(std::span<const dmx::Endpoint> endpoints, std::uint16_t defaultPort) {
    std::string text;
    for (const dmx::Endpoint& endpoint : endpoints) {
        if (!text.empty()) text += ", ";
        text += endpoint.port == defaultPort ? endpoint.address.toString() : endpoint.toString();
    }
    return text;
}

bool parseEndpointList(std::string_view text, std::uint16_t defaultPort, std::vector<dmx::Endpoint>& out) {
    std::vector<dmx::Endpoint> result;
    for (const std::string_view token : splitTokens(text)) {
        const std::optional<dmx::Endpoint> endpoint = dmx::Endpoint::parse(token, defaultPort);
        if (!endpoint) return false;
        result.push_back(*endpoint);
    }
    out = std::move(result);
    return true;
}

}  // namespace dmxviz::ui
