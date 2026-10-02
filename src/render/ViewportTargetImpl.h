#pragma once
// Everything that belongs to one 3D viewport: its size-dependent render
// targets and the per-frame data buffers (each viewport needs its own, because
// sokol allows one buffer update per frame and every viewport has its own
// camera, culling and therefore its own instance lists).

#include "render/GpuResources.h"
#include "render/Renderer.h"

#include <array>
#include <cstdint>

namespace dmxviz::render {

struct ViewportTarget::Impl {
    int width = 0;
    int height = 0;

    // Full resolution
    RenderTarget gAlbedo;    // RGBA8   albedo + roughness
    RenderTarget gNormal;    // RGBA16F octahedral normal, metallic, highlight
    RenderTarget gDistance;  // R32F    distance from the camera
    RenderTarget hdr;        // RGBA16F lit scene
    DepthTarget depth;       // D32F    hardware depth (tests only, never sampled)
    RenderTarget ldr;        // RGBA8   final image shown by the UI

    // Half resolution
    RenderTarget halfDepth;  // RG32F min/max distance
    RenderTarget volume;     // RGBA16F in-scattered beam light

    // Bloom chain: level 0 = half res, each next level half of the previous.
    std::array<RenderTarget, kBloomLevels> bloom;

    sg_sampler displaySampler{};  // for ImGui::Image

    // Per-frame data
    DynamicBuffer frameConstants;  // FrameGpu
    DynamicBuffer instances;       // InstanceGpu (vertex buffer)
    DynamicBuffer beams;           // BeamGpu (storage)
    DynamicBuffer glows;           // GlowGpu (storage)
    DynamicBuffer lines;           // LineVertexGpu (vertex buffer)

    Impl() {
        frameConstants.init(DynamicBuffer::Kind::Storage, "view-frame");
        instances.init(DynamicBuffer::Kind::Vertex, "view-instances");
        beams.init(DynamicBuffer::Kind::Storage, "view-beams");
        glows.init(DynamicBuffer::Kind::Storage, "view-glows");
        lines.init(DynamicBuffer::Kind::Vertex, "view-lines");
    }

    void createTargets(int w, int h);
    void destroyTargets();
    void destroyAll();
};

}  // namespace dmxviz::render
