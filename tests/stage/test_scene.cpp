#include "stage/Scene.h"
#include "stage/Selection.h"

#include <doctest/doctest.h>

using namespace dmxviz;
using namespace dmxviz::stage;

namespace {

Transform moved(float x, float y, float z) {
    Transform t;
    t.position = {x, y, z};
    return t;
}

glm::vec3 worldPos(const Scene& s, NodeId id) { return glm::vec3(s.worldMatrix(id)[3]); }

void checkVec(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    CHECK(a.x == doctest::Approx(b.x).epsilon(eps));
    CHECK(a.y == doctest::Approx(b.y).epsilon(eps));
    CHECK(a.z == doctest::Approx(b.z).epsilon(eps));
}

}  // namespace

TEST_CASE("Transform: matrix, decomposition and Euler degrees") {
    Transform t;
    t.position = {1, 2, 3};
    t.setEulerDegrees({10, 20, 30});
    t.scale = {2, 3, 4};
    const Transform back = Transform::fromMatrix(t.matrix());
    checkVec(back.position, t.position);
    checkVec(back.scale, t.scale);
    checkVec(back.eulerDegrees(), {10, 20, 30});

    // R = Rz * Ry * Rx: rotating +X by (0, 90, 0) points it to -Z.
    Transform y90;
    y90.setEulerDegrees({0, 90, 0});
    checkVec(glm::vec3(y90.matrix() * glm::vec4(1, 0, 0, 0)), {0, 0, -1});
    // Gimbal lock still round-trips the orientation.
    Transform g;
    g.setEulerDegrees({30, 90, 0});
    Transform g2;
    g2.setEulerDegrees(g.eulerDegrees());
    const glm::vec3 p{0.3f, 0.5f, 0.7f};
    checkVec(glm::vec3(g.matrix() * glm::vec4(p, 1)), glm::vec3(g2.matrix() * glm::vec4(p, 1)));

    // Mirroring matrices keep their handedness.
    const Transform mirrored = Transform::fromMatrix(glm::scale(glm::mat4(1.0f), {-1, 1, 1}));
    CHECK(mirrored.scale.x == doctest::Approx(-1.0f));

    const Transform cam = Transform::lookAt({0, 0, 10}, {0, 0, 0});
    checkVec(glm::vec3(cam.matrix() * glm::vec4(0, 0, -1, 0)), {0, 0, -1});
    const Transform down = Transform::lookAt({0, 10, 0}, {0, 0, 0});
    checkVec(glm::vec3(down.matrix() * glm::vec4(0, 0, -1, 0)), {0, -1, 0});
}

TEST_CASE("Scene: hierarchy, ids and world transforms with dirty propagation") {
    Scene s;
    const NodeId a = s.addNode(makeNodeData(NodeKind::Group, "A"), moved(1, 0, 0));
    Transform rot = moved(0, 2, 0);
    rot.setEulerDegrees({0, 90, 0});
    const NodeId b = s.addNode(makeNodeData(NodeKind::Group, "B"), rot, a);
    const NodeId c = s.addNode(makeNodeData(NodeKind::Primitive, "C"), moved(1, 0, 0), b);
    CHECK(a != b);
    CHECK(s.nodeCount() == 3);
    CHECK(s.roots() == std::vector<NodeId>{a});
    CHECK(s.childrenOf(a) == std::vector<NodeId>{b});
    CHECK(s.find(c)->parent() == b);
    CHECK(s.isAncestor(a, c));
    CHECK_FALSE(s.isAncestor(c, a));
    CHECK(s.depthFirst() == std::vector<NodeId>{a, b, c});
    CHECK(s.findByName("C") == c);
    CHECK(s.nodesOfKind(NodeKind::Primitive) == std::vector<NodeId>{c});

    // C is 1 m along B's local X, which points to world -Z.
    checkVec(worldPos(s, c), {1, 2, -1});
    s.setTransform(a, moved(5, 0, 0));
    checkVec(worldPos(s, c), {5, 2, -1});  // cached child matrix was invalidated
    s.setTransform(b, moved(0, 0, 0));
    checkVec(worldPos(s, c), {6, 0, 0});

    // localFromWorld puts a node at a world pose under any parent.
    const Transform local = s.localFromWorld(b, glm::translate(glm::mat4(1.0f), {0, 0, 0}));
    checkVec(local.position, {-5, 0, 0});

    // Cycles are refused.
    CHECK_FALSE(s.setParent(a, c));
    CHECK_FALSE(s.setParent(a, a));
    CHECK(s.setParent(c, kInvalidNode, 0));
    CHECK(s.roots() == std::vector<NodeId>{c, a});

    // Ids are never reused, even after removal.
    const NodeId before = s.nextId();
    CHECK(s.removeNode(a));
    CHECK_FALSE(s.contains(b));
    const NodeId d = s.addNode(makeNodeData(NodeKind::Group));
    CHECK(d == before);
    CHECK(d > b);
}

TEST_CASE("Scene: snapshots, topLevelOnly and tree order") {
    Scene s;
    const NodeId p = s.addNode(makeNodeData(NodeKind::Group, "P"));
    const NodeId x = s.addNode(makeNodeData(NodeKind::Primitive, "X"), {}, p);
    const NodeId y = s.addNode(makeNodeData(NodeKind::Primitive, "Y"), {}, p);
    const NodeId q = s.addNode(makeNodeData(NodeKind::Group, "Q"));
    CHECK(s.topLevelOnly({x, p, q, 999}) == std::vector<NodeId>{p, q});
    CHECK(s.sortedByTreeOrder({q, y, p}) == std::vector<NodeId>{p, y, q});
    CHECK(s.indexInParent(y) == 1);

    NodeSnapshot snap = s.snapshot(p);
    CHECK(snap.nodeCount() == 3);
    s.removeNode(p);
    // Free ids are kept on insertion...
    CHECK(s.insertSnapshot(snap, kInvalidNode, 0) == p);
    CHECK(s.childrenOf(p) == std::vector<NodeId>{x, y});
    // ...taken ids are replaced by fresh ones.
    const NodeId copy = s.insertSnapshot(snap);
    CHECK(copy != p);
    CHECK(s.find(copy)->children().size() == 2);
    CHECK(s.nodeCount() == 7);
    CHECK(s.insertSnapshot(snap, 12345) == kInvalidNode);  // unknown parent
}

TEST_CASE("Scene: effective visibility and lock follow ancestors and layers") {
    Scene s;
    s.setLayers({Layer{"Default"}, Layer{"Truss"}});
    const NodeId g = s.addNode(makeNodeData(NodeKind::Group));
    NodeData d = makeNodeData(NodeKind::Primitive);
    d.layer = 1;
    const NodeId p = s.addNode(d, {}, g);
    CHECK(s.effectiveVisible(p));
    std::vector<Layer> layers = s.layers();
    layers[1].visible = false;
    layers[1].locked = true;
    s.setLayers(layers);
    CHECK_FALSE(s.effectiveVisible(p));
    CHECK(s.effectiveLocked(p));
    CHECK(s.effectiveVisible(g));
    layers[1].visible = true;
    layers[1].locked = false;
    s.setLayers(layers);
    NodeData gd = s.find(g)->data();
    gd.visible = false;
    gd.locked = true;
    s.setData(g, gd);
    CHECK_FALSE(s.effectiveVisible(p));
    CHECK(s.effectiveLocked(p));
    // Out-of-range layers fall back to the default layer.
    d.layer = 42;
    s.setData(p, d);
    CHECK(s.layerOf(p).name == "Default");
}

TEST_CASE("Scene: revisions change on edits") {
    Scene s;
    const auto r0 = s.revision();
    const auto sr0 = s.structureRevision();
    const NodeId n = s.addNode(makeNodeData(NodeKind::Group));
    CHECK(s.revision() > r0);
    CHECK(s.structureRevision() > sr0);
    const auto sr1 = s.structureRevision();
    s.setTransform(n, moved(1, 2, 3));
    CHECK(s.structureRevision() == sr1);  // moving is not a structural change
}

TEST_CASE("Selection: order, primary, toggle, hover and prune") {
    Scene s;
    const NodeId a = s.addNode(makeNodeData(NodeKind::Group));
    const NodeId b = s.addNode(makeNodeData(NodeKind::Group));
    Selection sel;
    sel.set(a);
    sel.add(b);
    CHECK(sel.ids() == std::vector<NodeId>{a, b});
    CHECK(sel.primary() == b);
    sel.toggle(b);
    CHECK(sel.ids() == std::vector<NodeId>{a});
    CHECK(sel.primary() == a);
    sel.setHover(b);
    CHECK(sel.highlightFor(a) == Highlight::Selected);
    CHECK(sel.highlightFor(b) == Highlight::Hover);
    sel.add(b);
    s.removeNode(b);
    sel.prune(s);
    CHECK(sel.ids() == std::vector<NodeId>{a});
    CHECK(sel.hover() == kInvalidNode);
    sel.clear();
    CHECK(sel.empty());
    CHECK(sel.primary() == kInvalidNode);
}
