
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
            GraphCompiler::CompileInput(compilerCtx, graph, Inputs[1], InputValues[1]);
            compiler.emitByte(OpByte(OpCode::OP_RETURN));
        }
        break;
        }
    }

    virtual void Refresh(const Script& script, IDGenerator& IDGenerator) override
    {
        // Add missing inputs
        const int startingInput = 1;

        for (int i = 0; i < pFunctionDef->outputs.size(); ++i)
        {
            const BasicFunctionDef::Input& output = pFunctionDef->outputs[i];

            if (Pin* existingInput = FindInputByName(output.name))
            {
                existingInput->Type = TypeOfValue(output.value);
            }
            else
            {
                Inputs.insert(Inputs.begin() + startingInput + i, { IDGenerator.GetNextId(), output.name.c_str(), TypeOfValue(output.value) });
            }
        }

        // Redo input values
        InputValues.resize(pFunctionDef->inputs.size() + startingInput);

        InputValues[0] = Value(); // Flow node

        for (int i = 0; i < pFunctionDef->inputs.size(); ++i)
        {
            InputValues[i + startingInput] = pFunctionDef->inputs[i].value;
        }

        // Remove outputs that are no longer there
        stl::erase_if(Inputs, [&](const Pin& input)
        {
            return input.Type != PinType::Flow && pFunctionDef->FindOutputByName(input.Name) == nullptr;
        });
    }

    BasicFunctionDefPtr pFunctionDef;
};

static NodePtr BuildReturnNode(IDGenerator& IDGenerator, const ScriptFunction& function)
{
    NodePtr node = std::make_shared<ReturnNode>(IDGenerator.GetNextId(), "Return", function.functionDef);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->InputValues.emplace_back(Value());

    for (int i = 0; i < function.functionDef->outputs.size(); ++i)
    {
        const BasicFunctionDef::Input& output = function.functionDef->outputs[i];
        node->Inputs.emplace_back(IDGenerator.GetNextId(), output.name.c_str(), TypeOfValue(output.value));
        node->InputValues.emplace_back(output.value);
    }

    return node;
}
