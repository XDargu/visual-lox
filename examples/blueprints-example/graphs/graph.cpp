# pragma once

#include "graph.h"

#include <Compiler.h>

#include <algorithm>
#include <set>
#include <utility>

#include <imgui_node_editor_internal.h>

NodePtr Graph::FindNode(ed::NodeId id)
{
    for (auto& node : m_Nodes)
        if (node->ID == id)
            return node;

    return nullptr;
}

Link* Graph::FindLink(ed::LinkId id)
{
    for (auto& link : m_Links)
        if (link.ID == id)
            return &link;

    return nullptr;
}

Pin* Graph::FindPin(ed::PinId id)
{
    if (!id)
        return nullptr;

    for (auto& node : m_Nodes)
    {
        for (auto& pin : node->Inputs)
            if (pin.ID == id)
                return &pin;

        for (auto& pin : node->Outputs)
            if (pin.ID == id)
                return &pin;
    }

    return nullptr;
}

const NodePtr Graph::FindNode(ed::NodeId id) const
{
    for (auto& node : m_Nodes)
        if (node->ID == id)
            return node;

    return nullptr;
}

const Link* Graph::FindLink(ed::LinkId id) const
{
    for (auto& link : m_Links)
        if (link.ID == id)
            return &link;

    return nullptr;
}

const Pin* Graph::FindPin(ed::PinId id) const
{
    if (!id)
        return nullptr;

    for (auto& node : m_Nodes)
    {
        for (auto& pin : node->Inputs)
            if (pin.ID == id)
                return &pin;

        for (auto& pin : node->Outputs)
            if (pin.ID == id)
                return &pin;
    }

    return nullptr;
}

bool Graph::IsPinLinked(ed::PinId id) const
{
    if (!id)
        return false;

    for (auto& link : m_Links)
        if (link.StartPinID == id || link.EndPinID == id)
            return true;

    return false;
}

std::vector<ed::LinkId> Graph::CollectLinksToReplace(const Pin* a, const Pin* b) const
{
    std::vector<ed::LinkId> result;
    if (!a || !b || a->Kind == b->Kind)
        return result;

    const Pin* input = a->Kind == PinKind::Input ? a : b;
    const Pin* output = a->Kind == PinKind::Output ? a : b;
    for (const Link& link : m_Links)
    {
        const bool conflicts = output->Type == PinType::Flow
            ? link.StartPinID == output->ID
            : link.EndPinID == input->ID;
        if (conflicts)
            result.push_back(link.ID);
    }
    return result;
}

ELinkQueryResult Graph::CanCreateLink(const Pin* a, const Pin* b, const std::vector<ProcessedNode>& processedNodes) const
{
    if (!a || !b || a == b)
        return ELinkQueryResult::InvalidPin;

    if (a->Kind == b->Kind)
    {
        return ELinkQueryResult::IncompatibleKind;
    }

    if (a->Node == b->Node)
    {
        return ELinkQueryResult::SelfConnection;
    }

    if (!GraphUtils::AreTypesCompatible(a->Type, b->Type))
    {
        return ELinkQueryResult::IncompatibleType;
    }

    const Pin& input = a->Kind == PinKind::Input ? *a : *b;
    const Pin& output = a->Kind == PinKind::Output ? *a : *b;

    auto aProcessedNode = std::find_if(processedNodes.begin(), processedNodes.end(), [&](const ProcessedNode& pnode) { return pnode.node->ID == a->Node->ID; });
    auto bProcessedNode = std::find_if(processedNodes.begin(), processedNodes.end(), [&](const ProcessedNode& pnode) { return pnode.node->ID == b->Node->ID; });

    if (aProcessedNode != processedNodes.end() && bProcessedNode != processedNodes.end())
    {
        // Are we trying to connect to something behind?
        size_t indexA = std::distance(processedNodes.begin(), aProcessedNode);
        size_t indexB = std::distance(processedNodes.begin(), bProcessedNode);

        if (GraphUtils::IsNodeParent(*this, input.Node, output.Node))
            return ELinkQueryResult::CantConnectEarlier;

        // Are we trying to connect to something on a different branch? Only for data
        if (a->Type != PinType::Flow)
        {
            if (!GraphUtils::CanReachNodeAllPaths(*this, output.Node, input.Node))
                return ELinkQueryResult::CantConnectBranch;
        }
    }

    return ELinkQueryResult::Possible;
}
bool Graph::DeleteNode(ed::NodeId id)
{
    auto nodeIt = std::find_if(m_Nodes.begin(), m_Nodes.end(), [id](auto& node) { return node->ID == id; });
    if (nodeIt == m_Nodes.end() || HasFlag((*nodeIt)->DefinitionFlags, NodeDefinitionFlags::Protected))
        return false;

    std::set<int> pins;
    for (const Pin& pin : (*nodeIt)->Inputs)
        pins.insert(pin.ID.Get());
    for (const Pin& pin : (*nodeIt)->Outputs)
        pins.insert(pin.ID.Get());
    m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(), [&](const Link& link)
    {
        return pins.find(link.StartPinID.Get()) != pins.end() || pins.find(link.EndPinID.Get()) != pins.end();
    }), m_Links.end());
    m_Nodes.erase(nodeIt);
    return true;
}

void Graph::DeleteLink(ed::LinkId id)
{
    auto linkIt = std::find_if(m_Links.begin(), m_Links.end(), [id](auto& link) { return link.ID == id; });
    if (linkIt != m_Links.end())
        m_Links.erase(linkIt);
}

NodePtr Graph::AddNode(const NodePtr& node)
{
    m_Nodes.push_back(node);
    return m_Nodes.back();
}

 Link* Graph::AddLink(Link& link)
 {
     const Pin* start = FindPin(link.StartPinID);
     const Pin* end = FindPin(link.EndPinID);
     const std::vector<ed::LinkId> replacedLinks = CollectLinksToReplace(start, end);
     if (!replacedLinks.empty())
     {
         m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(), [&](const Link& existing)
         {
             return std::find(replacedLinks.begin(), replacedLinks.end(), existing.ID) != replacedLinks.end();
         }), m_Links.end());
     }
     m_Links.push_back(link);
     return &m_Links.back();
 }

 bool GraphUtils::IsNodeImplicit(const Node* node)
 {
     for (const Pin& input : node->Inputs)
     {
         if (input.Type == PinType::Flow)
             return false;
     }

     for (const Pin& output : node->Outputs)
     {
         if (output.Type == PinType::Flow)
             return false;
     }

     return true;
 }

 bool GraphUtils::IsNodeImplicit(const NodePtr& node)
 {
     for (const Pin& input : node->Inputs)
     {
         if (input.Type == PinType::Flow)
             return false;
     }

     for (const Pin& output : node->Outputs)
     {
         if (output.Type == PinType::Flow)
             return false;
     }

     return true;
 }

 int GraphUtils::FindNodeInputIdx(const Node* node, ed::PinId pinId)
 {
     if (!node)
         return -1;

     for (int i = 0; i < node->Inputs.size(); ++i)
     {
         if (node->Inputs[i].ID == pinId)
         {
             return i;
         }
     }

     return -1;
 }

 int GraphUtils::FindNodeOutputIdx(const Node* node, ed::PinId pinId)
 {
     if (!node)
         return -1;

     for (int i = 0; i < node->Outputs.size(); ++i)
     {
         if (node->Outputs[i].ID == pinId)
         {
             return i;
         }
     }

     return -1;
 }

 int GraphUtils::FindNodeInputIdx(const NodePtr& node, ed::PinId pinId)
 {
     return FindNodeInputIdx(node.get(), pinId);
 }

 int GraphUtils::FindNodeOutputIdx(const NodePtr& node, ed::PinId pinId)
 {
     return FindNodeOutputIdx(node.get(), pinId);
 }

 int GraphUtils::FindNodeInputIdx(const Pin& input)
 {
     return FindNodeInputIdx(input.Node, input.ID);
 }

 int GraphUtils::FindNodeOutputIdx(const Pin& output)
 {
     return FindNodeOutputIdx(output.Node, output.ID);
 }

 std::vector<const Pin*> GraphUtils::FindConnectedInputs(const Graph& graph, const Pin& outputPin)
 {
     std::vector<const Pin*> inputs;

     for (const Link& link : graph.GetLinks())
     {
         if (link.StartPinID == outputPin.ID)
         {
             inputs.push_back(graph.FindPin(link.EndPinID));
         }
     }

     return inputs;
 }

 std::vector<const Pin*> GraphUtils::FindConnectedOutputs(const Graph& graph, const Pin& inputPin)
 {
     std::vector<const Pin*> inputs;

     for (const Link& link : graph.GetLinks())
     {
         if (link.EndPinID == inputPin.ID)
         {
             inputs.push_back(graph.FindPin(link.StartPinID));
         }
     }

     return inputs;
 }

 const Pin* GraphUtils::FindConnectedOutput(const Graph& graph, const Pin& inputPin)
 {
     for (const Link& link : graph.GetLinks())
     {
         if (link.EndPinID == inputPin.ID)
         {
             return graph.FindPin(link.StartPinID);
         }
     }

     return nullptr;
 }

 std::vector<const Link*> GraphUtils::CollectInputLinks(const Graph& graph, const Pin& inputPin)
 {
     std::vector<const Link*> links;

     for (const Link& link : graph.GetLinks())
     {
         if (link.EndPinID == inputPin.ID)
         {
             links.push_back(&link);
         }
     }

     return links;
 }

 std::vector<const Link*> GraphUtils::CollectOutputLinks(const Graph& graph, const Pin& outputPin)
 {
     std::vector<const Link*> links;

     for (const Link& link : graph.GetLinks())
     {
         if (link.StartPinID == outputPin.ID)
         {
             links.push_back(&link);
         }
     }

     return links;
 }

 namespace
 {
 bool IsNodeConstFoldableRecursive(const Graph& graph, const NodePtr& node,
                                   std::set<const Node*>& visiting,
                                   std::set<const Node*>& verified)
 {
     if (!node || !node->IsPure() || !GraphUtils::IsNodeImplicit(node) ||
         HasFlag(node->InstanceFlags, NodeInstanceFlags::Error))
         return false;

     // The current folding bytecode path materializes one result. Definitions
     // with zero or multiple values need an explicit folding strategy first.
     const Pin* dataOutput = nullptr;
     size_t dataOutputCount = 0;
     for (const Pin& output : node->Outputs)
     {
         if (output.Type != PinType::Flow)
         {
             dataOutput = &output;
             ++dataOutputCount;
         }
     }
     if (dataOutputCount != 1)
         return false;
     if (dataOutput->Type != PinType::Bool && dataOutput->Type != PinType::Int &&
         dataOutput->Type != PinType::Float && dataOutput->Type != PinType::String)
         return false;

     if (verified.find(node.get()) != verified.end())
         return true;
     if (!visiting.insert(node.get()).second)
         return false;

     for (const Pin& input : node->Inputs)
     {
         if (input.Type == PinType::Flow)
             continue;

         if (const Pin* output = GraphUtils::FindConnectedOutput(graph, input))
         {
             if (!IsNodeConstFoldableRecursive(graph, output->Node, visiting, verified))
             {
                 visiting.erase(node.get());
                 return false;
             }
         }
     }

     visiting.erase(node.get());
     verified.insert(node.get());
     return true;
 }
 }

 bool GraphUtils::IsNodeConstFoldable(const Graph& graph, const NodePtr& node)
 {
     std::set<const Node*> visiting;
     std::set<const Node*> verified;
     return IsNodeConstFoldableRecursive(graph, node, visiting, verified);
 }

 bool GraphUtils::AreTypesCompatible(PinType a, PinType b)
 {
     if (a != b)
     {
         const bool isAny = (a == PinType::Any || b == PinType::Any);
         const bool isFlow = (a == PinType::Flow || b == PinType::Flow);

         return isAny && !isFlow;
     }

     return true;
 }

bool GraphUtils::IsNodeParent(const Graph& graph, const NodePtr& node, const NodePtr& child)
{
    if (child == node) { return true; }

    for (const Pin& input : child->Inputs)
    {
        if (input.Type == PinType::Flow)
        {
            const std::vector<const Pin*> outputs = GraphUtils::FindConnectedOutputs(graph, input);
            for (const Pin* output : outputs)
            {
                if (IsNodeParent(graph, node, output->Node))
                    return true;
            }
        }
    }

    return false;
}

bool GraphUtils::CanReachNodeAllPaths(const Graph& graph, const NodePtr& node, const NodePtr& child)
{
    if (child == node) { return true; } // Found the node
    if (child->Inputs.size() == 0) { return false; } // Begin node

    for (const Pin& input : child->Inputs)
    {
        if (input.Type == PinType::Flow)
        {
            const std::vector<const Pin*> outputs = GraphUtils::FindConnectedOutputs(graph, input);
            for (const Pin* output : outputs)
            {
                if (!CanReachNodeAllPaths(graph, node, output->Node))
                    return false; // One path wasn't successful
            }
        }
    }

    return true; // No paths failed
}
