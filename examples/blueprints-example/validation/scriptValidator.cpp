#include "scriptValidator.h"

#include "../graphs/graph.h"
#include "../script/function.h"
#include "../script/script.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace
{
using RawId = uintptr_t;

RawId IdValue(ed::NodeId id) { return reinterpret_cast<RawId>(id.AsPointer()); }
RawId IdValue(ed::PinId id) { return reinterpret_cast<RawId>(id.AsPointer()); }
RawId IdValue(ed::LinkId id) { return reinterpret_cast<RawId>(id.AsPointer()); }

bool HasFlowPins(const Node& node)
{
    const auto isFlow = [](const Pin& pin) { return pin.Type == PinType::Flow; };
    return std::any_of(node.Inputs.begin(), node.Inputs.end(), isFlow) ||
           std::any_of(node.Outputs.begin(), node.Outputs.end(), isFlow);
}

void ValidateGraph(const ScriptFunction& function, ValidationReport& report,
                   std::set<RawId>& documentIds, bool isClassFunction = false,
                   bool isConstructor = false)
{
    const Graph& graph = function.Graph;
    const std::string graphName = function.functionDef ? function.functionDef->name : "<unnamed>";
    const auto add = [&](DiagnosticSeverity severity, const char* code, std::string message,
                         ed::NodeId nodeId = 0, ed::PinId pinId = 0, ed::LinkId linkId = 0)
    {
        report.diagnostics.push_back({ severity, code, std::move(message), graphName,
                                       function.ID, nodeId, pinId, linkId });
    };
    const auto claimId = [&](RawId id, const char* kind, ed::NodeId nodeId = 0,
                             ed::PinId pinId = 0, ed::LinkId linkId = 0)
    {
        if (id == 0)
        {
            add(DiagnosticSeverity::Error, "invalid-id",
                std::string(kind) + " has an invalid ID.", nodeId, pinId, linkId);
            return;
        }
        if (!documentIds.insert(id).second)
        {
            add(DiagnosticSeverity::Error, "duplicate-id",
                std::string(kind) + " reuses document ID " + std::to_string(id) + ".",
                nodeId, pinId, linkId);
        }
    };

    size_t beginCount = 0;
    NodePtr beginNode;
    std::map<RawId, const Pin*> pins;
    std::map<RawId, NodePtr> nodes;

    for (const NodePtr& node : graph.GetNodes())
    {
        if (!node)
        {
            add(DiagnosticSeverity::Error, "null-node", "The graph contains a null node.");
            continue;
        }

        claimId(IdValue(node->ID), "Node", node->ID);
        nodes[IdValue(node->ID)] = node;
        if (node->Category == NodeCategory::Begin)
        {
            ++beginCount;
            beginNode = node;
        }
        if (node->SerializationType.empty())
            add(DiagnosticSeverity::Error, "missing-definition", "Node has no stable definition type.", node->ID);
        if (node->InputValues.size() != node->Inputs.size())
            add(DiagnosticSeverity::Error, "input-layout",
                "Node input pins and default values do not have the same size.", node->ID);
        if (HasFlag(node->InstanceFlags, NodeInstanceFlags::Error))
            add(DiagnosticSeverity::Error, "invalid-reference",
                node->Error.empty() ? "Node contains an invalid reference." : node->Error, node->ID);
        if (node->SerializationType == "class.this" && !isClassFunction)
            add(DiagnosticSeverity::Error, "this-outside-class",
                "The This node can only be used inside a class method or constructor.", node->ID);
        if (isConstructor && node->Category == NodeCategory::Return)
            add(DiagnosticSeverity::Error, "constructor-return",
                "Constructors return their instance implicitly and cannot contain Return nodes.", node->ID);
        for (const Pin& pin : node->Inputs)
        {
            claimId(IdValue(pin.ID), "Input pin", node->ID, pin.ID);
            pins[IdValue(pin.ID)] = &pin;
            if (pin.Kind != PinKind::Input || pin.Node.get() != node.get())
                add(DiagnosticSeverity::Error, "input-owner",
                    "Input pin ownership or direction is inconsistent.", node->ID, pin.ID);
        }
        for (const Pin& pin : node->Outputs)
        {
            claimId(IdValue(pin.ID), "Output pin", node->ID, pin.ID);
            pins[IdValue(pin.ID)] = &pin;
            if (pin.Kind != PinKind::Output || pin.Node.get() != node.get())
                add(DiagnosticSeverity::Error, "output-owner",
                    "Output pin ownership or direction is inconsistent.", node->ID, pin.ID);
        }
    }

    if (beginCount != 1)
        add(DiagnosticSeverity::Error, "begin-count",
            "Graph must contain exactly one Begin node; found " + std::to_string(beginCount) + ".");
    else if (beginNode->Outputs.empty() || beginNode->Outputs[0].Type != PinType::Flow)
        add(DiagnosticSeverity::Error, "begin-layout",
            "Begin must have a flow output as its first pin.", beginNode->ID);

    std::map<RawId, size_t> incomingDataLinks;
    std::map<RawId, size_t> outgoingFlowLinks;
    std::map<RawId, std::vector<RawId>> adjacency;
    std::map<RawId, std::vector<RawId>> flowAdjacency;

    for (const Link& link : graph.GetLinks())
    {
        claimId(IdValue(link.ID), "Link", 0, 0, link.ID);
        const auto startIt = pins.find(IdValue(link.StartPinID));
        const auto endIt = pins.find(IdValue(link.EndPinID));
        if (startIt == pins.end() || endIt == pins.end())
        {
            add(DiagnosticSeverity::Error, "missing-pin", "Link references a pin that does not exist.",
                0, 0, link.ID);
            continue;
        }

        const Pin* start = startIt->second;
        const Pin* end = endIt->second;
        if (!start->Node || !end->Node ||
            nodes.find(IdValue(start->Node->ID)) == nodes.end() ||
            nodes.find(IdValue(end->Node->ID)) == nodes.end())
        {
            add(DiagnosticSeverity::Error, "link-owner",
                "Link references a pin whose owning node is not in this graph.", 0, 0, link.ID);
            continue;
        }
        if (start->Kind != PinKind::Output || end->Kind != PinKind::Input)
            add(DiagnosticSeverity::Error, "link-direction",
                "Link must run from an output pin to an input pin.", end->Node->ID, end->ID, link.ID);
        if (start->Node == end->Node)
            add(DiagnosticSeverity::Error, "self-link", "A node cannot link to itself.",
                end->Node->ID, end->ID, link.ID);
        if (!GraphUtils::AreTypesCompatible(start->Type, end->Type))
            add(DiagnosticSeverity::Error, "link-type", "Link connects incompatible pin types.",
                end->Node->ID, end->ID, link.ID);

        if (start->Type == PinType::Flow)
        {
            if (++outgoingFlowLinks[IdValue(start->ID)] > 1)
                add(DiagnosticSeverity::Error, "multiple-flow-links",
                    "A flow output pin can only have one outgoing link.", start->Node->ID, start->ID, link.ID);
            flowAdjacency[IdValue(start->Node->ID)].push_back(IdValue(end->Node->ID));
        }
        else if (++incomingDataLinks[IdValue(end->ID)] > 1)
        {
            add(DiagnosticSeverity::Error, "multiple-data-links",
                "A data input pin can only have one incoming link.", end->Node->ID, end->ID, link.ID);
        }
        adjacency[IdValue(start->Node->ID)].push_back(IdValue(end->Node->ID));
    }

    std::map<RawId, int> colors;
    bool cycleReported = false;
    std::function<void(RawId)> visit = [&](RawId nodeId)
    {
        colors[nodeId] = 1;
        for (RawId next : adjacency[nodeId])
        {
            if (colors[next] == 0)
                visit(next);
            else if (colors[next] == 1 && !cycleReported)
            {
                cycleReported = true;
                const auto nextNode = nodes.find(next);
                add(DiagnosticSeverity::Error, "graph-cycle",
                    "Graph contains a dependency or flow cycle, which cannot be compiled.",
                    nextNode != nodes.end() ? nextNode->second->ID : ed::NodeId(0));
            }
        }
        colors[nodeId] = 2;
    };
    for (const auto& entry : nodes)
        if (colors[entry.first] == 0)
            visit(entry.first);

    std::set<RawId> reachable;
    std::function<void(RawId)> markReachable = [&](RawId nodeId)
    {
        if (!reachable.insert(nodeId).second)
            return;
        for (RawId next : flowAdjacency[nodeId])
            markReachable(next);
    };
    if (beginNode)
        markReachable(IdValue(beginNode->ID));

    bool hasReachableReturn = false;
    for (const auto& entry : nodes)
    {
        const NodePtr& node = entry.second;
        if (node->Category == NodeCategory::Return && reachable.count(entry.first) != 0)
            hasReachableReturn = true;
        if (HasFlowPins(*node) && reachable.count(entry.first) == 0)
            add(DiagnosticSeverity::Warning, "unreachable-node",
                "Flow node is not reachable from Begin.", node->ID);
    }
    if (function.functionDef && !function.functionDef->outputs.empty() && !hasReachableReturn)
        add(DiagnosticSeverity::Error, "missing-return",
            "Function declares outputs but has no reachable Return node.");
}
}

bool ValidationReport::HasErrors() const
{
    return ErrorCount() != 0;
}

size_t ValidationReport::ErrorCount() const
{
    return std::count_if(diagnostics.begin(), diagnostics.end(), [](const ValidationDiagnostic& item)
    {
        return item.severity == DiagnosticSeverity::Error;
    });
}

size_t ValidationReport::WarningCount() const
{
    return diagnostics.size() - ErrorCount();
}

std::vector<const ValidationDiagnostic*> ValidationReport::ForNode(ScriptElementID functionId,
                                                                    ed::NodeId nodeId) const
{
    std::vector<const ValidationDiagnostic*> result;
    for (const ValidationDiagnostic& diagnostic : diagnostics)
        if (diagnostic.functionId.id == functionId.id && diagnostic.nodeId == nodeId)
            result.push_back(&diagnostic);
    return result;
}

ValidationReport ScriptValidator::Validate(const Script& script)
{
    ValidationReport report;
    std::set<RawId> documentIds;
    const auto addScriptError = [&](const char* code, std::string message)
    {
        report.diagnostics.push_back({ DiagnosticSeverity::Error, code, std::move(message), "Script" });
    };
    const auto claimScriptId = [&](int id, const char* kind)
    {
        if (id <= 0)
            addScriptError("invalid-id", std::string(kind) + " has an invalid ID.");
        else if (!documentIds.insert(static_cast<RawId>(id)).second)
            addScriptError("duplicate-id", std::string(kind) + " reuses document ID " + std::to_string(id) + ".");
    };

    claimScriptId(script.ID.id, "Script");
    if (!script.main)
        addScriptError("missing-main", "Script has no main graph.");

    std::set<std::string> functionNames;
    std::set<std::string> variableNames;
    for (const ScriptPropertyPtr& variable : script.variables)
    {
        if (!variable)
        {
            addScriptError("null-variable", "Script contains a null variable definition.");
            continue;
        }
        claimScriptId(variable->ID.id, "Variable");
        if (!variableNames.insert(variable->Name).second)
            addScriptError("duplicate-variable", "Duplicate variable name '" + variable->Name + "'.");
    }

    const auto validateFunction = [&](const ScriptFunctionPtr& function, bool isMain,
                                      bool isClassFunction = false, bool isConstructor = false)
    {
        if (!function || !function->functionDef)
        {
            addScriptError("null-function", "Script contains an incomplete function definition.");
            return;
        }
        claimScriptId(function->ID.id, isMain ? "Main function" : "Function");
        if (!isMain && !isClassFunction && !functionNames.insert(function->functionDef->name).second)
            addScriptError("duplicate-function", "Duplicate function name '" + function->functionDef->name + "'.");
        if (!isMain && !isClassFunction && variableNames.count(function->functionDef->name) != 0)
            addScriptError("symbol-conflict", "Function and variable share the global name '" +
                function->functionDef->name + "'.");
        std::set<std::string> inputNames;
        std::set<std::string> outputNames;
        for (const BasicFunctionDef::Input& input : function->functionDef->inputs)
        {
            claimScriptId(input.id, "Function input");
            if (!inputNames.insert(input.name).second)
                addScriptError("duplicate-input", "Function '" + function->functionDef->name +
                    "' has duplicate input name '" + input.name + "'.");
        }
        for (const BasicFunctionDef::Input& output : function->functionDef->outputs)
        {
            claimScriptId(output.id, "Function output");
            if (!outputNames.insert(output.name).second)
                addScriptError("duplicate-output", "Function '" + function->functionDef->name +
                    "' has duplicate output name '" + output.name + "'.");
        }
        if (isConstructor && !function->functionDef->outputs.empty())
            addScriptError("constructor-output", "Constructors cannot declare output values.");
        ValidateGraph(*function, report, documentIds, isClassFunction, isConstructor);
    };

    if (script.main)
        validateFunction(script.main, true);
    for (const ScriptFunctionPtr& function : script.functions)
        validateFunction(function, false);

    std::set<std::string> classNames;
    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        if (!scriptClass)
        {
            addScriptError("null-class", "Script contains a null class definition.");
            continue;
        }
        claimScriptId(scriptClass->ID.id, "Class");
        if (!classNames.insert(scriptClass->Name).second)
            addScriptError("duplicate-class", "Duplicate class name '" + scriptClass->Name + "'.");
        if (variableNames.count(scriptClass->Name) || functionNames.count(scriptClass->Name))
            addScriptError("symbol-conflict", "Class name '" + scriptClass->Name +
                "' conflicts with another script-level symbol.");

        std::set<std::string> propertyNames;
        for (const ScriptPropertyPtr& property : scriptClass->properties)
        {
            if (!property)
            {
                addScriptError("null-property", "Class '" + scriptClass->Name +
                    "' contains a null property definition.");
                continue;
            }
            claimScriptId(property->ID.id, "Class property");
            if (!propertyNames.insert(property->Name).second)
                addScriptError("duplicate-property", "Class '" + scriptClass->Name +
                    "' has duplicate property '" + property->Name + "'.");
        }

        std::set<std::string> methodNames;
        for (const ScriptFunctionPtr& method : scriptClass->methods)
        {
            if (method && method->functionDef && !methodNames.insert(method->functionDef->name).second)
                addScriptError("duplicate-method", "Class '" + scriptClass->Name +
                    "' has duplicate method '" + method->functionDef->name + "'.");
            if (method && method->functionDef && propertyNames.count(method->functionDef->name))
                addScriptError("member-conflict", "Class '" + scriptClass->Name +
                    "' has a property and method named '" + method->functionDef->name + "'.");
            validateFunction(method, false, true, false);
        }
        if (scriptClass->constructor)
            validateFunction(scriptClass->constructor, false, true, true);
    }

    return report;
}

std::string FormatDiagnostic(const ValidationDiagnostic& diagnostic)
{
    std::ostringstream text;
    text << (diagnostic.severity == DiagnosticSeverity::Error ? "error" : "warning")
         << " [" << diagnostic.code << "]";
    if (!diagnostic.graphName.empty())
        text << " " << diagnostic.graphName << ":";
    if (diagnostic.nodeId)
        text << " node " << IdValue(diagnostic.nodeId) << ":";
    text << " " << diagnostic.message;
    return text.str();
}
