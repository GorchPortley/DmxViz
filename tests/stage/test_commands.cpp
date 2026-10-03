#include "stage/CommandStack.h"
#include "stage/Commands.h"
#include "stage/Scene.h"

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

void checkVec(const glm::vec3& a, const glm::vec3& b) {
    CHECK(a.x == doctest::Approx(b.x));
    CHECK(a.y == doctest::Approx(b.y));
    CHECK(a.z == doctest::Approx(b.z));
}

// A compact description of the tree: "name(child,child)" in order, with ids.
std::string describe(const Scene& s, NodeId parent = kInvalidNode) {
    std::string out;
    for (NodeId id : s.childrenOf(parent)) {
        if (!out.empty()) out += ",";
        out += s.find(id)->name() + "#" + std::to_string(id);
        if (!s.childrenOf(id).empty()) out += "(" + describe(s, id) + ")";
    }
    return out;
}

}  // namespace

TEST_CASE("AddNodeCommand: undo/redo keeps the id") {
    Scene s;
    CommandStack stack(s);
    auto* cmd = static_cast<AddNodeCommand*>(
        stack.execute(std::make_unique<AddNodeCommand>(makeNodeData(NodeKind::Primitive, "Box"), moved(1, 2, 3))));
    REQUIRE(cmd);
    const NodeId id = cmd->createdId();
    CHECK(cmd->resultNodes() == std::vector<NodeId>{id});
    CHECK(stack.undoName() == "Add Box");
    REQUIRE(stack.undo());
    CHECK_FALSE(s.contains(id));
    REQUIRE(stack.redo());
    REQUIRE(s.contains(id));
    checkVec(s.find(id)->transform().position, {1, 2, 3});
    // Adding under a missing parent fails and records nothing.
    CHECK_FALSE(stack.execute(std::make_unique<AddNodeCommand>(makeNodeData(NodeKind::Group), Transform{}, 999)));
    CHECK(stack.undoCount() == 1);
}

TEST_CASE("DeleteNodesCommand: subtrees come back with ids, parents and sibling order") {
    Scene s;
    const NodeId p = s.addNode(makeNodeData(NodeKind::Group, "P"));
    const NodeId a = s.addNode(makeNodeData(NodeKind::Group, "A"), {}, p);
    s.addNode(makeNodeData(NodeKind::Primitive, "A1"), {}, a);
    s.addNode(makeNodeData(NodeKind::Primitive, "A2"), moved(0, 1, 0), a);
    const NodeId b = s.addNode(makeNodeData(NodeKind::Primitive, "B"), {}, p);
    const NodeId c = s.addNode(makeNodeData(NodeKind::Primitive, "C"), {}, p);
    const NodeId d = s.addNode(makeNodeData(NodeKind::Primitive, "D"));
    const std::string original = describe(s);
    const std::size_t count = s.nodeCount();

    CommandStack stack(s);
    // A child of a deleted node in the list is ignored (deleted with its parent).
    REQUIRE(stack.execute(std::make_unique<DeleteNodesCommand>(std::vector<NodeId>{c, a, d, a + 1})));
    CHECK(describe(s) == "P#" + std::to_string(p) + "(B#" + std::to_string(b) + ")");
    CHECK(s.nodeCount() == 2);
    REQUIRE(stack.undo());
    CHECK(describe(s) == original);
    CHECK(s.nodeCount() == count);
    checkVec(worldPos(s, s.findByName("A2")), {0, 1, 0});
    REQUIRE(stack.redo());
    CHECK(s.nodeCount() == 2);
    REQUIRE(stack.undo());
    CHECK(describe(s) == original);
    CHECK_FALSE(stack.execute(std::make_unique<DeleteNodesCommand>(std::vector<NodeId>{4711})));
}

TEST_CASE("ReparentCommand: keeps world placement, restores order, refuses cycles") {
    Scene s;
    const NodeId g = s.addNode(makeNodeData(NodeKind::Group, "G"), moved(10, 0, 0));
    const NodeId a = s.addNode(makeNodeData(NodeKind::Primitive, "A"), moved(1, 0, 0));
    const NodeId b = s.addNode(makeNodeData(NodeKind::Primitive, "B"), moved(2, 0, 0));
    const NodeId c = s.addNode(makeNodeData(NodeKind::Primitive, "C"), moved(3, 0, 0));
    const std::string original = describe(s);
    CommandStack stack(s);
    REQUIRE(stack.execute(std::make_unique<ReparentCommand>(std::vector<NodeId>{a, c}, g)));
    CHECK(s.childrenOf(g) == std::vector<NodeId>{a, c});
    checkVec(worldPos(s, a), {1, 0, 0});
    checkVec(s.find(a)->transform().position, {-9, 0, 0});
    REQUIRE(stack.undo());
    CHECK(describe(s) == original);
    checkVec(s.find(a)->transform().position, {1, 0, 0});

    // Reorder among siblings (index counts after removal of the moved node).
    REQUIRE(stack.execute(std::make_unique<ReparentCommand>(std::vector<NodeId>{g}, kInvalidNode, 2)));
    CHECK(s.roots() == std::vector<NodeId>{a, b, g, c});
    stack.undo();
    CHECK(describe(s) == original);

    // Without keepWorld the local transform is kept.
    REQUIRE(stack.execute(std::make_unique<ReparentCommand>(std::vector<NodeId>{b}, g, -1, false)));
    checkVec(worldPos(s, b), {12, 0, 0});
    // Cycles are refused.
    CHECK_FALSE(stack.execute(std::make_unique<ReparentCommand>(std::vector<NodeId>{g}, b)));
}

TEST_CASE("ReparentCommand: several siblings of one parent move to another parent and undo restores the order") {
    Scene s;
    const NodeId p = s.addNode(makeNodeData(NodeKind::Group, "P"));
    const NodeId q = s.addNode(makeNodeData(NodeKind::Group, "Q"));
    std::vector<NodeId> kids;
    for (const char* name : {"A", "B", "C", "D", "E", "F"})
        kids.push_back(s.addNode(makeNodeData(NodeKind::Primitive, name), {}, p));
    const NodeId x = s.addNode(makeNodeData(NodeKind::Primitive, "X"), {}, q);
    const NodeId y = s.addNode(makeNodeData(NodeKind::Primitive, "Y"), {}, q);
    const std::string original = describe(s);
    const std::vector<NodeId> dragged{kids[0], kids[2], kids[4]};  // A, C, E: not next to each other

    CommandStack stack(s);
    REQUIRE(stack.execute(std::make_unique<ReparentCommand>(dragged, q, 1)));
    const std::string afterMove = describe(s);
    CHECK(s.childrenOf(q) == std::vector<NodeId>{x, kids[0], kids[2], kids[4], y});
    CHECK(s.childrenOf(p) == std::vector<NodeId>{kids[1], kids[3], kids[5]});

    REQUIRE(stack.undo());
    CHECK(describe(s) == original);
    REQUIRE(stack.redo());
    CHECK(describe(s) == afterMove);
    REQUIRE(stack.undo());
    CHECK(describe(s) == original);
}

TEST_CASE("ReparentCommand: index counts among the siblings that are not dragged") {
    Scene s;
    const NodeId p = s.addNode(makeNodeData(NodeKind::Group, "P"));
    std::vector<NodeId> k;
    for (const char* name : {"A", "B", "C", "D", "E", "F"})
        k.push_back(s.addNode(makeNodeData(NodeKind::Primitive, name), {}, p));
    const std::string original = describe(s);
    CommandStack stack(s);

    // Dragging A and C inside their own parent: the others are B, D, E, F; index 2 is "before E".
    REQUIRE(stack.execute(std::make_unique<ReparentCommand>(std::vector<NodeId>{k[0], k[2]}, p, 2)));
    CHECK(s.childrenOf(p) == std::vector<NodeId>{k[1], k[3], k[0], k[2], k[4], k[5]});
    REQUIRE(stack.undo());
    CHECK(describe(s) == original);
    REQUIRE(stack.redo());
    CHECK(s.childrenOf(p) == std::vector<NodeId>{k[1], k[3], k[0], k[2], k[4], k[5]});
    REQUIRE(stack.undo());
    CHECK(describe(s) == original);

    // Dragging B, D and F to the front, and C and E to the end (index -1).
    REQUIRE(stack.execute(std::make_unique<ReparentCommand>(std::vector<NodeId>{k[1], k[3], k[5]}, p, 0)));
    CHECK(s.childrenOf(p) == std::vector<NodeId>{k[1], k[3], k[5], k[0], k[2], k[4]});
    REQUIRE(stack.undo());
    CHECK(describe(s) == original);
    REQUIRE(stack.execute(std::make_unique<ReparentCommand>(std::vector<NodeId>{k[2], k[4]}, p, -1)));
    CHECK(s.childrenOf(p) == std::vector<NodeId>{k[0], k[1], k[3], k[5], k[2], k[4]});
    REQUIRE(stack.undo());
    CHECK(describe(s) == original);
}

TEST_CASE("SetTransformCommand: drags merge into one step until breakMerge") {
    Scene s;
    const NodeId a = s.addNode(makeNodeData(NodeKind::Primitive), moved(0, 0, 0));
    const NodeId b = s.addNode(makeNodeData(NodeKind::Primitive), moved(0, 0, 0));
    CommandStack stack(s);
    for (int i = 1; i <= 5; ++i) stack.execute(std::make_unique<SetTransformCommand>(a, moved(float(i), 0, 0), "Move"));
    CHECK(stack.undoCount() == 1);
    checkVec(s.find(a)->transform().position, {5, 0, 0});
    stack.breakMerge();
    stack.execute(std::make_unique<SetTransformCommand>(a, moved(6, 0, 0), "Move"));
    CHECK(stack.undoCount() == 2);
    // A different node set does not merge.
    stack.execute(std::make_unique<SetTransformCommand>(
        std::vector<std::pair<NodeId, Transform>>{{a, moved(7, 0, 0)}, {b, moved(1, 1, 1)}}, "Move"));
    CHECK(stack.undoCount() == 3);
    stack.undo();
    stack.undo();
    checkVec(s.find(a)->transform().position, {5, 0, 0});
    stack.undo();
    checkVec(s.find(a)->transform().position, {0, 0, 0});
    // Undo closes the merge window: a new drag is its own step.
    stack.redo();
    stack.execute(std::make_unique<SetTransformCommand>(a, moved(9, 0, 0)));
    CHECK(stack.undoCount() == 2);
    CHECK_FALSE(stack.canRedo());
}

TEST_CASE("EditNodesCommand: property edits, merge keys and the makeEditCommand helper") {
    Scene s;
    const NodeId a = s.addNode(makeNodeData(NodeKind::StageDeck, "Deck"));
    const NodeId b = s.addNode(makeNodeData(NodeKind::Primitive, "Box"));
    CommandStack stack(s);
    for (float h : {0.8f, 1.0f, 1.2f}) {
        stack.execute(makeEditCommand(
            s, {a}, "Deck height", [h](NodeData& d) { d.as<StageDeckContent>()->height = h; }, "deck.height"));
    }
    CHECK(stack.undoCount() == 1);
    CHECK(s.find(a)->as<StageDeckContent>()->height == doctest::Approx(1.2f));
    stack.execute(makeEditCommand(s, {a, b, 777}, "Hide", [](NodeData& d) { d.visible = false; }));
    CHECK(stack.undoCount() == 2);
    CHECK_FALSE(s.find(a)->data().visible);
    CHECK_FALSE(s.find(b)->data().visible);
    stack.undo();
    CHECK(s.find(b)->data().visible);
    stack.undo();
    CHECK(s.find(a)->as<StageDeckContent>()->height == doctest::Approx(StageDeckContent{}.height));
    CHECK(makeEditCommand(s, {999}, "x", [](NodeData&) {}) == nullptr);
}

TEST_CASE("GroupCommand and UngroupCommand keep world placement and undo exactly") {
    Scene s;
    const NodeId p = s.addNode(makeNodeData(NodeKind::Group, "P"), moved(0, 1, 0));
    const NodeId a = s.addNode(makeNodeData(NodeKind::Primitive, "A"), moved(1, 0, 0), p);
    const NodeId b = s.addNode(makeNodeData(NodeKind::Primitive, "B"), moved(2, 0, 0), p);
    const NodeId c = s.addNode(makeNodeData(NodeKind::Primitive, "C"), moved(3, 0, 0), p);
    const std::string original = describe(s);
    CommandStack stack(s);
    auto* group = static_cast<GroupCommand*>(stack.execute(std::make_unique<GroupCommand>(std::vector<NodeId>{c, a})));
    REQUIRE(group);
    const NodeId g = group->groupId();
    CHECK(s.find(g)->kind() == NodeKind::Group);
    CHECK(s.childrenOf(p) == std::vector<NodeId>{g, b});
    CHECK(s.childrenOf(g) == std::vector<NodeId>{a, c});  // tree order
    checkVec(worldPos(s, g), {2, 1, 0});                  // centre of the members
    checkVec(worldPos(s, a), {1, 1, 0});
    checkVec(worldPos(s, c), {3, 1, 0});
    stack.undo();
    CHECK(describe(s) == original);
    stack.redo();
    CHECK(s.contains(g));  // same group id on redo
    CHECK(s.childrenOf(g) == std::vector<NodeId>{a, c});

    stack.execute(std::make_unique<UngroupCommand>(std::vector<NodeId>{g, b}));  // b is not a group: ignored
    CHECK_FALSE(s.contains(g));
    CHECK(s.childrenOf(p) == std::vector<NodeId>{a, c, b});
    checkVec(worldPos(s, c), {3, 1, 0});
    stack.undo();
    CHECK(s.childrenOf(p) == std::vector<NodeId>{g, b});
    CHECK(s.childrenOf(g) == std::vector<NodeId>{a, c});
    stack.undo();
    CHECK(describe(s) == original);
}

TEST_CASE("Duplicate and paste create fresh ids next to the originals") {
    Scene s;
    const NodeId a = s.addNode(makeNodeData(NodeKind::Group, "A"), moved(1, 0, 0));
    s.addNode(makeNodeData(NodeKind::Primitive, "A1"), {}, a);
    const NodeId b = s.addNode(makeNodeData(NodeKind::Primitive, "B"), moved(2, 0, 0));
    CommandStack stack(s);
    Command* dup = stack.execute(makeDuplicateCommand(s, {a, b}, {0, 0, 5}));
    REQUIRE(dup);
    const std::vector<NodeId> copies = dup->resultNodes();
    REQUIRE(copies.size() == 2);
    CHECK(s.nodeCount() == 6);
    // Order: A, A', B, B'.
    const std::vector<NodeId> roots = s.roots();
    REQUIRE(roots.size() == 4);
    CHECK(roots[0] == a);
    CHECK(s.find(roots[1])->name() == "A");
    CHECK(s.find(roots[1])->children().size() == 1);
    CHECK(roots[2] == b);
    checkVec(worldPos(s, roots[3]), {2, 0, 5});
    stack.undo();
    CHECK(s.nodeCount() == 3);
    stack.redo();
    CHECK(s.contains(copies[0]));
    CHECK(s.contains(copies[1]));

    Command* paste = stack.execute(makePasteCommand({s.snapshot(a)}, b));
    REQUIRE(paste);
    CHECK(s.childrenOf(b).size() == 1);
    CHECK(paste->resultNodes().front() != a);
}

TEST_CASE("CommandStack: batches, compound failure, dirty flag and history limit") {
    Scene s;
    CommandStack stack(s, 3);
    CHECK_FALSE(stack.isDirty());
    stack.beginBatch("Build rig");
    stack.execute(std::make_unique<AddNodeCommand>(makeNodeData(NodeKind::Group, "1")));
    stack.execute(std::make_unique<AddNodeCommand>(makeNodeData(NodeKind::Group, "2")));
    CHECK(stack.isDirty());
    stack.endBatch();
    CHECK(stack.undoCount() == 1);
    CHECK(stack.undoName() == "Build rig");
    stack.undo();
    CHECK(s.empty());
    CHECK_FALSE(stack.isDirty());
    stack.redo();
    CHECK(s.nodeCount() == 2);

    stack.markSaved();
    CHECK_FALSE(stack.isDirty());
    const NodeId n = s.roots().front();
    stack.execute(std::make_unique<SetTransformCommand>(n, moved(1, 0, 0)));
    CHECK(stack.isDirty());
    stack.undo();
    CHECK_FALSE(stack.isDirty());
    stack.redo();
    CHECK(stack.isDirty());
    stack.markSaved();
    stack.undo();
    CHECK(stack.isDirty());  // undoing past the save point
    stack.redo();
    CHECK_FALSE(stack.isDirty());

    // Merging into the saved step changes the scene, so it becomes dirty.
    stack.execute(std::make_unique<SetTransformCommand>(n, moved(2, 0, 0)));
    stack.markSaved();
    const std::size_t steps = stack.undoCount();
    stack.execute(std::make_unique<SetTransformCommand>(n, moved(3, 0, 0)));
    CHECK(stack.undoCount() == steps);
    CHECK(stack.isDirty());

    // A compound whose second command fails is rolled back completely.
    std::vector<std::unique_ptr<Command>> parts;
    parts.push_back(std::make_unique<AddNodeCommand>(makeNodeData(NodeKind::Group, "x")));
    parts.push_back(std::make_unique<DeleteNodesCommand>(std::vector<NodeId>{999}));
    const std::size_t before = s.nodeCount();
    CHECK_FALSE(stack.execute(std::make_unique<CompoundCommand>("Fails", std::move(parts))));
    CHECK(s.nodeCount() == before);

    // History limit: only the newest 3 steps remain.
    for (int i = 0; i < 5; ++i) {
        stack.breakMerge();
        stack.execute(std::make_unique<SetTransformCommand>(n, moved(float(10 + i), 0, 0)));
    }
    CHECK(stack.undoCount() == 3);
    while (stack.undo()) {
    }
    checkVec(s.find(n)->transform().position, {11, 0, 0});
    CHECK(stack.isDirty());

    stack.clear();
    CHECK_FALSE(stack.canUndo());
    CHECK(stack.isDirty());  // clear() keeps the dirty state
}

TEST_CASE("SetLayersCommand") {
    Scene s;
    CommandStack stack(s);
    stack.execute(std::make_unique<SetLayersCommand>(std::vector<Layer>{Layer{"Default"}, Layer{"Set"}}));
    CHECK(s.layers().size() == 2);
    stack.undo();
    CHECK(s.layers().size() == 1);
}
