#include "FixtureTestGdtf.h"
#include "FixtureTestUtil.h"
#include "fixtures/Archive.h"
#include "fixtures/FixtureLibrary.h"
#include "fixtures/NativeFormat.h"

#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>

using namespace dmxviz;
using namespace dmxviz::fixtures;
using namespace dmxviz::fixtures::test;
namespace fs = std::filesystem;

namespace {

// Scratch folder under the system temp directory, removed on destruction.
class TempDir {
public:
    TempDir() {
        static std::atomic<int> counter{0};
        path_ = fs::temp_directory_path() / ("dmxviz_library_test_" + std::to_string(counter++) + "_" +
                                             std::to_string(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFF));
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void writeText(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
}

void writeBytes(const fs::path& file, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(file.parent_path());
    std::string error;
    REQUIRE(writeFileBytes(file, bytes, &error));
}

void writeNative(const fs::path& file, const FixtureType& type) {
    fs::create_directories(file.parent_path());
    std::string error;
    INFO(error);
    REQUIRE(FixtureSerializer::saveFile(type, file, &error));
}

// A folder with one fixture of each kind plus files that must not break the load.
void fillLibraryFolder(const fs::path& dir) {
    writeNative(dir / "generic" / "spot.dmxviz-fixture.json", makeTestSpot());
    fs::create_directories(dir / "ofl" / "clay-paky");
    fs::copy_file(fs::path(DMXVIZ_TEST_DATA_DIR) / "fixtures" / "ofl" / "clay-paky" / "sharpy.json",
                  dir / "ofl" / "clay-paky" / "sharpy.json");
    writeBytes(dir / "gdtf" / "acme-spot.gdtf", makeGdtfArchive(movingHeadXml()));
    writeText(dir / "notes.json", R"({"just": "some notes"})");                   // not a fixture: skipped
    writeText(dir / "readme.txt", "ignored");                                     // not a fixture type
    writeText(dir / "broken" / "bad.dmxviz-fixture.json", "{ this is not json");  // error
    writeBytes(dir / "broken" / "bad.gdtf", {1, 2, 3, 4});                        // error
}

}  // namespace

TEST_CASE("library loads native, OFL and GDTF files from a folder and reports bad files") {
    TempDir dir;
    fillLibraryFolder(dir.path());

    FixtureLibrary lib;
    CHECK(lib.loadDirectory(dir.path()) == 3);
    CHECK(lib.size() == 3);

    // The two broken files are reported with their path; the plain JSON and text file are not errors.
    REQUIRE(lib.errors().size() == 2);
    for (const auto& e : lib.errors()) {
        CHECK(e.path.filename().string().starts_with("bad."));
        CHECK_FALSE(e.message.empty());
    }

    const FixtureType* native = lib.find("test/spot");
    REQUIRE(native != nullptr);
    CHECK(native->source == FixtureSource::Native);
    CHECK(native->modes.size() == 1);
    CHECK(lib.sourcePath("test/spot").filename() == "spot.dmxviz-fixture.json");

    const FixtureType* ofl = lib.find("clay-paky/sharpy");
    REQUIRE(ofl != nullptr);
    CHECK(ofl->source == FixtureSource::Ofl);
    CHECK(ofl->manufacturer == "Clay Paky");

    const FixtureType* gdtf = lib.find("acme-lighting/test-spot-700");
    REQUIRE(gdtf != nullptr);
    CHECK(gdtf->source == FixtureSource::Gdtf);
    CHECK(gdtf->modes.size() == 2);

    CHECK(lib.find("nope/none") == nullptr);
    lib.clearErrors();
    CHECK(lib.errors().empty());
}

TEST_CASE("library lists fixtures sorted by manufacturer and name") {
    TempDir dir;
    fillLibraryFolder(dir.path());
    FixtureLibrary lib;
    lib.loadDirectory(dir.path());

    const auto all = lib.all();
    REQUIRE(all.size() == 3);
    CHECK(all[0]->manufacturer == "Acme Lighting");
    CHECK(all[1]->manufacturer == "Clay Paky");
    CHECK(all[2]->manufacturer == "Test");

    // Same manufacturer: by name, ignoring case.
    FixtureType b = makeTestRgbwPar();
    b.name = "bravo";
    FixtureType a = makeTestRgbwPar();
    a.id = "test/alpha";
    a.name = "Alpha";
    REQUIRE(lib.addOrReplace(b));
    REQUIRE(lib.addOrReplace(a));
    const auto sorted = lib.all();
    REQUIRE(sorted.size() == 5);
    CHECK(sorted[2]->name == "Alpha");
    CHECK(sorted[3]->name == "bravo");
    CHECK(sorted[4]->name == "Spot");
}

TEST_CASE("library search is case-insensitive over manufacturer, name and id") {
    TempDir dir;
    fillLibraryFolder(dir.path());
    FixtureLibrary lib;
    lib.loadDirectory(dir.path());

    CHECK(lib.search("").size() == 3);
    CHECK(lib.search("   ").size() == 3);
    REQUIRE(lib.search("SHARPY").size() == 1);
    CHECK(lib.search("sharpy")[0]->id == "clay-paky/sharpy");
    CHECK(lib.search("clay paky").size() == 1);    // every word must match
    CHECK(lib.search("clay sharpy").size() == 1);  // words may hit different fields
    CHECK(lib.search("clay acme").empty());
    CHECK(lib.search("zzz").empty());
    REQUIRE(lib.search("acme").size() == 1);  // manufacturer
    CHECK(lib.search("acme")[0]->name == "Test Spot 700");
    REQUIRE(lib.search("test/spot").size() == 1);  // id
    const auto spots = lib.search("spot");         // both "Test Spot 700" (GDTF) and "Spot" (native)
    REQUIRE(spots.size() == 2);
    CHECK(spots[0]->manufacturer == "Acme Lighting");  // result order follows all()
    CHECK(spots[1]->manufacturer == "Test");
}

TEST_CASE("library pointers stay valid when types are added") {
    FixtureLibrary lib;
    REQUIRE(lib.addOrReplace(makeTestSpot()));
    const FixtureType* spot = lib.find("test/spot");
    REQUIRE(spot != nullptr);
    for (int i = 0; i < 300; ++i) {
        FixtureType t = makeTestRgbwPar();
        t.id = "test/par-" + std::to_string(i);
        t.name = "Par " + std::to_string(i);
        REQUIRE(lib.addOrReplace(std::move(t)));
    }
    CHECK(lib.size() == 301);
    CHECK(lib.find("test/spot") == spot);
    CHECK(spot->name == "Spot");
    CHECK(spot->modes.size() == 1);
}

TEST_CASE("library: duplicate ids, last loaded wins") {
    TempDir dir;
    FixtureType first = makeTestRgbwPar();
    first.description = "from a";
    FixtureType second = makeTestRgbwPar();
    second.description = "from b";
    writeNative(dir.path() / "a" / "par.dmxviz-fixture.json", first);
    writeNative(dir.path() / "b" / "par.dmxviz-fixture.json", second);

    FixtureLibrary lib;
    CHECK(lib.loadDirectory(dir.path()) == 2);  // both files load ...
    CHECK(lib.size() == 1);                     // ... into one entry
    const FixtureType* par = lib.find("test/rgbw-par");
    REQUIRE(par != nullptr);
    CHECK(par->description == "from b");  // paths load in sorted order
    CHECK(lib.sourcePath("test/rgbw-par") == dir.path() / "b" / "par.dmxviz-fixture.json");

    // A replaced type stays alive for whoever still points at it.
    std::string error;
    auto id = lib.importFile(dir.path() / "a" / "par.dmxviz-fixture.json", &error);
    INFO(error);
    REQUIRE(id.has_value());
    CHECK(*id == "test/rgbw-par");
    CHECK(lib.size() == 1);
    CHECK(lib.find("test/rgbw-par")->description == "from a");
    CHECK(par->description == "from b");
}

TEST_CASE("library importFile dispatches on content and extension") {
    TempDir dir;
    fillLibraryFolder(dir.path());
    FixtureLibrary lib;
    std::string error;

    auto sharpy = lib.importFile(dir.path() / "ofl" / "clay-paky" / "sharpy.json", &error);
    INFO(error);
    REQUIRE(sharpy.has_value());
    CHECK(*sharpy == "clay-paky/sharpy");

    // Upper-case extension, and a native file under a different name (found by "formatVersion").
    writeBytes(dir.path() / "SHOUT.GDTF", makeGdtfArchive(pixelBarXml(), false, false));
    auto bar = lib.importFile(dir.path() / "SHOUT.GDTF", &error);
    INFO(error);
    REQUIRE(bar.has_value());
    CHECK(*bar == "acme-lighting/pixel-bar-4");
    CHECK(lib.find(*bar)->beamCount() == 4);

    std::string nativeText = FixtureSerializer::toString(makeTestRgbwPar());
    writeText(dir.path() / "renamed.json", nativeText);
    auto renamed = lib.importFile(dir.path() / "renamed.json", &error);
    INFO(error);
    REQUIRE(renamed.has_value());
    CHECK(*renamed == "test/rgbw-par");

    // Failures leave the library untouched and say why; the error pointer is optional.
    const std::size_t before = lib.size();
    CHECK_FALSE(lib.importFile(dir.path() / "missing.gdtf", &error).has_value());
    CHECK_FALSE(error.empty());
    error.clear();
    CHECK_FALSE(lib.importFile(dir.path() / "notes.json", &error).has_value());
    CHECK(error.find("not a fixture") != std::string::npos);
    error.clear();
    CHECK_FALSE(lib.importFile(dir.path() / "readme.txt", &error).has_value());
    CHECK_FALSE(error.empty());
    CHECK_FALSE(lib.importFile(dir.path() / "broken" / "bad.gdtf").has_value());
    CHECK(lib.size() == before);

    std::vector<std::string> warnings;
    REQUIRE(lib.importFile(dir.path() / "gdtf" / "acme-spot.gdtf", &error, &warnings).has_value());
    CHECK_FALSE(warnings.empty());  // the generated GDTF has an unknown element and attribute
}

TEST_CASE("library addOrReplace and saveNative") {
    TempDir dir;
    FixtureLibrary lib;
    std::string error;

    FixtureType nameless = makeTestRgbwPar();
    nameless.name.clear();
    CHECK_FALSE(lib.addOrReplace(nameless, &error));
    CHECK_FALSE(error.empty());
    CHECK(lib.size() == 0);

    // The id is generated when the editor leaves it empty.
    FixtureType fresh = makeTestRgbwPar();
    fresh.id.clear();
    fresh.manufacturer = "My Company";
    fresh.name = "Home Brew";
    REQUIRE(lib.addOrReplace(fresh));
    const FixtureType* added = lib.find("my-company/home-brew");
    REQUIRE(added != nullptr);
    CHECK(added->id == "my-company/home-brew");

    // Editing keeps the id; the replacement is what find() returns.
    FixtureType edited = *added;
    edited.description = "edited";
    REQUIRE(lib.addOrReplace(edited));
    CHECK(lib.size() == 1);
    CHECK(lib.find("my-company/home-brew")->description == "edited");

    // Save a type with wheels, resources and 16-bit channels and read it back in a new library.
    REQUIRE(lib.addOrReplace(makeTestSpot()));
    const fs::path file = dir.path() / "saved" / "spot.dmxviz-fixture.json";
    fs::create_directories(file.parent_path());
    REQUIRE(lib.saveNative("test/spot", file, &error));
    CHECK_FALSE(lib.saveNative("no/such", file, &error));
    CHECK(error.find("no/such") != std::string::npos);

    FixtureLibrary other;
    auto id = other.importFile(file, &error);
    INFO(error);
    REQUIRE(id.has_value());
    const FixtureType* loaded = other.find("test/spot");
    REQUIRE(loaded != nullptr);
    CHECK(loaded->wheels.size() == 3);
    CHECK(loaded->resources.size() == 1);
    CHECK(loaded->modes[0].channels.size() == lib.find("test/spot")->modes[0].channels.size());
    CHECK(loaded->modes[0].channels[0].offsets.size() == 2);
}

TEST_CASE("library loadDirectory on a missing folder reports an error") {
    TempDir dir;
    FixtureLibrary lib;
    CHECK(lib.loadDirectory(dir.path() / "does-not-exist") == 0);
    REQUIRE(lib.errors().size() == 1);
    CHECK(lib.errors()[0].message.find("not a directory") != std::string::npos);
    CHECK(lib.loadDirectory(dir.path()) == 0);  // an empty folder is fine
    CHECK(lib.errors().size() == 1);
}
