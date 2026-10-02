#include "DemoStage.h"

#include "ProceduralGobos.h"
#include "assets/AssetLibrary.h"
#include "assets/Primitives.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace dmxviz::sandbox {
namespace {

using render::RenderScene;

// Saturated linear-RGB show colours.
const glm::vec3 kRed{1.0f, 0.03f, 0.02f};
const glm::vec3 kBlue{0.03f, 0.12f, 1.0f};
const glm::vec3 kMagenta{1.0f, 0.04f, 0.55f};
const glm::vec3 kCyan{0.0f, 0.65f, 1.0f};
const glm::vec3 kGreen{0.08f, 1.0f, 0.12f};
const glm::vec3 kAmber{1.0f, 0.42f, 0.04f};
const glm::vec3 kWhite{1.0f, 0.95f, 0.88f};
const glm::vec3 kViolet{0.38f, 0.04f, 1.0f};
const glm::vec3 kWarm{1.0f, 0.62f, 0.3f};

Material material(const glm::vec3& albedo, float roughness, float metallic = 0.0f,
                  const glm::vec3& emissive = glm::vec3(0.0f)) {
    Material m;
    m.albedo = albedo;
    m.roughness = roughness;
    m.metallic = metallic;
    m.emissive = emissive;
    return m;
}

MeshInstance instance(MeshId mesh, const glm::mat4& world, const Material& m) {
    MeshInstance i;
    i.mesh = mesh;
    i.world = world;
    i.material = m;
    return i;
}

glm::mat4 boxAt(const glm::vec3& centre, const glm::vec3& size) {
    return glm::scale(glm::translate(glm::mat4(1.0f), centre), size);
}

// Rotation whose local +Y axis points along `axis` and local +Z along `up`.
glm::mat4 orient(const glm::vec3& origin, const glm::vec3& axis, const glm::vec3& up, const glm::vec3& scale) {
    const glm::vec3 y = glm::normalize(axis);
    const glm::vec3 z = glm::normalize(up - y * glm::dot(up, y));
    const glm::vec3 x = glm::cross(y, z);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x * scale.x, 0.0f);
    m[1] = glm::vec4(y * scale.y, 0.0f);
    m[2] = glm::vec4(z * scale.z, 0.0f);
    m[3] = glm::vec4(origin, 1.0f);
    return m;
}

// A box truss along X (0.29 m square): four chords plus zig-zag braces.
assets::MeshData makeBoxTruss(float length) {
    assets::MeshData truss;
    const float h = 0.145f;
    const glm::vec2 corners[4] = {{-h, -h}, {h, -h}, {h, h}, {-h, h}};
    const float x0 = -0.5f * length, x1 = 0.5f * length;
    for (const glm::vec2& c : corners) truss.append(assets::makeTube({x0, c.x, c.y}, {x1, c.x, c.y}, 0.024f, 8));
    const int bays = std::max(1, static_cast<int>(length / 0.5f));
    for (int face = 0; face < 4; ++face) {
        const glm::vec2 a = corners[face];
        const glm::vec2 b = corners[(face + 1) % 4];
        for (int i = 0; i < bays; ++i) {
            const float xa = x0 + length * i / bays;
            const float xb = x0 + length * (i + 1) / bays;
            const bool flip = (i % 2) == 0;
            const glm::vec2 p = flip ? a : b;
            const glm::vec2 q = flip ? b : a;
            truss.append(assets::makeTube({xa, p.x, p.y}, {xb, q.x, q.y}, 0.01f, 6));
        }
    }
    truss.computeBounds();
    return truss;
}

}  // namespace

void DemoStage::build(assets::AssetLibrary& assets, Rig rig, int stressBeams) {
    cube_ = assets.builtin(assets::BuiltinMesh::Cube);
    cylinder_ = assets.builtin(assets::BuiltinMesh::Cylinder);
    disc_ = assets.builtin(assets::BuiltinMesh::Disc);
    const MeshId plane = assets.builtin(assets::BuiltinMesh::Plane);

    std::vector<ImageId> gobos;
    const GoboPattern patterns[] = {GoboPattern::Dots, GoboPattern::Breakup, GoboPattern::Star,
                                    GoboPattern::Lines, GoboPattern::Ring,    GoboPattern::Glass};
    const char* names[] = {"dots", "breakup", "star", "lines", "ring", "glass"};
    for (int i = 0; i < 6; ++i)
        gobos.push_back(assets.addImage(makeGobo(patterns[i]), std::string("sandbox:gobo-") + names[i]));

    // --- static set ---------------------------------------------------------------
    staticMeshes_.clear();
    // Glossy black stage floor: beams and lenses reflect in it.
    staticMeshes_.push_back(instance(plane, boxAt({0, 0, 0}, {80, 1, 80}), material(glm::vec3(0.035f), 0.32f)));
    // Cyc / back wall: light grey, catches gobos and the LED bar's glow.
    staticMeshes_.push_back(instance(cube_, boxAt({0, 5.5f, -9.6f}, {26, 11, 0.2f}), material(glm::vec3(0.5f), 0.9f)));
    // Drum riser and side speaker stacks.
    staticMeshes_.push_back(instance(cube_, boxAt({0, 0.3f, -5.0f}, {3.2f, 0.6f, 2.4f}), material(glm::vec3(0.07f), 0.6f)));
    for (float side : {-1.0f, 1.0f}) {
        staticMeshes_.push_back(
            instance(cube_, boxAt({side * 8.8f, 1.3f, -1.5f}, {1.2f, 2.6f, 1.0f}), material(glm::vec3(0.03f), 0.5f)));
    }

    if (rig == Rig::Stress) {
        buildStressRig(gobos, std::max(stressBeams, 1));
    } else if (rig == Rig::Single) {
        addTruss(assets, {0, 6.0f, -2.0f}, 6.0f, false);
        buildSingleRig(gobos);
    } else {
        addTruss(assets, {0, 8.0f, -7.5f}, 18.0f, false);
        addTruss(assets, {0, 8.5f, -3.5f}, 18.0f, false);
        addTruss(assets, {0, 8.0f, 1.5f}, 18.0f, false);
        addTruss(assets, {-10.0f, 4.0f, 1.5f}, 8.0f, true);
        addTruss(assets, {10.0f, 4.0f, 1.5f}, 8.0f, true);
        buildShowRig(gobos);
    }
}

void DemoStage::addTruss(assets::AssetLibrary& assets, const glm::vec3& centre, float length, bool vertical) {
    const MeshId mesh = assets.addMesh(makeBoxTruss(length), "sandbox:truss-" + std::to_string(length));
    glm::mat4 m = glm::translate(glm::mat4(1.0f), centre);
    if (vertical) m = glm::rotate(m, 0.5f * kPi, glm::vec3(0, 0, 1));
    staticMeshes_.push_back(instance(mesh, m, material(glm::vec3(0.62f), 0.35f, 1.0f)));
}

void DemoStage::aim(Fixture& f, const glm::vec3& target) {
    const glm::vec3 head = f.mount + glm::vec3(0.0f, f.hanging ? -0.45f : 0.45f, 0.0f);
    const glm::vec3 d = glm::normalize(target - head);
    f.tilt = std::acos(std::clamp(f.hanging ? -d.y : d.y, -1.0f, 1.0f));
    f.pan = std::atan2(d.x, d.z);
}

void DemoStage::buildShowRig(const std::vector<ImageId>& gobos) {
    fixtures_.clear();
    const ImageId dots = gobos[0], breakup = gobos[1], star = gobos[2], lines = gobos[3], ring = gobos[4],
                  glass = gobos[5];

    // Upstage truss: gobo spots. The outer pairs reach over the audience, the
    // inner four throw gobos onto the stage floor, two of them through a prism.
    const glm::vec3 spotColors[8] = {kBlue, kMagenta, kCyan, kAmber, kAmber, kCyan, kMagenta, kBlue};
    const ImageId spotGobos[8] = {breakup, dots, star, dots, breakup, star, dots, breakup};
    for (int i = 0; i < 8; ++i) {
        Fixture f;
        f.kind = Kind::Spot;
        const float x = -7.0f + 2.0f * i;
        f.mount = {x, 8.0f - 0.18f, -7.5f};
        f.color = spotColors[i];
        f.gobo = spotGobos[i];
        f.goboSpin = (i % 2 == 0) ? 0.5f : -0.35f;
        f.panSwing = 0.03f;
        f.speed = 0.4f;
        f.phase = 0.7f * i;
        const float side = x < 0.0f ? -1.0f : 1.0f;
        if (i == 0 || i == 7) {
            aim(f, {side * 9.0f, 2.5f, 18.0f});
        } else if (i == 1 || i == 6) {
            aim(f, {side * 6.0f, 4.0f, 20.0f});
        } else {
            aim(f, {x * 0.9f, 0.0f, 2.5f + 0.8f * static_cast<float>(i % 2)});
            if (i == 2 || i == 5) {
                f.prismFacets = 3;
                f.prismSpread = degToRad(5.0f);
                f.prismSpin = 0.3f;
            }
        }
        if (i == 3) f.gobo2 = glass;  // colour glass on top of the dots
        fixtures_.push_back(f);
    }
    // Midstage truss: narrow beam fixtures sweeping a fan over the audience.
    const glm::vec3 beamColors[8] = {kWhite, kRed, kCyan, kGreen, kGreen, kCyan, kRed, kWhite};
    for (int i = 0; i < 8; ++i) {
        Fixture f;
        f.kind = Kind::Beam;
        const float x = -7.0f + 2.0f * i;
        f.mount = {x, 8.5f - 0.18f, -3.5f};
        f.color = beamColors[i];
        aim(f, {x * 2.4f, 0.5f, 13.0f});
        f.panSwing = 0.25f;
        f.tiltSwing = 0.12f;
        f.speed = 0.55f;
        f.phase = (x < 0 ? 0.0f : kPi) + 0.25f * i;
        if (i == 3) {
            f.prismFacets = 8;
            f.prismSpread = degToRad(6.0f);
            f.prismSpin = 0.5f;
        }
        fixtures_.push_back(f);
    }
    // Front truss: two spots projecting gobos on the cyc, four washes on the stage.
    for (int i = 0; i < 6; ++i) {
        Fixture f;
        const float x = -6.25f + 2.5f * i;
        f.mount = {x, 8.0f - 0.18f, 1.5f};
        if (i == 1 || i == 4) {
            f.kind = Kind::Spot;
            f.color = i == 1 ? kBlue : kViolet;
            f.gobo = i == 1 ? breakup : ring;
            f.goboSpin = i == 1 ? 0.15f : -0.2f;
            f.zoom = 1.6f;
            aim(f, {x * 0.8f, 5.0f, -9.5f});
        } else {
            f.kind = Kind::Wash;
            f.color = kWarm;
            f.intensity = 0.5f;
            aim(f, {x * 0.7f, 0.0f, -3.5f});
        }
        fixtures_.push_back(f);
    }
    (void)lines;
    // Floor uplights in front of the cyc: a fan of beams into the roof.
    for (int i = 0; i < 6; ++i) {
        Fixture f;
        f.kind = Kind::Beam;
        f.hanging = false;
        const float x = -6.0f + 2.4f * i;
        f.mount = {x, 0.0f, -8.4f};
        f.color = (i % 2 == 0) ? kMagenta : kBlue;
        aim(f, {x * 2.2f, 25.0f, -1.0f});
        f.tiltSwing = 0.1f;
        f.speed = 0.35f;
        f.phase = 0.9f * i;
        f.zoom = 1.6f;
        fixtures_.push_back(f);
    }
    // LED pixel bar at the foot of the cyc: emitters only, lighting the wall.
    for (int i = 0; i < 16; ++i) {
        Fixture f;
        f.kind = Kind::Pixel;
        f.hanging = false;
        f.mount = {-7.5f + i, 0.12f, -9.1f};
        const float h = static_cast<float>(i) / 16.0f * 6.2831853f;
        f.color = glm::clamp(glm::vec3(std::cos(h), std::cos(h - 2.094f), std::cos(h + 2.094f)) * 0.5f + 0.5f,
                             0.0f, 1.0f);
        f.color = f.color * f.color;  // more saturated
        f.color /= std::max(f.color.r, std::max(f.color.g, f.color.b));
        f.pan = kPi;
        f.tilt = 0.35f;
        fixtures_.push_back(f);
    }
    // Blinders on the towers, facing the audience.
    for (float side : {-1.0f, 1.0f}) {
        for (int k = 0; k < 2; ++k) {
            Fixture f;
            f.kind = Kind::Blinder;
            f.hanging = false;
            f.mount = {side * 9.6f, 5.6f + 0.35f * k, 1.9f};
            f.color = kWarm;
            f.intensity = 0.35f;
            f.pan = -side * 0.25f;
            f.tilt = 1.45f;
            fixtures_.push_back(f);
        }
    }
}

void DemoStage::buildSingleRig(const std::vector<ImageId>& gobos) {
    fixtures_.clear();
    const ImageId patterns[2] = {gobos[1], gobos[0]};  // breakup, dots
    const glm::vec3 colors[2] = {kCyan, kAmber};
    for (int i = 0; i < 2; ++i) {
        Fixture f;
        f.kind = Kind::Spot;
        f.mount = {-1.5f + 3.0f * i, 6.0f - 0.18f, -2.0f};
        f.color = colors[i];
        f.pan = 0.0f;
        f.tilt = 0.45f;
        f.gobo = patterns[i];
        fixtures_.push_back(f);
    }
}

void DemoStage::buildStressRig(const std::vector<ImageId>& gobos, int beams) {
    fixtures_.clear();
    const glm::vec3 palette[8] = {kBlue, kMagenta, kCyan, kAmber, kRed, kGreen, kWhite, kViolet};
    const int rows = 10;
    const int perRow = (beams + rows - 1) / rows;
    for (int i = 0; i < beams; ++i) {
        const int row = i / perRow;
        const int col = i % perRow;
        Fixture f;
        f.kind = (i % 3 == 0) ? Kind::Beam : Kind::Spot;
        const float x = -12.0f + 24.0f * (static_cast<float>(col) + 0.5f) / static_cast<float>(perRow);
        f.mount = {x, 8.5f + 0.2f * (row % 2), -8.0f + 1.3f * row};
        f.color = palette[(row + col) % 8];
        f.pan = -0.03f * x;
        f.tilt = 0.5f + 0.04f * static_cast<float>(row);
        f.panSwing = 0.3f;
        f.tiltSwing = 0.1f;
        f.speed = 0.5f;
        f.phase = 0.37f * i;
        if (f.kind == Kind::Spot) f.gobo = gobos[static_cast<std::size_t>(i % gobos.size())];
        f.goboSpin = 0.5f;
        if (i % 7 == 0) {
            f.prismFacets = 3;
            f.prismSpread = degToRad(4.0f);
            f.prismSpin = 0.4f;
        }
        fixtures_.push_back(f);
    }
}

void DemoStage::update(double timeSeconds, RenderScene& scene) const {
    scene.meshes.insert(scene.meshes.end(), staticMeshes_.begin(), staticMeshes_.end());
    for (const Fixture& f : fixtures_) emitFixture(f, timeSeconds, scene);
}

void DemoStage::emitFixture(const Fixture& f, double time, RenderScene& scene) const {
    const float t = static_cast<float>(time);
    const float pan = f.pan + f.panSwing * std::sin(f.speed * t + f.phase);
    const float tilt = f.tilt + f.tiltSwing * std::cos(0.7f * f.speed * t + f.phase);

    // Beam direction from pan/tilt: hanging fixtures point down at rest, standing ones up.
    const float sp = std::sin(pan), cp = std::cos(pan), st = std::sin(tilt), ct = std::cos(tilt);
    const float vertical = f.hanging ? -1.0f : 1.0f;
    const glm::vec3 dir{sp * st, vertical * ct, cp * st};
    const glm::vec3 up{-vertical * sp * ct, st, -vertical * cp * ct};

    BeamState b;
    b.direction = glm::normalize(dir);
    b.up = glm::normalize(up);
    b.color = f.color;
    b.intensity = f.intensity;
    b.frost = f.frost;
    b.owner = static_cast<NodeId>(scene.beams.size() + 1);

    const Material body = material(glm::vec3(0.04f), 0.45f);
    const glm::vec3 down{0.0f, vertical, 0.0f};

    if (f.kind == Kind::Pixel) {
        b.shape = BeamShape::Glow;
        b.luminousFlux = 300.0f;
        b.beamAngle = degToRad(100.0f);
        b.fieldAngle = degToRad(130.0f);
        b.lensRadius = 0.04f;
        b.position = f.mount + glm::vec3(0, 0.035f, 0);
        b.castsVolume = false;
        scene.meshes.push_back(instance(cube_, boxAt(f.mount, {0.12f, 0.07f, 0.12f}),
                                        material(glm::vec3(0.1f), 0.4f, 0.0f, f.color * 6.0f * f.intensity)));
        scene.beams.push_back(b);
        return;
    }
    if (f.kind == Kind::Blinder) {
        b.shape = BeamShape::Rectangle;
        b.luminousFlux = 2500.0f;
        b.beamAngle = degToRad(45.0f);
        b.fieldAngle = degToRad(70.0f);
        b.emitterSize = {0.22f, 0.22f};
        b.position = f.mount + b.direction * 0.06f;
        scene.meshes.push_back(instance(cube_, orient(f.mount, b.direction, b.up, {0.3f, 0.1f, 0.3f}), body));
        scene.meshes.push_back(instance(disc_, orient(b.position, b.direction, b.up, {0.24f, 1.0f, 0.24f}),
                                        material(glm::vec3(0.2f), 0.3f, 0.0f, f.color * 3.0f * f.intensity)));
        scene.beams.push_back(b);
        return;
    }

    // Moving heads.
    float headRadius = 0.13f;
    switch (f.kind) {
        case Kind::Spot:
            b.shape = BeamShape::Spot;
            b.luminousFlux = 20000.0f;
            b.beamAngle = degToRad(15.0f);
            b.fieldAngle = degToRad(19.0f);
            b.lensRadius = 0.065f;
            b.focus = 0.92f;
            break;
        case Kind::Beam:
            b.shape = BeamShape::Beam;
            b.luminousFlux = 7000.0f;
            b.beamAngle = degToRad(2.2f);
            b.fieldAngle = degToRad(3.0f);
            b.lensRadius = 0.075f;
            break;
        default:
            b.shape = BeamShape::Wash;
            b.luminousFlux = 14000.0f;
            b.beamAngle = degToRad(22.0f);
            b.fieldAngle = degToRad(40.0f);
            b.lensRadius = 0.11f;
            headRadius = 0.17f;
            break;
    }
    b.beamAngle *= f.zoom;
    b.fieldAngle *= f.zoom;
    b.gobos[0] = {f.gobo, f.goboSpin * t};
    b.gobos[1] = {f.gobo2, 0.0f};
    if (f.prismFacets > 0) {
        b.prism.facetCount = static_cast<std::uint8_t>(f.prismFacets);
        for (int i = 0; i < f.prismFacets; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(f.prismFacets) + 0.5f * kPi;
            b.prism.facets[static_cast<std::size_t>(i)] = glm::vec2(std::cos(a), std::sin(a)) * f.prismSpread;
        }
        b.prism.rotation = f.prismSpin * t;
    }

    const glm::vec3 headCentre = f.mount + down * 0.45f;
    b.position = headCentre + b.direction * 0.19f;

    // Base, yoke arms (turn with pan), head along the beam, emissive lens.
    scene.meshes.push_back(instance(cube_, boxAt(f.mount + down * 0.07f, {0.42f, 0.14f, 0.32f}), body));
    const glm::vec3 yokeSide{cp, 0.0f, -sp};
    for (float s : {-1.0f, 1.0f}) {
        scene.meshes.push_back(instance(
            cube_, orient(f.mount + down * 0.3f + yokeSide * (s * (headRadius + 0.05f)), down, {sp, 0, cp}, {0.05f, 0.34f, 0.12f}),
            body));
    }
    scene.meshes.push_back(instance(cylinder_, orient(headCentre, b.direction, b.up, {2 * headRadius, 0.36f, 2 * headRadius}),
                                    body));
    scene.meshes.push_back(instance(disc_, orient(b.position - b.direction * 0.005f, b.direction, b.up,
                                                  {2.0f * b.lensRadius, 1.0f, 2.0f * b.lensRadius}),
                                    material(glm::vec3(0.1f), 0.2f, 0.0f, f.color * 2.0f * f.intensity)));
    scene.beams.push_back(b);
}

}  // namespace dmxviz::sandbox
