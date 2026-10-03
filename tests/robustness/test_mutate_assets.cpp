// Mutation robustness and size limits of the asset loaders: glTF/GLB, OBJ, 3DS, PNG and SVG.

#include "ModelSamples.h"
#include "Mutate.h"

#include "assets/ImageLoader.h"
#include "assets/ModelLoader.h"
#include "core/Limits.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace dmxviz;
using namespace dmxviz::assets;
using namespace dmxviz::robust;
namespace fs = std::filesystem;

namespace {

std::optional<std::vector<std::uint8_t>> mapResolver(void* user, std::string_view uri) {
    auto* files = static_cast<std::map<std::string, Bytes>*>(user);
    auto it = files->find(std::string(uri));
    if (it == files->end()) return std::nullopt;
    return it->second;
}

// Whatever the loader accepted must be internally consistent: indices in range, finite counts.
void checkModel(const ModelData& model) {
    CHECK(!model.parts.empty());
    for (const ModelPart& part : model.parts) {
        for (std::uint32_t index : part.mesh.indices) {
            if (index >= part.mesh.vertices.size()) {
                FAIL("index out of range in a loaded mesh");
                return;
            }
        }
        CHECK(part.mesh.indices.size() % 3 == 0);
    }
}

bool load(const Bytes& bytes, std::string_view format, std::map<std::string, Bytes>& files) {
    std::string error;
    auto model = loadModelFromMemory(bytes, format, {}, &error, &mapResolver, &files);
    if (model) checkModel(*model);
    return model.has_value();
}

Bytes pngSample() {
    ImageData image;
    image.width = 24;
    image.height = 16;
    image.channels = 4;
    image.pixels.resize(24 * 16 * 4);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) image.pixels[i] = static_cast<std::uint8_t>(i * 13);
    const fs::path path = fs::temp_directory_path() / "dmxviz_robust_sample.png";
    REQUIRE(writePng(path, image));
    std::ifstream in(path, std::ios::binary);
    Bytes bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::error_code ec;
    fs::remove(path, ec);
    return bytes;
}

// A PNG signature plus an IHDR that claims `width` x `height`, and nothing else.
Bytes pngHeaderOnly(std::uint32_t width, std::uint32_t height) {
    Bytes b{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    for (std::uint32_t v : {width, height})
        for (int shift = 24; shift >= 0; shift -= 8) b.push_back(static_cast<std::uint8_t>(v >> shift));
    for (std::uint8_t v : {8, 6, 0, 0, 0, 0, 0, 0, 0}) b.push_back(v);  // depth, RGBA, methods, CRC (unchecked)
    return b;
}

}  // namespace

TEST_CASE("mutation: glTF, GLB, OBJ and 3DS loaders survive damaged files") {
    using namespace samples;
    Rng rng(0x61F0B001);
    std::map<std::string, Bytes> files{{"scene.mtl", toBytes(mtlText())}};

    const Bytes glb = quadGlb();
    const std::string gltf = quadGltfText();
    const std::string obj = objText();
    const Bytes tds = make3ds();
    {
        std::string error;
        REQUIRE_MESSAGE(loadModelFromMemory(glb, "glb", {}, &error), error);
        REQUIRE_MESSAGE(loadModelFromMemory(toBytes(gltf), "gltf", {}, &error), error);
        REQUIRE_MESSAGE(loadModelFromMemory(toBytes(obj), "obj", {}, &error, &mapResolver, &files), error);
        REQUIRE_MESSAGE(loadModelFromMemory(tds, "3ds", {}, &error), error);
    }
    const nlohmann::json gltfJson = nlohmann::json::parse(gltf);

    SlowestCall slowest;
    std::size_t accepted = 0;
    for (std::size_t i = 0; i < 700; ++i) {
        slowest.run([&] {
            accepted += load(mutateBytes(glb, rng, i), "glb", files);
            accepted += load(toBytes(mutateJsonText(gltfJson, gltf, rng, i)), "gltf", files);
            accepted += load(mutateBytes(toBytes(obj), rng, i), "obj", files);
            accepted += load(mutateBytes(tds, rng, i), "3ds", files);
            accepted += load(mutateBytes(tds, rng, i), "", files);  // format detection from the bytes
        });
    }
    MESSAGE("models: " << accepted << " of 3500 mutations were still accepted");
    CHECK(accepted > 0);
    CHECK(slowest.seconds() < 1.0);
}

TEST_CASE("mutation: PNG and SVG image loading survive damaged files") {
    Rng rng(0x1A9E0002);
    const Bytes png = pngSample();
    REQUIRE(!png.empty());
    std::vector<Bytes> svgs;
    for (const auto& entry : fs::directory_iterator(fs::path(DMXVIZ_DATA_DIR) / "gobos")) {
        if (entry.path().extension() != ".svg") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        svgs.emplace_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    REQUIRE(svgs.size() >= 4);
    std::sort(svgs.begin(), svgs.end());  // directory order is not deterministic

    SlowestCall slowest;
    std::string error;
    for (std::size_t i = 0; i < 600; ++i) {
        slowest.run([&] {
            if (auto image = loadImageFromMemory(mutateBytes(png, rng, i), "png", {}, &error)) CHECK(image->valid());
            const Bytes& svg = svgs[i % svgs.size()];
            ImageLoadOptions options;
            options.svgSize = 64;
            options.desiredChannels = i % 2 ? 1 : 4;
            if (auto image = loadImageFromMemory(mutateBytes(svg, rng, i), "svg", options, &error))
                CHECK(image->valid());
        });
    }
    CHECK(slowest.seconds() < 1.0);
}

TEST_CASE("limits: images with absurd sizes are rejected before decoding") {
    std::string error;
    CHECK_FALSE(loadImageFromMemory(pngHeaderOnly(100000, 100000), "png", {}, &error));
    INFO(error);
    CHECK(error.find("too large") != std::string::npos);
    CHECK_FALSE(loadImageFromMemory(pngHeaderOnly(16385, 4), "png", {}, &error));
    CHECK_FALSE(loadImageFromMemory(pngHeaderOnly(16384, 16384), "png", {}, &error));  // 268 megapixels
    // Within the limits the header is accepted and decoding then fails on the missing data, politely.
    CHECK_FALSE(loadImageFromMemory(pngHeaderOnly(64, 64), "png", {}, &error));

    // SVG sizes: infinity, NaN-ish and astronomic values.
    for (const char* size : {"1e999", "1e30", "0", "-5"}) {
        const std::string svg = std::string("<svg xmlns='http://www.w3.org/2000/svg' width='") + size +
                                "' height='10'><rect width='5' height='5'/></svg>";
        loadImageFromMemory(toBytes(svg), "svg", {}, &error);  // must not crash; result is irrelevant
    }
    ImageLoadOptions huge;
    huge.svgSize = 2'000'000'000;  // clamped, not allocated
    const std::string rect =
        "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><rect width='5' height='5'/></svg>";
    const auto image = loadImageFromMemory(toBytes(rect), "svg", huge, &error);
    REQUIRE(image);
    CHECK(image->width <= 4096);
}

TEST_CASE("limits: models cannot exhaust memory or time") {
    using namespace samples;
    std::map<std::string, Bytes> files;
    std::string error;

    // An accessor without a buffer view may claim any number of vertices.
    {
        std::string json = quadJson(60, "", R"([{"mesh":0}])");
        const std::string from = R"("bufferView":0,"componentType":5126,"count":4)";
        const std::size_t at = json.find(from);
        REQUIRE(at != std::string::npos);
        json.replace(at, from.size(), R"("componentType":5126,"count":2000000000)");
        CHECK_FALSE(loadModelFromMemory(makeGlb(json, quadBuffer()), "glb", {}, &error));
    }

    // Instancing bomb: the scene lists the same mesh node 200000 times (cgltf already refuses a node that
    // is the child of several parents, but a scene may repeat its roots).
    {
        std::string roots = "[";
        for (int i = 0; i < 200000; ++i) roots += i ? ",0" : "0";
        roots += "]";
        const Bytes bin = quadBuffer();
        std::string json = quadJson(bin.size(), "", R"([{"mesh":0}])");
        const std::string from = R"("scenes":[{"nodes":[0]}])";
        json.replace(json.find(from), from.size(), "\"scenes\":[{\"nodes\":" + roots + "}]");
        SlowestCall slowest;
        slowest.run([&] { CHECK_FALSE(loadModelFromMemory(makeGlb(json, bin), "glb", {}, &error)); });
        CHECK(slowest.seconds() < 5.0);
        INFO(error);
        CHECK(error.find("too") != std::string::npos);
    }

    // glTF JSON nested absurdly deeply (cgltf skips unknown values recursively).
    {
        std::string deep(200000, '[');
        deep += std::string(200000, ']');
        const std::string json = R"({"asset":{"version":"2.0"},"extras":)" + deep + "}";
        CHECK_FALSE(loadModelFromMemory(toBytes(json), "gltf", {}, &error));
        CHECK(error.find("nested") != std::string::npos);
    }

    // 3DS: thousands of material groups used to make loading quadratic.
    CHECK_FALSE(loadModelFromMemory(make3ds(5000), "3ds", {}, &error));
    CHECK(error.find("too many") != std::string::npos);

    // Files over the size limit are refused without being read.
    CHECK(limits::fileTooLarge(fs::path(DMXVIZ_DATA_DIR) / "shows" / "demo.dmxviz", 10));
    CHECK_FALSE(limits::fileTooLarge(fs::path(DMXVIZ_DATA_DIR) / "shows" / "does-not-exist.dmxviz"));
}
