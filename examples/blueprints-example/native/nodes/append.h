
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"
#include "../../script/script.h"
#include "variadic.h"

#include "../../utilities/utils.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct AppendNode : public VariadicInputNode
{
    AppendNode(int id, const char* name)
        : VariadicInputNode(id, name, ImColor(255, 128, 128))
    {
        Category = NodeCategory::Function;
        DefinitionFlags |= NodeDefinitionFlags::DynamicInputs | NodeDefinitionFlags::Pure;
    }

    virtual void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
        case CompilationStage::BeginInputs:
        {
            if (!GraphUtils::IsNodeImplicit(this))
                CompileInputs(compilerCtx, graph);
        }
        break;
        case CompilationStage::PullOutput:
        {
            if (GraphUtils::IsNodeImplicit(this))
                CompileInputs(compilerCtx, graph);
        }
        break;
        }
    }

    void CompileInputs(CompilerContext& compilerCtx, const Graph& graph) const
    {
        Compiler& compiler = compilerCtx.compiler;

        GraphCompiler::CompileInput(compilerCtx, graph, Inputs[0], Inputs[0].LiteralValue);

        for (int i = 1; i < Inputs.size(); ++i)
        {
            GraphCompiler::CompileInput(compilerCtx, graph, Inputs[i], Inputs[i].LiteralValue);
            compiler.emitByte(OpByte(OpCode::OP_ADD));
        }

        GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[0]);
    }

};

static NodePtr CreateAppendNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<AppendNode>(IDGenerator.GetNextId(), "Append");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "A", PinType::Any);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "B", PinType::Any);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "Result", PinType::String);

    node->Inputs[0].LiteralValue = Value(copyString("", 0));
    node->Inputs[1].LiteralValue = Value(copyString("", 0));
    return node;
}
