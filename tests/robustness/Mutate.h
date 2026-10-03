#pragma once
// Deterministic input mutation for the robustness tests.
//
// Every parser that sees untrusted bytes is fed valid samples that were truncated, bit-flipped,
// spliced, filled with extreme values or replaced by random bytes. The random source is a fixed-seed
// generator of our own (std::distributions differ between standard libraries), so a failure always
// reproduces. The requirement is simple: no crash, no hang, no sanitizer report; the parsers report
// bad input through their normal error paths.

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::robust {

using Bytes = std::vector<std::uint8_t>;

// SplitMix64: tiny, fast, good enough for test input generation.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // Uniform in [0, n); n == 0 yields 0.
    std::size_t below(std::size_t n) { return n == 0 ? 0 : static_cast<std::size_t>(next() % n); }
    bool chance(std::size_t oneIn) { return below(oneIn) == 0; }

private:
    std::uint64_t state_;
};

inline Bytes toBytes(std::string_view text) {
    return Bytes(text.begin(), text.end());
}
inline std::string toText(const Bytes& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

// ---- byte-level mutations ----------------------------------------------------------------

// One mutated copy of `base`. `kind` cycles through the strategies so every one gets used.
inline Bytes mutateBytes(const Bytes& base, Rng& rng, std::size_t kind) {
    Bytes b = base;
    switch (kind % 8) {
        case 0:  // truncate
            b.resize(rng.below(b.size() + 1));
            break;
        case 1:  // flip a few bits
            for (std::size_t i = 0, n = 1 + rng.below(8); i < n && !b.empty(); ++i)
                b[rng.below(b.size())] ^= static_cast<std::uint8_t>(1u << rng.below(8));
            break;
        case 2:  // extreme byte values (length fields, counts)
            for (std::size_t i = 0, n = 1 + rng.below(4); i < n && !b.empty(); ++i) {
                static constexpr std::uint8_t kValues[] = {0x00, 0xFF, 0x7F, 0x80, 0x01};
                b[rng.below(b.size())] = kValues[rng.below(sizeof(kValues))];
            }
            break;
        case 3:  // overwrite a range with random bytes
            if (!b.empty()) {
                const std::size_t from = rng.below(b.size());
                const std::size_t count = 1 + rng.below(std::min<std::size_t>(16, b.size() - from));
                for (std::size_t i = 0; i < count; ++i) b[from + i] = static_cast<std::uint8_t>(rng.next());
            }
            break;
        case 4:  // delete a range
            if (!b.empty()) {
                const std::size_t from = rng.below(b.size());
                const std::size_t count = 1 + rng.below(std::min<std::size_t>(32, b.size() - from));
                b.erase(b.begin() + static_cast<std::ptrdiff_t>(from),
                        b.begin() + static_cast<std::ptrdiff_t>(from + count));
            }
            break;
        case 5:  // duplicate a range in place (counts no longer match the content)
            if (!b.empty()) {
                const std::size_t from = rng.below(b.size());
                const std::size_t count = 1 + rng.below(std::min<std::size_t>(32, b.size() - from));
                const Bytes chunk(b.begin() + static_cast<std::ptrdiff_t>(from),
                                  b.begin() + static_cast<std::ptrdiff_t>(from + count));
                b.insert(b.begin() + static_cast<std::ptrdiff_t>(from), chunk.begin(), chunk.end());
            }
            break;
        case 6:  // keep the start (magic numbers, headers), randomise the rest
            if (!b.empty()) {
                const std::size_t keep = rng.below(std::min<std::size_t>(b.size(), 48) + 1);
                b.resize(keep + rng.below(256));
                for (std::size_t i = keep; i < b.size(); ++i) b[i] = static_cast<std::uint8_t>(rng.next());
            }
            break;
        default:  // random garbage of random length
            b.resize(rng.below(300));
            for (std::uint8_t& v : b) v = static_cast<std::uint8_t>(rng.next());
            break;
    }
    return b;
}

// Calls fn(mutated) `count` times.
template <typename Fn>
void forEachByteMutation(const Bytes& base, Rng& rng, std::size_t count, Fn&& fn) {
    for (std::size_t i = 0; i < count; ++i) fn(mutateBytes(base, rng, i));
}

// ---- JSON-aware mutations ----------------------------------------------------------------

// Replacement values that tend to break parsers: type changes, extremes, huge strings, nesting.
inline nlohmann::json hostileJsonValue(Rng& rng) {
    using nlohmann::json;
    switch (rng.below(16)) {
        case 0:
            return nullptr;
        case 1:
            return true;
        case 2:
            return -1;
        case 3:
            return 0;
        case 4:
            return 4294967296.0;
        case 5:
            return 9223372036854775807LL;
        case 6:
            return -9223372036854775807LL - 1;
        case 7:
            return 1e300;
        case 8:
            return -1e300;
        case 9:
            return 1e-300;
        case 10:
            return std::string();
        case 11:
            return std::string(5000, 'x');
        case 12:
            return json::array();
        case 13:
            return json::object();
        case 14: {
            json nested = json::array();
            for (int i = 0; i < 60; ++i) nested = json::array({nested});
            return nested;
        }
        default:
            return 65536;
    }
}

namespace detail {

inline void collectPointers(const nlohmann::json& j, std::string path, std::vector<std::string>& out) {
    out.push_back(path);
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            std::string escaped = it.key();
            for (std::size_t p = 0; (p = escaped.find('~', p)) != std::string::npos; p += 2)
                escaped.replace(p, 1, "~0");
            for (std::size_t p = 0; (p = escaped.find('/', p)) != std::string::npos; p += 2)
                escaped.replace(p, 1, "~1");
            collectPointers(it.value(), path + "/" + escaped, out);
        }
    } else if (j.is_array()) {
        for (std::size_t i = 0; i < j.size(); ++i) collectPointers(j[i], path + "/" + std::to_string(i), out);
    }
}

}  // namespace detail

// Applies 1..3 random edits (replace with a hostile value, delete, duplicate a member, swap two
// members' values) to a copy of `base`.
inline nlohmann::json mutateJson(const nlohmann::json& base, Rng& rng) {
    using nlohmann::json;
    json doc = base;
    std::vector<std::string> pointers;
    for (std::size_t edits = 1 + rng.below(3); edits > 0; --edits) {
        pointers.clear();
        detail::collectPointers(doc, "", pointers);
        const std::string& target = pointers[rng.below(pointers.size())];
        if (target.empty()) {  // root: replace the whole document now and then
            if (rng.chance(8)) doc = hostileJsonValue(rng);
            continue;
        }
        try {
            const json::json_pointer ptr(target);
            const std::size_t op = rng.below(5);
            if (op <= 1) {
                doc[ptr] = hostileJsonValue(rng);
            } else if (op == 2) {
                const json::json_pointer parentPtr = ptr.parent_pointer();
                json& parent = doc[parentPtr];
                if (parent.is_object())
                    parent.erase(ptr.back());
                else if (parent.is_array() && !parent.empty())
                    parent.erase(parent.begin() + static_cast<std::ptrdiff_t>(rng.below(parent.size())));
            } else if (op == 3) {
                json& node = doc[ptr];
                if (node.is_array() && !node.empty())
                    node.push_back(node[rng.below(node.size())]);
                else if (node.is_array())
                    node.push_back(hostileJsonValue(rng));
                else if (node.is_object())
                    node["extra"] = hostileJsonValue(rng);
            } else {
                const std::string& other = pointers[rng.below(pointers.size())];
                if (!other.empty()) {
                    const json value = doc[json::json_pointer(other)];
                    doc[ptr] = value;
                }
            }
        } catch (const nlohmann::json::exception&) {
            // The edit did not apply to this shape; the next one will.
        }
    }
    return doc;
}

// A JSON text variant: mostly structure-aware edits, sometimes byte-level damage to the text.
inline std::string mutateJsonText(const nlohmann::json& base, const std::string& baseText, Rng& rng, std::size_t i) {
    if (i % 4 == 3) return toText(mutateBytes(toBytes(baseText), rng, rng.below(8)));
    return mutateJson(base, rng).dump();
}

// ---- XML attribute mutations --------------------------------------------------------------

// Replaces the value of a few random attributes (name="value") with hostile text, and
// sometimes damages the bytes of the document too.
inline std::string mutateXml(const std::string& base, Rng& rng, std::size_t i) {
    if (i % 4 == 3) return toText(mutateBytes(toBytes(base), rng, rng.below(8)));
    static const char* const kValues[] = {"",
                                          "-1",
                                          "0",
                                          "99999999999999999999",
                                          "1e308",
                                          "nan",
                                          "{",
                                          "{1,2}",
                                          "{{{{",
                                          "1,2,3,4,5,6,7,8,9,10",
                                          "0.0.0",
                                          "../../etc/passwd",
                                          "\xFF\xFE",
                                          "9999999999",
                                          "-2147483648",
                                          "1/0",
                                          "inf"};
    std::string out = base;
    for (std::size_t edits = 1 + rng.below(4); edits > 0; --edits) {
        // Find a random `="` and replace up to the closing quote.
        std::size_t pos = rng.below(out.size());
        pos = out.find("=\"", pos);
        if (pos == std::string::npos) pos = out.find("=\"");
        if (pos == std::string::npos) break;
        const std::size_t end = out.find('"', pos + 2);
        if (end == std::string::npos) break;
        out.replace(pos + 2, end - pos - 2, kValues[rng.below(sizeof(kValues) / sizeof(kValues[0]))]);
    }
    return out;
}

// ---- timing guard -------------------------------------------------------------------------

// Measures the slowest single call, to notice an input that makes a parser (nearly) hang.
class SlowestCall {
public:
    template <typename Fn>
    void run(Fn&& fn) {
        const auto start = std::chrono::steady_clock::now();
        fn();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (seconds > slowest_) slowest_ = seconds;
    }
    double seconds() const { return slowest_; }

private:
    double slowest_ = 0.0;
};

}  // namespace dmxviz::robust
