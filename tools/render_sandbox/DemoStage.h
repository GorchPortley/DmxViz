#pragma once
// A hand-built concert stage for the render sandbox: floor, deck, cyc wall,
// truss, and ~48 fixtures (profile spots with gobos and prisms, beam fixtures,
// washes, floor uplights, an LED pixel bar and blinders). It produces the same
// MeshInstance/BeamState data the fixture runtime will, without depending on
// the fixtures or stage modules.

#include "render/RenderScene.h"

#include <vector>

namespace dmxviz::assets {
class AssetLibrary;
}

namespace dmxviz::sandbox {

class DemoStage {
public:
    enum class Rig {
        Show,    // the concert stage (default)
        Single,  // two gobo spots, for close-up inspection of one beam
        Stress,  // `stressBeams` moving heads in a grid (performance test)
    };

    void build(assets::AssetLibrary& assets, Rig rig, int stressBeams);

    // Appends this moment's meshes and beams (pan sweeps, gobo spin) to the scene.
    void update(double timeSeconds, render::RenderScene& scene) const;

    int fixtureCount() const { return static_cast<int>(fixtures_.size()); }

private:
    enum class Kind { Spot, Beam, Wash, Pixel, Blinder };

    struct Fixture {
        Kind kind = Kind::Spot;
        glm::vec3 mount{0.0f};    // hanging point (truss) or floor point
        bool hanging = true;
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        float pan = 0.0f, tilt = 0.0f;              // radians, at rest
        float panSwing = 0.0f, tiltSwing = 0.0f;    // animation amplitude
        float speed = 0.0f, phase = 0.0f;           // rad/s, rad
        ImageId gobo = kInvalidImage;
        ImageId gobo2 = kInvalidImage;
        float goboSpin = 0.0f;                      // rad/s
        int prismFacets = 0;
        float prismSpread = 0.0f;                   // radians
        float prismSpin = 0.0f;                     // rad/s
        float frost = 0.0f;
        float zoom = 1.0f;                          // multiplies beam/field angle
        bool blades = false;
    };

    static void aim(Fixture& f, const glm::vec3& target);  // sets pan/tilt so the beam hits target
    void addTruss(assets::AssetLibrary& assets, const glm::vec3& centre, float length, bool vertical);
    void buildShowRig(const std::vector<ImageId>& gobos);
    void buildStressRig(const std::vector<ImageId>& gobos, int beams);
    void buildSingleRig(const std::vector<ImageId>& gobos);
    void emitFixture(const Fixture& f, double time, render::RenderScene& scene) const;

    std::vector<MeshInstance> staticMeshes_;
    std::vector<Fixture> fixtures_;
    MeshId cube_ = kInvalidMesh, cylinder_ = kInvalidMesh, disc_ = kInvalidMesh;
};

}  // namespace dmxviz::sandbox
