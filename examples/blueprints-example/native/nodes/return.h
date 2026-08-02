
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
                GraphCompiler::CompileInput(compilerCtx, graph, Inputs[1], Inputs[1].LiteralValue);
                compiler.emitByte(OpByte(OpCode::OP_RETURN));
                break;
            }

            compiler.emitByte(OpByte(OpCode::OP_BUILD_LIST));
            for (size_t outputIndex = 0; outputIndex < outputCount; ++outputIndex)
            {
                const size_t inputIndex = outputIndex + 1;
                GraphCompiler::CompileInput(
                    compilerCtx, graph, Inputs[inputIndex], Inputs[inputIndex].LiteralValue);
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
        std::vector<InputPin> saved;
        std::copy_if(Inputs.begin(), Inputs.end(), std::back_inserter(saved), [](const InputPin& input) { return input.Type != PinType::Flow; });
        saved.insert(saved.end(), UnresolvedInputs.begin(), UnresolvedInputs.end());

        std::vector<InputPin> refreshedInputs;
        const auto flow = std::find_if(Inputs.begin(), Inputs.end(), [](const Pin& input) { return input.Type == PinType::Flow; });
        InputPin flowPin = flow != Inputs.end() ? *flow : InputPin(Pin(IDGenerator.GetNextId(), "", PinType::Flow, "Returns from this function."));
        flowPin.Identity = PortIdentity::Fixed("execute");
        refreshedInputs.push_back(std::move(flowPin));

        for (const BasicFunctionDef::Input& output : pFunctionDef->outputs)
        {
            const auto existing = std::find_if(saved.begin(), saved.end(), [&](const InputPin& input)
            {
                return PortIdentitiesMatch(input.Identity, PortIdentity::Script(output.persistentId));
            });
            InputPin input = existing != saved.end() ? *existing : InputPin(Pin(IDGenerator.GetNextId(), output.name.c_str(), output.type, output.description), output.value);
            if (existing != saved.end()) saved.erase(existing);
            input.Name = output.name;
            input.Type = input.DeclaredType = output.type;
            input.Description = output.description;
            input.Identity = PortIdentity::Script(output.persistentId);
            refreshedInputs.push_back(std::move(input));
        }
        Inputs = std::move(refreshedInputs);
        UnresolvedInputs = std::move(saved);
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

    for (int i = 0; i < function.functionDef->outputs.size(); ++i)
    {
        const BasicFunctionDef::Input& output = function.functionDef->outputs[i];
        node->Inputs.emplace_back(IDGenerator.GetNextId(), output.name.c_str(), output.type,
            output.description);
        node->Inputs.back().Identity = PortIdentity::Script(output.persistentId);
        node->Inputs.back().LiteralValue = output.value;
    }

    return node;
}
