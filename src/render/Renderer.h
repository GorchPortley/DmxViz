#pragma once
// The 3D renderer: stage geometry, fixture bodies, surface lighting from every
// beam (with gobos), volumetric haze beams, lens glow, bloom and tonemapping.
//
// OWNER: render work stream (WS3). See docs/ARCHITECTURE.md "Rendering".
//
// Threading: main thread only, after sg_setup(). Call render() outside any
// sokol pass; it records its own offscreen passes.

#include "render/RenderScene.h"

#include <cstdint>
#include <memory>

namespace dmxviz::assets {
class AssetLibrary;
}

namespace dmxviz::render {

// The offscreen images for one 3D viewport. The UI shows colorTexture() with
// ImGui::Image(). Resizing is cheap to request every frame: it only reallocates
// when the size actually changes.
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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dmxviz::render
