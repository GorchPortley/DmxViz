#include "stage/Node.h"

namespace dmxviz::stage {

NodeData makeNodeData(NodeKind kind, std::string name) { return makeNodeData(defaultContent(kind), std::move(name)); }

NodeData makeNodeData(NodeContent content, std::string name) {
    NodeData d;
    d.content = std::move(content);
    d.name = name.empty() ? std::string(nodeKindLabel(d.kind())) : std::move(name);
    return d;
}

void NodeSnapshot::clearIds() {
    id = kInvalidNode;
    for (NodeSnapshot& c : children) c.clearIds();
}

std::size_t NodeSnapshot::nodeCount() const {
    std::size_t n = 1;
    for (const NodeSnapshot& c : children) n += c.nodeCount();
    return n;
}

}  // namespace dmxviz::stage
