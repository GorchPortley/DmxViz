#pragma once
// Passes that produce the lit surfaces:
//   1. G-buffer: all meshes, instanced per mesh (gbuffer.glsl)
//   2. Spot lighting: every beam lights the G-buffer, one instanced draw (spot_light.glsl)

#include "render/HullMesh.h"
#include "render/MeshCache.h"
#include "render/PassContext.h"

#include <span>

namespace dmxviz::render {

// A run of instances (in the per-view instance buffer) that all use one mesh.
struct MeshBatch {
    const MeshCache::Entry* mesh = nullptr;
    int firstInstance = 0;
    int instanceCount = 0;
};

class SurfacePasses {
public:
    bool init();
    void shutdown();

    void drawGBuffer(PassContext& ctx, const MeshCache& meshes, std::span<const MeshBatch> batches);
    void drawSpotLights(PassContext& ctx, const HullMesh& hull, int beamCount, sg_view goboAtlas);

private:
    sg_shader gbufferShader_{};
    sg_pipeline gbufferPipeline_{};
    sg_shader spotShader_{};
    sg_pipeline spotPipeline_{};
};

}  // namespace dmxviz::render
