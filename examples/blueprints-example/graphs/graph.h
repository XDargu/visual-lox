
# pragma once
#define IMGUI_DEFINE_MATH_OPERATORS

#include "node.h"
#include "link.h"
#include "idgeneration.h"

#include "../utilities/builders.h"
#include "../utilities/widgets.h"
#include "../utilities/drawing.h"

#include <imgui_internal.h>
#include <imgui_node_editor.h>

#include <map>
#include <vector>

class Compiler;
class VM;

namespace ed = ax::NodeEditor;

struct Graph
{
    NodePtr FindNode(ed::NodeId id);
    Link* FindLink(ed::LinkId id);
    Pin* FindPin(ed::PinId id);

    const NodePtr FindNode(ed::NodeId id) const;
    const Link* FindLink(ed::LinkId id) const;
    const Pin* FindPin(ed::PinId id) const;

    template<class Func>
    NodePtr FindNodeIf(Func func)
    {
        for (auto& node : m_Nodes)
            if (func(node))
                return node;

        return nullptr;
    }

    template<class Func>
    const NodePtr FindNodeIf(Func func) const
    {
        for (auto& node : m_Nodes)
            if (func(node))
                return node;

        return nullptr;
    }

    bool IsPinLinked(ed::PinId id) const;

    void DeleteNode(ed::NodeId id);
    void DeleteLink(ed::LinkId id);

    NodePtr AddNode(const NodePtr& node);
    Link* AddLink(Link& link);

    std::vector<NodePtr>& GetNodes() { return m_Nodes; } // TODO: Remove
    std::vector<Link>& GetLinks() { return m_Links; } // TODO: Remove

    const std::vector<NodePtr>& GetNodes() const { return m_Nodes; }
    const std::vector<Link>& GetLinks() const { return m_Links; }

private:
    std::vector<NodePtr>   m_Nodes;
    std::vector<Link>    m_Links;
};

// TODO: Move somewhere else
struct ProcessedNode
{
    NodePtr node;
    std::vector<int> stackFrames;
};

namespace GraphUtils
{
    bool IsNodeImplicit(const Node* node);
    bool IsNodeImplicit(const NodePtr& node);

    int FindNodeInputIdx(const Node* node, ed::PinId pinId);
    int FindNodeOutputIdx(const Node* node, ed::PinId pinId);

    int FindNodeInputIdx(const NodePtr& node, ed::PinId pinId);
    int FindNodeOutputIdx(const NodePtr& node, ed::PinId pinId);

    int FindNodeInputIdx(const Pin& input);
    int FindNodeOutputIdx(const Pin& output);

    std::vector<const Pin*> FindConnectedInputs(const Graph& graph, const Pin& outputPin);
    std::vector<const Pin*> FindConnectedOutputs(const Graph& graph, const Pin& inputPin);

    const Pin* FindConnectedOutput(const Graph& graph, const Pin& inputPin);

    std::vector<const Link*> CollectInputLinks(const Graph& graph, const Pin& inputPin);
    std::vector<const Link*> CollectOutputLinks(const Graph& graph, const Pin& outputPin);

    bool IsNodeConstFoldable(const Graph& graph, const NodePtr& node);

    bool AreTypesCompatible(PinType a, PinType b);
    bool CanCreateLink(const Pin* a, const Pin* b, const std::vector<ProcessedNode>& processedNodes);
    std::string LinkCreationFailedReason(const Pin& startPin, const Pin& endPin, const std::vector<ProcessedNode>& processedNodes);
}