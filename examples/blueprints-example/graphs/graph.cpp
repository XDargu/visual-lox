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

bool Graph::CanCreateLink(const Pin* a, const Pin* b) const
{
    if (!a || !b || a == b || a->Kind == b->Kind || a->Type != b->Type || a->Node == b->Node)
        return false;

    return true;
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

 int GraphUtils::FindNodeInputIdx(const Pin& input)
 {
     if (!input.Node)
         return -1;

     for (int i = 0; i < input.Node->Inputs.size(); ++i)
     {
         if (input.Node->Inputs[i].ID == input.ID)
         {
             return i;
         }
     }

     return -1;
 }

 int GraphUtils::FindNodeOutputIdx(const Pin& output)
 {
     if (!output.Node)
         return -1;

     for (int i = 0; i < output.Node->Outputs.size(); ++i)
     {
         if (output.Node->Outputs[i].ID == output.ID)
         {
             return i;
         }
     }

     return -1;
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
