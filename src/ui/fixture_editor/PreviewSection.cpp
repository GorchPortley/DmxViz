#include "ui/fixture_editor/PreviewSection.h"

#include "ui/ColorWidgets.h"
#include "ui/EditorContext.h"

#include "imgui.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <functional>

namespace dmxviz::ui::fixture_editor {

using fixtures::Attribute;

namespace {

constexpr double kRebuildDelaySeconds = 0.15;  // wait for the end of a drag before rebuilding
constexpr float kHangHeight = 2.4f;            // m above the floor: the rigging point of the fixture
constexpr int kFaderRows = 7;

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
    return (hash ^ value) * 1099511628211ull;
}

std::uint64_t mixFloat(std::uint64_t hash, float value) {
    return mix(hash, std::bit_cast<std::uint32_t>(value));
}

std::uint64_t mixText(std::uint64_t hash, const std::string& text) {
    return mix(hash, std::hash<std::string>{}(text));
}

// Everything that FixtureAssets::realize() turns into meshes and images. While it is unchanged,
// the assets are reused instead of being created again.
std::uint64_t visualSignature(const fixtures::FixtureType& type) {
    std::uint64_t hash = 1469598103934665603ull;
    fixtures::forEachGeometry(type.geometry, [&](const fixtures::Geometry& g, const fixtures::Geometry*) {
        hash = mixText(hash, g.name);
        hash = mix(hash, static_cast<std::uint64_t>(g.type));
        hash = mix(hash, static_cast<std::uint64_t>(g.model.primitive));
        hash = mixText(hash, g.model.mesh);
        for (int i = 0; i < 3; ++i) {
            hash = mixFloat(hash, g.model.size[i]);
            hash = mixFloat(hash, g.model.color[i]);
        }
    });
    for (const fixtures::Resource& r : type.resources) {
        hash = mixText(hash, r.name);
        hash = mixText(hash, r.format);
        hash = mix(hash, r.data.size());
        const std::size_t n = r.data.size();
        for (std::size_t i = 0; i < std::min<std::size_t>(n, 64); ++i) hash = mix(hash, r.data[i]);
        for (std::size_t i = n > 64 ? n - 64 : n; i < n; ++i) hash = mix(hash, r.data[i]);
    }
    return hash;
}

// First wheel that a control of `attribute` selects slots on (the gobo wheel of Gobo1...).
const fixtures::Wheel* wheelOf(const fixtures::AttributeEncoder& encoder, Attribute attribute) {
    for (const fixtures::AttributeEncoder::Control& control : encoder.controls())
        if (control.attribute == attribute && control.wheel != nullptr) return control.wheel;
    return nullptr;
}

bool hasColourMixing(const fixtures::AttributeEncoder& encoder) {
    for (Attribute a : {Attribute::ColorAdd_R, Attribute::ColorAdd_G, Attribute::ColorAdd_B, Attribute::ColorAdd_W,
                        Attribute::ColorSub_C, Attribute::ColorSub_M, Attribute::ColorSub_Y})
        if (encoder.has(a)) return true;
    return false;
}

}  // namespace

PreviewSection::PreviewSection() {
    resetCamera();
}

void PreviewSection::resetCamera() {
    camera_ = ViewportCamera{};
    camera_.lookFromTo(glm::vec3(2.6f, 1.6f, 3.6f), glm::vec3(0.0f, kHangHeight - 0.9f, 0.0f));
}

void PreviewSection::reset() {
    faders_ = Faders{};
    built_ = false;
    resetCamera();
}

// ---------------------------------------------------------------------------
// Building

void PreviewSection::rebuild(EditorContext& ctx, const fixtures::FixtureType& type, int modeIndex) {
    encoder_.reset();
    runtime_.reset();

    type_ = std::make_shared<fixtures::FixtureType>(type);
    type_->id = "editor/preview";  // its own asset keys: the library's images stay untouched
    if (type_->modes.empty()) return;

    const std::uint64_t signature = visualSignature(*type_);
    if (!assets_ || signature != assetSignature_) {
        assets_ = std::make_unique<fixtures::FixtureAssets>(fixtures::FixtureAssets::realize(*type_, ctx.assets));
        assetSignature_ = signature;
    }

    const int index = std::clamp(modeIndex, 0, static_cast<int>(type_->modes.size()) - 1);
    runtime_ = std::make_unique<fixtures::FixtureRuntime>(type_, type_->modes[static_cast<std::size_t>(index)].name, assets_.get());
    encoder_ = std::make_unique<fixtures::AttributeEncoder>(*type_, runtime_->mode());
    dmx_.assign(static_cast<std::size_t>(std::max(1, runtime_->footprint())), 0);
    applyFaders();

    // Start at the fader position instead of moving there from the home position after every edit.
    runtime_->setDmx(dmx_);
    runtime_->update(0.0f, time_);
    runtime_->snapToTargets();
    runtime_->update(0.0f, time_);
}

void PreviewSection::applyFaders() {
    if (!encoder_) return;
    const fixtures::AttributeEncoder& e = *encoder_;
    e.writeDefaults(dmx_);
    e.setShutter(dmx_, fixtures::FunctionKind::ShutterOpen);  // a fixture that starts closed would show nothing
    e.setNormalized(dmx_, Attribute::Dimmer, faders_.dimmer);
    e.setNormalized(dmx_, Attribute::Pan, faders_.pan);
    e.setNormalized(dmx_, Attribute::Tilt, faders_.tilt);
    e.setNormalized(dmx_, Attribute::Zoom, faders_.zoom);
    if (hasColourMixing(e)) e.setColor(dmx_, srgbToLinear(faders_.colour));
    else e.setWheelSlot(dmx_, Attribute::Color1, static_cast<float>(faders_.colourSlot));
    e.setWheelSlot(dmx_, Attribute::Gobo1, static_cast<float>(faders_.goboSlot));
}

void PreviewSection::buildScene(EditorContext& ctx) {
    scene_.clear();

    MeshInstance floor;
    floor.mesh = ctx.assets.builtin(assets::BuiltinMesh::Plane);
    floor.world = glm::scale(glm::mat4(1.0f), glm::vec3(12.0f, 1.0f, 12.0f));
    floor.material = Material{glm::vec3(0.06f), 0.85f, 0.0f, glm::vec3(0.0f)};
    floor.castsShadow = false;
    scene_.meshes.push_back(floor);

    // A pipe the fixture hangs from, for scale.
    MeshInstance pipe;
    pipe.mesh = ctx.assets.builtin(assets::BuiltinMesh::Cube);
    pipe.world = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, kHangHeight + 0.03f, 0.0f)), glm::vec3(1.6f, 0.05f, 0.05f));
    pipe.material = Material{glm::vec3(0.35f), 0.4f, 0.8f, glm::vec3(0.0f)};
    scene_.meshes.push_back(pipe);

    if (runtime_) runtime_->emit(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, kHangHeight, 0.0f)), kInvalidNode, scene_.meshes, scene_.beams);

    render::Environment& env = scene_.environment;
    env.hazeDensity = 0.5f;
    env.hazeVariation = 0.2f;
    env.ambient = glm::vec3(0.03f);
    env.background = glm::vec3(0.008f, 0.008f, 0.012f);
    env.exposure = 1.0f;
    env.bloomStrength = 0.06f;
    env.beamBrightness = 1.0f;
    env.showGrid = false;
    env.timeSeconds = time_;
}

// ---------------------------------------------------------------------------
// Frame

void PreviewSection::draw(EditorContext& ctx, const fixtures::FixtureType& type, std::uint64_t revision, int modeIndex) {
    ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    const bool stale = built_ && (revision != builtRevision_ || modeIndex != builtMode_);
    const bool modeSwitched = built_ && modeIndex != builtMode_;
    if (!built_ || modeSwitched || (stale && now - lastBuildTime_ >= kRebuildDelaySeconds)) {
        rebuild(ctx, type, modeIndex);
        built_ = true;
        builtRevision_ = revision;
        builtMode_ = modeIndex;
        lastBuildTime_ = now;
    }

    if (!runtime_) {
        ImGui::TextDisabled("Add a DMX mode to see the fixture.");
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float fadersHeight = ImGui::GetFrameHeightWithSpacing() * static_cast<float>(kFaderRows);
    const ImVec2 viewSize(std::max(avail.x, 16.0f), std::max(avail.y - fadersHeight, 100.0f));

    const float dt = std::min(io.DeltaTime, 0.05f);
    time_ += static_cast<double>(dt);
    runtime_->setDmx(dmx_);
    runtime_->update(dt, time_);
    buildScene(ctx);

    const int pixelsW = std::max(1, static_cast<int>(viewSize.x * ctx.dpiScale));
    const int pixelsH = std::max(1, static_cast<int>(viewSize.y * ctx.dpiScale));
    target_.resize(pixelsW, pixelsH);
    const float aspect = static_cast<float>(pixelsW) / static_cast<float>(pixelsH);
    ctx.renderer.render(target_, camera_.camera(aspect), scene_, ctx.assets);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(ImTextureRef(target_.imguiTexture()), viewSize);
    handleViewportCameraInput(camera_, ImGui::IsItemHovered(), viewSize.y, io.DeltaTime);

    char caption[160];
    std::snprintf(caption, sizeof(caption), "Preview, mode \"%s\"  (middle drag: orbit, wheel: zoom)", runtime_->mode().name.c_str());
    ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 8.0f, origin.y + 6.0f), IM_COL32(210, 210, 220, 200), caption);

    if (drawFaders()) applyFaders();
}

bool PreviewSection::drawFaders() {
    bool changed = false;
    const fixtures::AttributeEncoder& e = *encoder_;
    ImGui::PushItemWidth(-90.0f);

    auto fader = [&](const char* label, float& value, Attribute attribute) {
        ImGui::BeginDisabled(!e.has(attribute));
        changed |= ImGui::SliderFloat(label, &value, 0.0f, 1.0f, "%.2f");
        ImGui::EndDisabled();
    };
    fader("Dimmer", faders_.dimmer, Attribute::Dimmer);
    fader("Pan", faders_.pan, Attribute::Pan);
    fader("Tilt", faders_.tilt, Attribute::Tilt);
    fader("Zoom", faders_.zoom, Attribute::Zoom);

    // Colour: a picker when the mode mixes colours, else the colour wheel slots.
    if (hasColourMixing(e) || !e.has(Attribute::Color1)) {
        ImGui::BeginDisabled(!hasColourMixing(e));
        changed |= ImGui::ColorEdit3("Color", &faders_.colour.x, ImGuiColorEditFlags_NoInputs);
        ImGui::EndDisabled();
    } else if (const fixtures::Wheel* wheel = wheelOf(e, Attribute::Color1)) {
        char preview[96];
        const int slots = static_cast<int>(wheel->slots.size());
        faders_.colourSlot = std::clamp(faders_.colourSlot, 1, std::max(1, slots));
        std::snprintf(preview, sizeof(preview), "%d %s", faders_.colourSlot, slots > 0 ? wheel->slots[static_cast<std::size_t>(faders_.colourSlot - 1)].name.c_str() : "");
        if (ImGui::BeginCombo("Color", preview)) {
            for (int s = 1; s <= slots; ++s) {
                char label[96];
                std::snprintf(label, sizeof(label), "%d %s", s, wheel->slots[static_cast<std::size_t>(s - 1)].name.c_str());
                if (ImGui::Selectable(label, s == faders_.colourSlot)) {
                    faders_.colourSlot = s;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::BeginDisabled(!e.has(Attribute::Gobo1));
    if (const fixtures::Wheel* wheel = wheelOf(e, Attribute::Gobo1)) {
        char preview[96];
        const int slots = static_cast<int>(wheel->slots.size());
        faders_.goboSlot = std::clamp(faders_.goboSlot, 1, std::max(1, slots));
        std::snprintf(preview, sizeof(preview), "%d %s", faders_.goboSlot, slots > 0 ? wheel->slots[static_cast<std::size_t>(faders_.goboSlot - 1)].name.c_str() : "");
        if (ImGui::BeginCombo("Gobo", preview)) {
            for (int s = 1; s <= slots; ++s) {
                char label[96];
                std::snprintf(label, sizeof(label), "%d %s", s, wheel->slots[static_cast<std::size_t>(s - 1)].name.c_str());
                if (ImGui::Selectable(label, s == faders_.goboSlot)) {
                    faders_.goboSlot = s;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    } else {
        int slot = faders_.goboSlot;
        if (ImGui::SliderInt("Gobo", &slot, 1, 8)) {
            faders_.goboSlot = slot;
            changed = true;
        }
    }
    ImGui::EndDisabled();

    ImGui::PopItemWidth();
    ImGui::TextDisabled("Faders use the attributes of the selected mode.");
    return changed;
}

}  // namespace dmxviz::ui::fixture_editor
