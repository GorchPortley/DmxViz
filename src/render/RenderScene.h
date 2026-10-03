#pragma once
// Everything the renderer needs to draw one view of one frame. Built by the
// application each frame from the stage + fixture simulation; the renderer
// never touches the scene graph directly.
//
// OWNER: render work stream (WS3). Additive changes are fine; changes to
// existing fields must be coordinated (see CLAUDE.md).

#include "core/SceneTypes.h"

#include <vector>

namespace dmxviz::render {

struct Camera {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};  // OpenGL clip conventions (z in [-1, 1])
    glm::vec3 position{0.0f};
    float nearPlane = 0.05f;
    float farPlane = 500.0f;
    float fovY = degToRad(50.0f);  // radians, vertical
};

struct Environment {
    float hazeDensity = 0.25f;          // 0 = clean air, 1 = thick haze
    float hazeVariation = 0.3f;         // 0..1 amount of slowly drifting haze noise
    glm::vec3 ambient{0.02f};           // linear RGB ambient light on surfaces
    glm::vec3 background{0.0f};         // linear RGB clear colour
    float exposure = 1.0f;              // linear multiplier before tonemapping
    float bloomStrength = 0.05f;
    float beamBrightness = 1.0f;        // artistic multiplier on volumetric beams
    bool showGrid = true;
    double timeSeconds = 0.0;           // for animated haze noise
};

struct DebugLine {
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
    glm::vec4 color{1.0f};  // linear RGBA, drawn unlit on top of the scene
};

struct RenderScene {
    std::vector<MeshInstance> meshes;
    std::vector<BeamState> beams;
    std::vector<DebugLine> lines;
    Environment environment;

    void clear() {
        meshes.clear();
        beams.clear();
        lines.clear();
    }
};

struct RenderStats {
    int drawCalls = 0;
    int meshInstances = 0;
    int beams = 0;
    int beamInstances = 0;  // beams after prism facet expansion
    float gpuMs = 0.0f;     // 0 when unavailable
    // Haze pass (see RenderSettings): what it was asked to do this frame.
    int volumetricDivisor = 2;               // volume target resolution divisor (2 = half, 4 = quarter)
    float volumetricMaxSteps = 0.0f;         // march step cap after the cost budget
    float volumetricCoverageMPixels = 0.0f;  // estimated millions of volume pixels covered by haze beams
    int qualityLevel = 0;                    // automatic quality level in use (0 = as configured)
};

}  // namespace dmxviz::render
