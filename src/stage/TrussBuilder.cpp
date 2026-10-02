#include "stage/TrussBuilder.h"

#include "stage/GeometryUtil.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace dmxviz::stage::truss {
namespace {

using assets::MeshData;
using geom::addTube;

constexpr float kArcSubdivisionRad = 7.5f * kPi / 180.0f;

Material steel() { return Material{glm::vec3(0.12f), 0.55f, 0.8f, glm::vec3(0.0f)}; }

std::string profileKey(const TrussProfile& p) {
    return std::format("{}:{}:{}:{}", trussShapeName(p.shape), geom::keyNum(p.width), geom::keyNum(p.chordDiameter),
                       geom::keyNum(p.braceDiameter));
}

// Pairs of chord indices that form a braced face of the profile.
std::vector<std::pair<int, int>> bracedFaces(const TrussProfile& p, int chordCount) {
    std::vector<std::pair<int, int>> faces;
    if (p.shape == TrussShape::Box || p.shape == TrussShape::Triangle) {
        for (int i = 0; i < chordCount; ++i) faces.emplace_back(i, (i + 1) % chordCount);
    } else if (chordCount >= 2) {
        faces.emplace_back(0, 1);
    }
    return faces;
}

// Zig-zag diagonals between two chord lines given as functions of a run
// parameter in [ta, tb], plus perpendicular braces at both ends. Ladder
// profiles get rungs instead of diagonals.
template <typename PointFn>
void addFaceBracing(MeshData& m, const TrussProfile& p, float runLength, float ta, float tb, PointFn&& point,
                    int chordA, int chordB) {
    const float rb = p.braceDiameter * 0.5f;
    const float spacing = std::max(p.chordSpacing(), 0.05f);
    addTube(m, point(ta, chordA), point(ta, chordB), rb, kBraceSides, false);
    addTube(m, point(tb, chordA), point(tb, chordB), rb, kBraceSides, false);
    if (p.shape == TrussShape::Ladder) {
        // Rungs roughly every 1.5 x the chord spacing.
        const int bays = std::max(1, static_cast<int>(std::lround(runLength / (spacing * 1.5f))));
        for (int k = 1; k < bays; ++k) {
            const float t = ta + (tb - ta) * static_cast<float>(k) / static_cast<float>(bays);
            addTube(m, point(t, chordA), point(t, chordB), rb, kBraceSides, false);
        }
        return;
    }
    // Diagonals at about 45 degrees.
    const int steps = std::max(1, static_cast<int>(std::lround(runLength / spacing)));
    for (int k = 0; k < steps; ++k) {
        const float t0 = ta + (tb - ta) * static_cast<float>(k) / static_cast<float>(steps);
        const float t1 = ta + (tb - ta) * static_cast<float>(k + 1) / static_cast<float>(steps);
        if (k % 2 == 0)
            addTube(m, point(t0, chordA), point(t1, chordB), rb, kBraceSides, false);
        else
            addTube(m, point(t0, chordB), point(t1, chordA), rb, kBraceSides, false);
    }
}

// Section point (y, z) of the arc at angle t: z grows outward from the centre.
glm::vec3 arcPoint(float radius, float t, const glm::vec2& section) {
    const float r = radius + section.y;
    return {r * std::cos(t), section.x, -r * std::sin(t)};
}

// Local matrix that stands a run (built along +X) upright along +Y.
glm::mat4 uprightAt(float centreY) {
    return glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, centreY, 0.0f)) *
           glm::rotate(glm::mat4(1.0f), kPi * 0.5f, glm::vec3(0, 0, 1));
}

struct TowerLayout {
    float trussBottom = 0.0f;
    float trussTop = 0.0f;
    float sleeveCentre = 0.0f;
    float topCentre = 0.0f;
};

TowerLayout towerLayout(const TrussContent& t) {
    TowerLayout l;
    const float w = t.profile.width;
    const float height = std::max(t.tower.height, kBasePlateThickness + w + 0.1f);
    l.trussBottom = kBasePlateThickness;
    l.trussTop = height - w;
    l.topCentre = height - w * 0.5f;
    const float lo = l.trussBottom + kSleeveHeight * 0.5f;
    const float hi = std::max(lo, l.trussTop - kSleeveHeight * 0.5f);
    l.sleeveCentre = std::clamp(t.tower.sleeveHeight, lo, hi);
    return l;
}

float arcRadius(const TrussContent& t) { return std::max(t.arc.radius, t.profile.width); }

float arcPieceAngle(const TrussContent& t, int& pieces) {
    const float total = degToRad(std::clamp(t.arc.angleDeg, 1.0f, 360.0f));
    pieces = std::clamp(t.arc.pieces, 1, 64);
    return total / static_cast<float>(pieces);
}

}  // namespace

MeshData buildStraight(const TrussProfile& p, float length) {
    MeshData m;
    const std::vector<glm::vec2> chords = p.chordPositions();
    const float x0 = -length * 0.5f;
    const float x1 = length * 0.5f;
    for (const glm::vec2& c : chords)
        addTube(m, {x0, c.x, c.y}, {x1, c.x, c.y}, p.chordDiameter * 0.5f, kChordSides, true);
    // End braces sit just inside the connectors.
    const float inset = std::min(p.chordDiameter, length * 0.25f);
    auto point = [&](float x, int chord) {
        return glm::vec3(x, chords[static_cast<std::size_t>(chord)].x, chords[static_cast<std::size_t>(chord)].y);
    };
    for (auto [a, b] : bracedFaces(p, static_cast<int>(chords.size())))
        addFaceBracing(m, p, length - 2.0f * inset, x0 + inset, x1 - inset, point, a, b);
    return m;
}

MeshData buildCornerBlock(const TrussProfile& p, std::uint8_t faces) {
    MeshData m;
    const float h = p.chordSpacing() * 0.5f;
    const float rc = p.chordDiameter * 0.5f;
    const float e = h + rc;  // edges overlap at the corners so they look welded
    for (float a : {-h, h}) {
        for (float b : {-h, h}) {
            addTube(m, {-e, a, b}, {e, a, b}, rc, kChordSides, true);  // along X
            addTube(m, {a, -e, b}, {a, e, b}, rc, kChordSides, true);  // along Y
            addTube(m, {a, b, -e}, {a, b, e}, rc, kChordSides, true);  // along Z
        }
    }
    // Closed faces get a diagonal brace; connecting faces stay open.
    const float rb = p.braceDiameter * 0.5f;
    struct Face {
        std::uint8_t bit;
        glm::vec3 from, to;
    };
    const Face faceList[] = {
        {kTrussFacePosX, {h, -h, -h}, {h, h, h}}, {kTrussFaceNegX, {-h, -h, h}, {-h, h, -h}},
        {kTrussFacePosY, {-h, h, -h}, {h, h, h}}, {kTrussFaceNegY, {-h, -h, h}, {h, -h, -h}},
        {kTrussFacePosZ, {-h, -h, h}, {h, h, h}}, {kTrussFaceNegZ, {h, -h, -h}, {-h, h, -h}},
    };
    for (const Face& f : faceList) {
        if ((faces & f.bit) == 0) addTube(m, f.from, f.to, rb, kBraceSides, false);
    }
    return m;
}

MeshData buildArcPiece(const TrussProfile& p, float radius, float angleRad) {
    MeshData m;
    const std::vector<glm::vec2> chords = p.chordPositions();
    const int subdivisions = std::max(2, static_cast<int>(std::ceil(angleRad / kArcSubdivisionRad)));
    for (const glm::vec2& c : chords) {
        for (int i = 0; i < subdivisions; ++i) {
            const float t0 = angleRad * static_cast<float>(i) / static_cast<float>(subdivisions);
            const float t1 = angleRad * static_cast<float>(i + 1) / static_cast<float>(subdivisions);
            addTube(m, arcPoint(radius, t0, c), arcPoint(radius, t1, c), p.chordDiameter * 0.5f, kChordSides, true);
        }
    }
    const float inset = std::min(p.chordDiameter / radius, angleRad * 0.25f);
    auto point = [&](float t, int chord) { return arcPoint(radius, t, chords[static_cast<std::size_t>(chord)]); };
    const float runLength = radius * (angleRad - 2.0f * inset);
    for (auto [a, b] : bracedFaces(p, static_cast<int>(chords.size())))
        addFaceBracing(m, p, runLength, inset, angleRad - inset, point, a, b);
    return m;
}

MeshData buildBasePlate(float size) {
    MeshData m;
    geom::addBox(m, {0.0f, kBasePlateThickness * 0.5f, 0.0f}, {size, kBasePlateThickness, size});
    return m;
}

MeshData buildSleeveBlock(const TrussProfile& p) {
    MeshData m;
    const float hs = p.width * 0.5f + 0.03f;  // clears the tower chords
    const float hh = kSleeveHeight * 0.5f;
    const float r = 0.025f;
    for (float x : {-hs, hs}) {
        for (float z : {-hs, hs}) addTube(m, {x, -hh, z}, {x, hh, z}, r, 8, true);
    }
    for (float y : {-hh + r, hh - r}) {
        for (float s : {-hs, hs}) {
            addTube(m, {-hs, y, s}, {hs, y, s}, r, 8, false);
            addTube(m, {s, y, -hs}, {s, y, hs}, r, 8, false);
        }
    }
    return m;
}

MeshId straightMesh(assets::AssetLibrary& library, const TrussProfile& profile, float length) {
    const std::string key = std::format("stage:truss:straight:{}:L{}", profileKey(profile), geom::keyNum(length));
    return geom::cachedMesh(library, key, [&] { return buildStraight(profile, length); });
}

MeshId cornerMesh(assets::AssetLibrary& library, const TrussProfile& profile, std::uint8_t faces) {
    const std::string key = std::format("stage:truss:corner:{}:F{}", profileKey(profile), static_cast<int>(faces & 63));
    return geom::cachedMesh(library, key, [&] { return buildCornerBlock(profile, faces); });
}

MeshId arcPieceMesh(assets::AssetLibrary& library, const TrussProfile& profile, float radius, float angleRad) {
    const std::string key = std::format("stage:truss:arc:{}:R{}:A{}", profileKey(profile), geom::keyNum(radius),
                                        geom::keyNum(angleRad));
    return geom::cachedMesh(library, key, [&] { return buildArcPiece(profile, radius, angleRad); });
}

MeshId basePlateMesh(assets::AssetLibrary& library, float size) {
    const std::string key = std::format("stage:truss:baseplate:{}", geom::keyNum(size));
    return geom::cachedMesh(library, key, [&] { return buildBasePlate(size); });
}

MeshId sleeveBlockMesh(assets::AssetLibrary& library, const TrussProfile& profile) {
    const std::string key = std::format("stage:truss:sleeve:{}", profileKey(profile));
    return geom::cachedMesh(library, key, [&] { return buildSleeveBlock(profile); });
}

std::vector<float> splitRun(float length, const std::vector<float>& explicitSegments) {
    std::vector<float> out;
    if (!explicitSegments.empty()) {
        for (float s : explicitSegments) {
            if (s > 1e-4f) out.push_back(s);
        }
        if (!out.empty()) return out;
    }
    if (length <= 1e-4f) return out;
    float remaining = length;
    for (float standard : standardTrussLengths()) {
        while (remaining >= standard - 1e-4f) {
            out.push_back(standard);
            remaining -= standard;
        }
    }
    if (remaining > 1e-3f) {
        // Avoid a tiny stub: merge it with the last piece into one custom length.
        if (!out.empty() && remaining < 0.5f) {
            out.back() += remaining;
        } else {
            out.push_back(remaining);
        }
    }
    return out;
}

std::vector<RenderPart> trussParts(const TrussContent& t, assets::AssetLibrary& library) {
    std::vector<RenderPart> parts;
    const Material alu = materials::aluminium();
    switch (t.piece) {
        case TrussPiece::Straight: {
            const std::vector<float> segments = splitRun(t.straight.length, t.straight.segments);
            const float total = std::accumulate(segments.begin(), segments.end(), 0.0f);
            float x = -total * 0.5f;
            for (float s : segments) {
                parts.push_back({straightMesh(library, t.profile, s),
                                 glm::translate(glm::mat4(1.0f), glm::vec3(x + s * 0.5f, 0.0f, 0.0f)), alu});
                x += s;
            }
            break;
        }
        case TrussPiece::Corner:
            parts.push_back({cornerMesh(library, t.profile, t.corner.faces), glm::mat4(1.0f), alu});
            break;
        case TrussPiece::Arc: {
            int pieces = 1;
            const float pieceAngle = arcPieceAngle(t, pieces);
            const MeshId mesh = arcPieceMesh(library, t.profile, arcRadius(t), pieceAngle);
            for (int i = 0; i < pieces; ++i) {
                parts.push_back({mesh,
                                 glm::rotate(glm::mat4(1.0f), pieceAngle * static_cast<float>(i), glm::vec3(0, 1, 0)),
                                 alu});
            }
            break;
        }
        case TrussPiece::Tower: {
            const TowerLayout l = towerLayout(t);
            parts.push_back({basePlateMesh(library, t.tower.basePlateSize), glm::mat4(1.0f), steel()});
            float y = l.trussBottom;
            for (float s : splitRun(l.trussTop - l.trussBottom)) {
                parts.push_back({straightMesh(library, t.profile, s), uprightAt(y + s * 0.5f), alu});
                y += s;
            }
            parts.push_back({cornerMesh(library, t.profile, kTrussFaceNegY),
                             glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, l.topCentre, 0.0f)), alu});
            parts.push_back({sleeveBlockMesh(library, t.profile),
                             glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, l.sleeveCentre, 0.0f)), steel()});
            break;
        }
    }
    return parts;
}

std::vector<ChordLine> trussChords(const TrussContent& t) {
    std::vector<ChordLine> lines;
    const std::vector<glm::vec2> chords = t.profile.chordPositions();
    switch (t.piece) {
        case TrussPiece::Straight: {
            const std::vector<float> segments = splitRun(t.straight.length, t.straight.segments);
            const float half = std::accumulate(segments.begin(), segments.end(), 0.0f) * 0.5f;
            for (const glm::vec2& c : chords) lines.push_back({{-half, c.x, c.y}, {half, c.x, c.y}});
            break;
        }
        case TrussPiece::Corner: {
            const float h = t.profile.chordSpacing() * 0.5f;
            for (float a : {-h, h}) {
                for (float b : {-h, h}) {
                    lines.push_back({{-h, a, b}, {h, a, b}});
                    lines.push_back({{a, -h, b}, {a, h, b}});
                    lines.push_back({{a, b, -h}, {a, b, h}});
                }
            }
            break;
        }
        case TrussPiece::Arc: {
            int pieces = 1;
            const float pieceAngle = arcPieceAngle(t, pieces);
            const float total = pieceAngle * static_cast<float>(pieces);
            const int subdivisions = std::max(2, static_cast<int>(std::ceil(total / kArcSubdivisionRad)));
            for (const glm::vec2& c : chords) {
                for (int i = 0; i < subdivisions; ++i) {
                    const float t0 = total * static_cast<float>(i) / static_cast<float>(subdivisions);
                    const float t1 = total * static_cast<float>(i + 1) / static_cast<float>(subdivisions);
                    lines.push_back({arcPoint(arcRadius(t), t0, c), arcPoint(arcRadius(t), t1, c)});
                }
            }
            break;
        }
        case TrussPiece::Tower: {
            const TowerLayout l = towerLayout(t);
            // uprightAt() maps section y to -x.
            for (const glm::vec2& c : chords) lines.push_back({{-c.x, l.trussBottom, c.y}, {-c.x, l.trussTop, c.y}});
            break;
        }
    }
    return lines;
}

float trussChordRadius(const TrussContent& truss) { return truss.profile.chordDiameter * 0.5f; }

}  // namespace dmxviz::stage::truss
