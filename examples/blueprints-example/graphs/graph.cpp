# pragma once

#include "graph.h"

#include <Compiler.h>

#include <algorithm>
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

bool Graph::CanCreateLink(PinType a, PinType b) const
{
    if (a != b)
    {
        const bool isAny = (a == PinType::Any || b == PinType::Any);
        const bool isFlow = (a == PinType::Flow || b == PinType::Flow);

        return isAny && !isFlow;
    }

    return true;
}

bool Graph::CanCreateLink(const Pin* a, const Pin* b) const
{
    if (!a || !b || a == b || a->Kind == b->Kind || a->Node == b->Node)
        return false;

    return CanCreateLink(a->Type, b->Type);
}

std::string Graph::LinkCreationFailedReason(const Pin& startPin, const Pin& endPin) const
{
    if (endPin.Kind == startPin.Kind)
    {
        return "Incompatible Pin Kind";
    }
    else if (endPin.Node == startPin.Node)
    {
        return "Cannot connect to self";
    }
    else if (endPin.Type != startPin.Type && endPin.Type != PinType::Any && startPin.Type != PinType::Any)
    {
        return "Incompatible Pin Type";
    }

    return "Unknown";
}

void Graph::DeleteNode(ed::NodeId id)
{
    auto nodeIt = std::find_if(m_Nodes.begin(), m_Nodes.end(), [id](auto& node) { return node->ID == id; });
    if (nodeIt != m_Nodes.end())
        m_Nodes.erase(nodeIt);
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

 bool GraphUtils::IsNodeConstFoldable(const Graph& graph, const NodePtr& node)
 {
     if (!HasFlag(node->Flags, NodeFlags::CanConstFold))
         return false;

     if (node->Category != NodeCategory::Function)
         return false;

     for (const Pin& input : node->Inputs)
     {
         if (input.Type != PinType::Flow)
         {
             if (const Pin* output = FindConnectedOutput(graph, input))
             {
                 if (!IsNodeConstFoldable(graph, output->Node))
                     return false;
             }
         }
     }

     return true;
 }
