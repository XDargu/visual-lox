
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"
#include "../../script/script.h"

#include "../../utilities/utils.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct BeginNode : public Node
{
    BeginNode(int id, const char* name, const BasicFunctionDefPtr& functionDef)
        : Node(id, name, ImColor(255, 255, 255))
        , functionDef(functionDef)
    {
        Category = NodeCategory::Begin;
    }

    virtual void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
    }

    virtual void Refresh(const Script& script, IDGenerator& IDGenerator) override
    {
        Description = "Entry point for '" + functionDef->name + "'. " +
            functionDef->description;
        std::vector<Pin> saved;
        for (const Pin& output : Outputs)
            if (output.Type != PinType::Flow)
                saved.push_back(output);
        saved.insert(saved.end(), UnresolvedOutputs.begin(), UnresolvedOutputs.end());

        std::vector<Pin> refreshed;
        const auto flow = std::find_if(Outputs.begin(), Outputs.end(), [](const Pin& output) { return output.Type == PinType::Flow; });
        Pin flowPin = flow != Outputs.end() ? *flow : Pin(IDGenerator.GetNextId(), "", PinType::Flow, "Starts execution of this graph.");
        flowPin.Identity = PortIdentity::Fixed("start");
        refreshed.push_back(std::move(flowPin));

        for (const BasicFunctionDef::Input& input : functionDef->inputs)
        {
            const auto existing = std::find_if(saved.begin(), saved.end(), [&](const Pin& output)
            {
                return PortIdentitiesMatch(output.Identity, PortIdentity::Script(input.persistentId));
            });
            Pin output = existing != saved.end() ? *existing : Pin(IDGenerator.GetNextId(), input.name.c_str(), input.type, input.description);
            if (existing != saved.end()) saved.erase(existing);
            output.Name = input.name;
            output.Type = output.DeclaredType = input.type;
            output.Description = input.description;
            output.Identity = PortIdentity::Script(input.persistentId);
            refreshed.push_back(std::move(output));
        }
        Outputs = std::move(refreshed);
        UnresolvedOutputs = std::move(saved);
    }

    BasicFunctionDefPtr functionDef;
};

static NodePtr BuildBeginNode(IDGenerator& IDGenerator, const ScriptFunctionPtr& function)
{
    NodePtr node = std::make_shared<BeginNode>(IDGenerator.GetNextId(), "Begin", function->functionDef);
    node->SerializationType = "begin";
    node->DefinitionId = "vlox.core.begin";
    node->Description = "Entry point for '" + function->functionDef->name + "'. " +
        function->functionDef->description;
    node->DefinitionFlags |= NodeDefinitionFlags::Protected;
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow,
        "Starts execution of this graph.");
    node->Outputs.back().Identity = PortIdentity::Fixed("start");

    for (int i = 0; i < function->functionDef->inputs.size(); ++i)
    {
        const BasicFunctionDef::Input& input = function->functionDef->inputs[i];
        node->Outputs.emplace_back(IDGenerator.GetNextId(), input.name.c_str(), input.type,
            input.description);
        node->Outputs.back().Identity = PortIdentity::Script(input.persistentId);
    }

    return node;
}
