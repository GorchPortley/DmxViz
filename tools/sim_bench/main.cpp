// sim_bench: headless CPU benchmark of one frame of simulation work (NFR-1 in docs/REQUIREMENTS.md).
//
// Builds a rig (default 500 fixtures, ~1000 beams, 64 universes), animates the DMX values every frame and
// times, per frame:
//   * dmx snapshot   UniverseStore::snapshot (what DmxManager does at the start of a frame)
//   * simulation     Simulation::update (decode, physics, emit meshes and beams)
//   * beam packer    render::BeamPacker::pack on the produced beams (CPU half of the volumetric pass)
// and counts heap allocations (global operator new) for each part. Nothing is drawn: no window, no GPU.
//
// Usage: sim_bench [--frames N] [--warmup N] [--fixtures N] [--universes N] [--no-selection] [--edit] [--hold]
//   --hold   freeze the DMX values (a show that holds its look); the default animates every fixture every frame
//   --edit   also moves one truss line (with its fixtures) every frame, like dragging a gizmo: this makes the
//            simulation re-sync its fixture list and rebuild the static meshes every frame

#include "AllocCounter.h"
#include "DmxAnimator.h"
#include "RigBuilder.h"

#include "app/Simulation.h"
#include "assets/AssetLibrary.h"
#include "core/Log.h"
#include "dmx/DmxSnapshot.h"
#include "dmx/UniverseStore.h"
#include "fixtures/FixtureLibrary.h"
#include "render/BeamMath.h"
#include "render/BeamPacker.h"
#include "render/GoboAtlas.h"
#include "render/RenderScene.h"
#include "stage/Scene.h"
#include "stage/Selection.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace dmxviz;
using Clock = std::chrono::steady_clock;

struct Options {
    int frames = 600;
    int warmup = 60;
    int fixtures = 500;
    int universes = 64;
    bool selection = true;
    bool edit = false;
    bool hold = false;
};

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto intValue = [&](int& dst) {
            if (i + 1 >= argc) return false;
            dst = std::atoi(argv[++i]);
            return dst >= 0;
        };
        if (a == "--frames" && intValue(o.frames)) continue;
        if (a == "--warmup" && intValue(o.warmup)) continue;
        if (a == "--fixtures" && intValue(o.fixtures)) continue;
        if (a == "--universes" && intValue(o.universes)) continue;
        if (a == "--no-selection") {
            o.selection = false;
            continue;
        }
        if (a == "--hold") {
            o.hold = true;
            continue;
        }
        if (a == "--edit") {
            o.edit = true;
            continue;
        }
        std::fprintf(stderr,
                     "usage: sim_bench [--frames N] [--warmup N] [--fixtures N] [--universes N] [--no-selection] "
                     "[--edit] [--hold]\n");
        return false;
    }
    o.frames = std::max(o.frames, 1);
    return true;
}

// Timing and allocation results of one measured part of the frame.
struct Section {
    const char* name;
    std::vector<double> ms;
    std::uint64_t allocs = 0;
    std::uint64_t bytes = 0;

    template <class Fn>
    void measure(Fn&& fn) {
        const simbench::AllocStats before = simbench::allocStats();
        const auto t0 = Clock::now();
        fn();
        const auto t1 = Clock::now();
        const simbench::AllocStats after = simbench::allocStats();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        allocs += after.count - before.count;
        bytes += after.bytes - before.bytes;
    }

    double average() const {
        double sum = 0.0;
        for (double v : ms) sum += v;
        return ms.empty() ? 0.0 : sum / static_cast<double>(ms.size());
    }
    double percentile(double p) const {
        std::vector<double> sorted = ms;
        std::sort(sorted.begin(), sorted.end());
        if (sorted.empty()) return 0.0;
        return sorted[static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1))];
    }
    void print() const {
        const double frames = static_cast<double>(std::max<std::size_t>(ms.size(), 1));
        std::printf("%-14s %8.3f %8.3f %8.3f %8.3f %14.2f %12.0f\n", name, average(), percentile(0.5), percentile(0.95),
                    percentile(1.0), static_cast<double>(allocs) / frames, static_cast<double>(bytes) / frames);
    }
};

// A camera in front of the stage looking at the middle of the rig (as in the app's default view).
render::beammath::Frustum defaultFrustum() {
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 3.0f, 22.0f), glm::vec3(0.0f, 4.0f, -2.0f), glm::vec3(0, 1, 0));
    const glm::mat4 projection = glm::perspective(degToRad(50.0f), 16.0f / 9.0f, 0.05f, 500.0f);
    return render::beammath::frustumFromViewProj(projection * view);
}

// Proves that the global operator new replacement is active (a silent 0 would make the numbers meaningless).
bool allocationCounterWorks() {
    static int* volatile sink;  // volatile: stops the compiler from optimising the new/delete pair away
    const simbench::AllocStats before = simbench::allocStats();
    int* p = new int[16];
    sink = p;
    delete[] p;
    return sink != nullptr && simbench::allocStats().count > before.count;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) return 2;
    log::setMinLevel(log::Level::Warn);
    if (!allocationCounterWorks()) std::fprintf(stderr, "sim_bench: warning: allocation counting is not active\n");

    fixtures::FixtureLibrary library;
    const std::filesystem::path fixtureDir = std::filesystem::path(DMXVIZ_DATA_DIR) / "fixtures";
    if (library.loadDirectory(fixtureDir) == 0) {
        std::fprintf(stderr, "sim_bench: no fixture types found in %s\n", fixtureDir.string().c_str());
        return 1;
    }

    stage::Scene scene;
    std::vector<simbench::RigFixture> rigFixtures;
    std::string error;
    simbench::RigOptions rigOptions;
    rigOptions.fixtures = options.fixtures;
    rigOptions.universes = options.universes;
    if (!simbench::buildRig(scene, library, rigOptions, rigFixtures, error)) {
        std::fprintf(stderr, "sim_bench: %s\n", error.c_str());
        return 1;
    }

    assets::AssetLibrary assets;
    app::Simulation simulation(assets, library);
    stage::Selection selection;
    if (options.selection) {
        // A realistic editing session: a handful of selected fixtures and one hovered (highlight path).
        std::vector<NodeId> picked;
        for (std::size_t i = 0; i < rigFixtures.size(); i += 25) picked.push_back(rigFixtures[i].node);
        selection.setMany(picked);
        selection.setHover(rigFixtures[1 % rigFixtures.size()].node);
    }
    simulation.setSelection(&selection);

    simbench::DmxAnimator animator(rigFixtures, options.universes);
    dmx::UniverseStore store;
    store.setProgrammerMode(dmx::ProgrammerMode::Override);
    dmx::DmxSnapshot snapshot;
    render::RenderScene renderScene;
    render::BeamPacker packer;
    render::GoboAtlas gobos;  // never init()ed: BeamPacker only asks it for layer numbers (CPU side)
    const render::RenderSettings settings;
    const render::beammath::Frustum frustum = defaultFrustum();
    const glm::vec3 cameraPos(0.0f, 3.0f, 22.0f);
    // 1080p with half-res haze: focal length of the 50 degree camera in volume pixels.
    const render::VolumeView volumeView{0.5f * 540.0f / std::tan(degToRad(25.0f)), 960.0f * 540.0f};

    // --edit: the truss line that carries the first fixture is nudged every frame.
    const stage::Node* firstFixture = scene.find(rigFixtures.front().node);
    const NodeId editedNode = firstFixture != nullptr ? firstFixture->parent() : kInvalidNode;
    stage::Transform editedTransform;
    if (const stage::Node* n = scene.find(editedNode)) editedTransform = n->transform();

    Section snapshotSection{"dmx snapshot"};
    Section simSection{"simulation"};
    Section packSection{"beam packer"};

    constexpr double kFrameTime = 1.0 / 60.0;
    const int total = options.warmup + options.frames;
    for (int frame = 0; frame < total; ++frame) {
        const double t = static_cast<double>(frame) * kFrameTime;
        animator.animate(options.hold ? 0.0 : t, store);  // harness work: not measured
        if (options.edit) {
            editedTransform.position.x = 0.01f * static_cast<float>(frame % 100);
            scene.setTransform(editedNode, editedTransform);
        }

        const bool measured = frame >= options.warmup;
        auto run = [&](Section& s, auto&& fn) {
            if (measured)
                s.measure(fn);
            else
                fn();
        };
        run(snapshotSection, [&] { store.snapshot(snapshot); });
        run(simSection, [&] { simulation.update(scene, snapshot, static_cast<float>(kFrameTime), t, renderScene); });
        run(packSection, [&] { packer.pack(renderScene.beams, frustum, cameraPos, settings, gobos, volumeView); });
    }

    std::printf("rig: %d fixtures, %zu beams, %zu mesh instances per frame, %d universes\n", simulation.fixtureCount(),
                renderScene.beams.size(), renderScene.meshes.size(), options.universes);
    std::printf("packed: %zu beam instances (%d volumetric), %zu lens glows after culling\n", packer.instances().size(),
                packer.volumetricCount(), packer.glows().size());
    std::printf("haze cost estimate (1080p, half res): %.1f M covered pixels, march step cap %.1f\n",
                static_cast<double>(packer.volumetricCoveragePixels()) * 1.0e-6,
                static_cast<double>(packer.marchStepCap()));
    std::printf("%d frames after %d warm-up frames%s, times in ms per frame\n\n", options.frames, options.warmup,
                options.edit   ? " (scene edited every frame)"
                : options.hold ? " (DMX held)"
                               : "");
    std::printf("%-14s %8s %8s %8s %8s %14s %12s\n", "part", "avg", "p50", "p95", "max", "allocs/frame", "bytes/frame");
    snapshotSection.print();
    simSection.print();
    packSection.print();

    constexpr double kBudgetMs = 2.0;
    std::printf("\nNFR-1 simulation budget %.1f ms: avg %.3f ms -> %s; steady-state allocations/frame: %.2f\n",
                kBudgetMs, simSection.average(), simSection.average() < kBudgetMs ? "met" : "NOT met",
                static_cast<double>(simSection.allocs) / static_cast<double>(options.frames));
    return 0;
}
