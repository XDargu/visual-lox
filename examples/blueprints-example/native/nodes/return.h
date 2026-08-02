
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

struct ReturnNode : public Node
{
    ReturnNode(int id, const char* name, const BasicFunctionDefPtr& functionDef)
        : Node(id, name, ImColor(255, 255, 255))
    {
        Category = NodeCategory::Return;
        pFunctionDef = functionDef;
    }

    virtual void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        Compiler& compiler = compilerCtx.compiler;

        switch (stage)
        {
        case CompilationStage::BeginInputs:
        {
            const size_t outputCount = pFunctionDef ? pFunctionDef->outputs.size() : 0;
            if (outputCount == 0)
            {
                compiler.emitReturn();
                break;
            }

            if (outputCount == 1)
            {
                GraphCompiler::CompileInput(compilerCtx, graph, Inputs[1], InputValues[1]);
                compiler.emitByte(OpByte(OpCode::OP_RETURN));
                break;
            }

            compiler.emitByte(OpByte(OpCode::OP_BUILD_LIST));
            for (size_t outputIndex = 0; outputIndex < outputCount; ++outputIndex)
            {
                const size_t inputIndex = outputIndex + 1;
                GraphCompiler::CompileInput(
                    compilerCtx, graph, Inputs[inputIndex], InputValues[inputIndex]);
                compiler.emitByte(OpByte(OpCode::OP_APPEND_LIST));
            }
            compiler.emitByte(OpByte(OpCode::OP_RETURN));
        }
        break;
        }
    }

    virtual void Refresh(const Script& script, IDGenerator& IDGenerator) override
    {
        Description = "Returns from '" + pFunctionDef->name + "'.";
        std::vector<std::pair<Pin, Value>> saved;
        for (size_t index = 0; index < Inputs.size(); ++index)
            if (Inputs[index].Type != PinType::Flow)
                saved.emplace_back(Inputs[index], InputValues[index]);
        for (size_t index = 0; index < UnresolvedInputs.size(); ++index)
            saved.emplace_back(UnresolvedInputs[index], UnresolvedInputValues[index]);

        std::vector<InputPin> refreshedInputs;
        std::vector<Value> refreshedValues;
        const auto flow = std::find_if(Inputs.begin(), Inputs.end(), [](const Pin& input) { return input.Type == PinType::Flow; });
        Pin flowPin = flow != Inputs.end() ? *flow : Pin(IDGenerator.GetNextId(), "", PinType::Flow, "Returns from this function.");
        flowPin.Identity = PortIdentity::Fixed("execute");
        refreshedInputs.push_back(std::move(flowPin));
        refreshedValues.emplace_back();

        for (const BasicFunctionDef::Input& output : pFunctionDef->outputs)
        {
            const auto existing = std::find_if(saved.begin(), saved.end(), [&](const auto& item)
            {
                return item.first.Identity.kind == PortIdentityKind::Script
                    ? PortIdentitiesMatch(item.first.Identity, PortIdentity::Script(output.id, output.persistentId)) : item.first.Name == output.name;
            });
            Pin input = existing != saved.end() ? existing->first : Pin(IDGenerator.GetNextId(), output.name.c_str(), output.type, output.description);
            Value value = existing != saved.end() ? existing->second : output.value;
            if (existing != saved.end()) saved.erase(existing);
            input.Name = output.name;
            input.Type = input.DeclaredType = output.type;
            input.Description = output.description;
            input.Identity = PortIdentity::Script(output.id, output.persistentId);
            refreshedInputs.push_back(std::move(input));
            refreshedValues.push_back(value);
        }
        Inputs = std::move(refreshedInputs);
        InputValues = std::move(refreshedValues);
        UnresolvedInputs.clear();
        UnresolvedInputValues.clear();
        for (auto& [pin, value] : saved)
        {
            UnresolvedInputs.push_back(std::move(pin));
            UnresolvedInputValues.push_back(value);
        }
    }

    BasicFunctionDefPtr pFunctionDef;
};

static NodePtr BuildReturnNode(IDGenerator& IDGenerator, const ScriptFunction& function)
{
    NodePtr node = std::make_shared<ReturnNode>(IDGenerator.GetNextId(), "Return", function.functionDef);
    node->SerializationType = "return";
    node->DefinitionId = "vlox.core.return";
    node->Description = "Returns from '" + function.functionDef->name + "'.";
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow,
        "Returns when execution reaches this pin.");
    node->Inputs.back().Identity = PortIdentity::Fixed("execute");
    node->InputValues.emplace_back(Value());

    for (int i = 0; i < function.functionDef->outputs.size(); ++i)
    {
        const BasicFunctionDef::Input& output = function.functionDef->outputs[i];
        node->Inputs.emplace_back(IDGenerator.GetNextId(), output.name.c_str(), output.type,
            output.description);
        node->Inputs.back().Identity = PortIdentity::Script(output.id, output.persistentId);
        node->InputValues.emplace_back(output.value);
    }

    return node;
}
