#pragma once
// Ready-made NodeData for the stage builder toolbar. Each function returns
// the data of one new node with a descriptive name; add it with
//   stack.execute(std::make_unique<AddNodeCommand>(factory::trussStraight(f34, 3.0f), transform));

#include "stage/Node.h"

#include <string>

namespace dmxviz::stage::factory {

NodeData group(std::string name = "Group");
NodeData primitive(PrimitiveShape shape, const glm::vec3& size = glm::vec3(1.0f));
// A dark floor plane (primitive plane) of the given size, lying at y = 0.
NodeData floor(float width = 20.0f, float depth = 15.0f);
NodeData model(std::string path, bool zUp = false, float unitScale = 1.0f);

NodeData trussStraight(const TrussProfile& profile, float length);
NodeData trussCorner(const TrussProfile& profile, TrussCornerPreset preset);
NodeData trussArc(const TrussProfile& profile, float radius, float angleDeg, int pieces);
NodeData trussCircle(const TrussProfile& profile, float radius, int pieces = 4);
NodeData trussTower(const TrussProfile& profile, float height, float sleeveHeight);

NodeData stageDeck(int columns, int rows, float height, const glm::vec2& panelSize = {2.0f, 1.0f});
NodeData riser(int columns, int rows, float height, const glm::vec2& panelSize = {2.0f, 1.0f});
NodeData steps(float width, float height);
NodeData wall(float width, float height, float thickness = 0.1f);
NodeData flat(float width, float height);

NodeData fixture(std::string fixtureTypeId, std::string modeName, DmxPatch patch = {}, int fixtureNumber = 0);
NodeData cameraPreset(std::string name, float fovYDeg = 50.0f);
NodeData referenceFigure(float height = 1.8f);

}  // namespace dmxviz::stage::factory
