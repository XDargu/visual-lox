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

bool Graph::CanCreateLink(const Pin* a, const Pin* b, const std::vector<ProcessedNode>& processedNodes) const
{
    if (!a || !b || a == b || a->Kind == b->Kind || a->Node == b->Node)
        return false;

    if (!GraphUtils::AreTypesCompatible(a->Type, b->Type))
        return false;

    if (a->Type != PinType::Flow)
    {
        // TODO: Data ports can't hold multiple pins!
    }
    else
    {
        // TODO: 
    }

    auto aProcessedNode = std::find_if(processedNodes.begin(), processedNodes.end(), [&](const ProcessedNode& pnode) { return pnode.node->ID == a->Node->ID; });
    auto bProcessedNode = std::find_if(processedNodes.begin(), processedNodes.end(), [&](const ProcessedNode& pnode) { return pnode.node->ID == b->Node->ID; });

    if (aProcessedNode != processedNodes.end() && bProcessedNode != processedNodes.end())
    {
        // Are we trying to connect to something behind?
        size_t indexA = std::distance(processedNodes.begin(), aProcessedNode);
        size_t indexB = std::distance(processedNodes.begin(), bProcessedNode);

        if (a->Kind == PinKind::Input)
        {
            // Connecting b->a, a must be later!
            if (indexA < indexB)
                return false;
        }
        else
        {
            // Connecting a->b, b must be later!
            if (indexB < indexA)
                return false;
        }

        // Are we trying to connect to something on a different branch?
        const int shortestStackFrame = std::min(aProcessedNode->stackFrames.size(), bProcessedNode->stackFrames.size());

        for (int i = 0; i < shortestStackFrame; ++i)
        {
            int stackFrameA = aProcessedNode->stackFrames[i];
            int stackFrameB = bProcessedNode->stackFrames[i];

            if (stackFrameA != stackFrameB)
                return false;
        }
    }

    return true;
}

std::string Graph::LinkCreationFailedReason(const Pin& startPin, const Pin& endPin, const std::vector<ProcessedNode>& processedNodes) const
{
    if (endPin.Kind == startPin.Kind)
    {
        return "Incompatible Pin Kind";
    }
    else if (endPin.Node == startPin.Node)
    {
        return "Cannot connect to self";
    }
    else if (!GraphUtils::AreTypesCompatible(startPin.Type, endPin.Type))
    {
        return "Incompatible Pin Type";
    }
    else
    {
        auto aProcessedNode = std::find_if(processedNodes.begin(), processedNodes.end(), [&](const ProcessedNode& pnode) { return pnode.node->ID == startPin.Node->ID; });
        auto bProcessedNode = std::find_if(processedNodes.begin(), processedNodes.end(), [&](const ProcessedNode& pnode) { return pnode.node->ID == endPin.Node->ID; });

        if (aProcessedNode != processedNodes.end() && bProcessedNode != processedNodes.end())
        {
            // Are we trying to connect to something on a different branch?
            const int shortestStackFrame = std::min(aProcessedNode->stackFrames.size(), bProcessedNode->stackFrames.size());

            for (int i = 0; i < shortestStackFrame; ++i)
            {
                int stackFrameA = aProcessedNode->stackFrames[i];
                int stackFrameB = bProcessedNode->stackFrames[i];

                if (stackFrameA != stackFrameB)
                    return "Cannot connect to a different branch";
            }

            // Are we trying to connect to something behind?
            size_t indexA = std::distance(processedNodes.begin(), aProcessedNode);
            size_t indexB = std::distance(processedNodes.begin(), bProcessedNode);

            if (startPin.Kind == PinKind::Input)
            {
                // Connecting b->a, a must be later!
                if (indexA < indexB)
                    return "Cannot conntext to a node earlier in the sequence";
            }
            else
            {
                // Connecting a->b, b must be later!
                if (indexB < indexA)
                    return "Cannot conntext to a node earlier in the sequence";
            }
        }
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
     NodePtr current = child;

     while (current)
     {
         if (current == node) { return true; }

         NodePtr pParent = nullptr;

         for (const Pin& input : current->Inputs)
         {
             if (input.Type == PinType::Flow)
             {
                 if (const Pin* pin = FindConnectedOutput(graph, input))
                 {
                     pParent = pin->Node;
                 }
                 break;
             }
         }

         current = pParent;
     }

     return false;
 }
