#pragma once
// Procedural mesh generators. All meshes are centred on the origin unless noted,
// use the Y-up convention, and have outward-facing counter-clockwise triangles.

#include "assets/MeshData.h"

namespace dmxviz::assets {

MeshData makeBox(const glm::vec3& size);
MeshData makePlane(float width, float depth, int subdivisions = 1);  // in the XZ plane, normal +Y
MeshData makeDisc(float radius, int segments = 32);                  // in the XZ plane, normal +Y
MeshData makeSphere(float radius, int segments = 32, int rings = 16);
// Cylinder along Y from y = -height/2 to +height/2.
MeshData makeCylinder(float radius, float height, int segments = 24, bool capped = true);
// Cone along Y: base disc at y = 0, apex at y = height.
MeshData makeCone(float radius, float height, int segments = 24, bool capped = true);
// Thin cylinder between two points (truss chords and braces, tubes).
MeshData makeTube(const glm::vec3& from, const glm::vec3& to, float radius, int segments = 8);

}  // namespace dmxviz::assets
