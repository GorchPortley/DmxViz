// Size and depth limits of the fixture loaders: zip bombs, deeply nested geometry, huge pixel matrices.

#include "../fixtures/FixtureTestGdtf.h"
#include "Mutate.h"

#include "core/Limits.h"
#include "fixtures/Archive.h"
#include "fixtures/GdtfImporter.h"
#include "fixtures/NativeFormat.h"
#include "fixtures/OflImporter.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

using namespace dmxviz;
using namespace dmxviz::fixtures;
using namespace dmxviz::robust;
using nlohmann::json;

namespace {

void writeLe32(Bytes& b, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
}

// Patches the declared uncompressed size of every entry (central directory and local headers).
Bytes claimUncompressedSize(Bytes zip, std::uint32_t size) {
    for (std::size_t i = 0; i + 30 < zip.size(); ++i) {
        if (zip[i] == 'P' && zip[i + 1] == 'K' && zip[i + 2] == 0x01 && zip[i + 3] == 0x02)
            writeLe32(zip, i + 24, size);
        if (zip[i] == 'P' && zip[i + 1] == 'K' && zip[i + 2] == 0x03 && zip[i + 3] == 0x04)
            writeLe32(zip, i + 22, size);
    }
    return zip;
}

}  // namespace

TEST_CASE("limits: a zip entry that claims to be huge is refused before extraction") {
    const Bytes honest = test::makeGdtfArchive(test::movingHeadXml());
    std::string error;
    REQUIRE_MESSAGE(importGdtf(honest, &error), error);

    // 300 MB declared (above the 256 MB entry limit). Without the check miniz would allocate it up front.
    const Bytes bomb = claimUncompressedSize(honest, 300u * 1024u * 1024u);
    ZipReader reader;
    REQUIRE(reader.open(bomb, &error));
    for (const std::string& name : reader.entries()) CHECK_FALSE(reader.read(name).has_value());
    CHECK_FALSE(importGdtf(bomb, &error));
}

TEST_CASE("limits: deeply nested fixture geometry is rejected") {
    std::string error;
    auto fixtureWithDepth = [](std::size_t depth) {
        std::string geometry;
        for (std::size_t i = 0; i < depth; ++i) geometry += R"({"name":"g","type":"generic","children":[)";
        geometry += R"({"name":"leaf","type":"generic"})";
        for (std::size_t i = 0; i < depth; ++i) geometry += "]}";
        return R"({"formatVersion":1,"id":"x","name":"X","manufacturer":"M","geometry":)" + geometry +
               R"(,"modes":[{"name":"m","footprint":1,"channels":[]}]})";
    };
    // 100000 levels: refused by the JSON depth check, no recursion.
    CHECK_FALSE(FixtureSerializer::fromString(fixtureWithDepth(100000), {}, &error));
    INFO(error);
    CHECK(error.find("deeply") != std::string::npos);
    // 200 levels pass the JSON check but exceed the geometry depth limit.
    CHECK_FALSE(FixtureSerializer::fromString(fixtureWithDepth(limits::kMaxNodeDepth + 72), {}, &error));
    INFO(error);
    CHECK(error.find("nested too deeply") != std::string::npos);
}

TEST_CASE("limits: an OFL fixture cannot declare a gigantic pixel matrix") {
    std::string error;
    auto fixture = [](const std::string& matrix) {
        return json::parse(R"({"name":"Bar","categories":["Pixel Bar"],"matrix":)" + matrix +
                           R"(,"availableChannels":{"Dimmer":{"capability":{"type":"Intensity"}}},)"
                           R"("modes":[{"name":"1ch","channels":["Dimmer"]}]})");
    };
    const nlohmann::ordered_json huge =
        nlohmann::ordered_json::parse(fixture(R"({"pixelCount":[100000,100000,100000]})").dump());
    CHECK_FALSE(importOfl(huge, {}, &error));
    INFO(error);
    CHECK(error.find("pixels") != std::string::npos);

    // A key list that is merely long is refused at the pixel cap.
    std::string keys = "[[[";
    for (int i = 0; i < 5000; ++i) keys += (i ? ",\"p" : "\"p") + std::to_string(i) + "\"";
    keys += "]]]";
    const nlohmann::ordered_json many = nlohmann::ordered_json::parse(fixture("{\"pixelKeys\":" + keys + "}").dump());
    CHECK_FALSE(importOfl(many, {}, &error));
    CHECK(error.find("pixels") != std::string::npos);
}
