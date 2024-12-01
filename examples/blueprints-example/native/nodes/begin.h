
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
        // Add missing outputs
        const int startingOutput = 1;

        for (int i = 0; i < functionDef->inputs.size(); ++i)
        {
            const BasicFunctionDef::Input& input = functionDef->inputs[i];

            if (Pin* existingOutput = FindOutputByName(input.name))
            {
                existingOutput->Type = TypeOfValue(input.value);
            }
            else
            {
                Outputs.insert(Outputs.begin() + startingOutput + i, { IDGenerator.GetNextId(), input.name.c_str(), TypeOfValue(input.value) });
            }
        }

        // Remove outputs that are no longer there
        stl::erase_if(Outputs, [&](const Pin& output)
        {
            return output.Type != PinType::Flow && functionDef->FindInputByName(output.Name) == nullptr;
        });
    }

    BasicFunctionDefPtr functionDef;
};

static NodePtr BuildBeginNode(IDGenerator& IDGenerator, const ScriptFunctionPtr& function)
{
    NodePtr node = std::make_shared<BeginNode>(IDGenerator.GetNextId(), "Begin", function->functionDef);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);

    for (int i = 0; i < function->functionDef->inputs.size(); ++i)
    {
        const BasicFunctionDef::Input& input = function->functionDef->inputs[i];
        node->Outputs.emplace_back(IDGenerator.GetNextId(),input.name.c_str(), TypeOfValue(input.value));
    }

    return node;
}