#pragma once
// The 3D renderer: stage geometry, fixture bodies, surface lighting from every
// beam (with gobos), volumetric haze beams, lens glow, bloom and tonemapping.
//
// OWNER: render work stream (WS3). See docs/ARCHITECTURE.md "Rendering" and
// src/render/README.md for the pass list, formats and costs.
//
// Threading: main thread only, after sg_setup(). Call render() outside any
// sokol pass; it records its own offscreen passes. Render each viewport at most
// once per frame (its per-frame buffers can be updated only once).

#include "render/RenderScene.h"
#include "render/RenderSettings.h"

#include <cstdint>
#include <memory>

namespace dmxviz::assets {
class AssetLibrary;
}

namespace dmxviz::render {

// The offscreen images for one 3D viewport. The UI shows imguiTexture() with
// ImGui::Image() (default uv0/uv1: the image is stored top row first). Resizing
// is cheap to request every frame: it only reallocates when the size changes.
class ViewportTarget {
public:
    ViewportTarget();
    ~ViewportTarget();
    ViewportTarget(const ViewportTarget&) = delete;
    ViewportTarget& operator=(const ViewportTarget&) = delete;

    void resize(int width, int height);
    int width() const;
    int height() const;
    // Value for ImGui::Image (an ImTextureID produced by sokol_imgui).
    std::uint64_t imguiTexture() const;

    struct Impl;
    Impl& impl() { return *impl_; }

private:
    std::unique_ptr<Impl> impl_;
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init();
    void shutdown();

    void render(ViewportTarget& target, const Camera& camera, const RenderScene& scene,
                const assets::AssetLibrary& assets);

    const RenderStats& stats() const;

    // Quality/performance tunables; may be changed between frames.
    RenderSettings& settings();
    const RenderSettings& settings() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dmxviz::render
