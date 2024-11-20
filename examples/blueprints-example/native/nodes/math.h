
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct AddNode : public Node
{
    AddNode(int id, const char* name)
        : Node(id, name, ImColor(230, 230, 0))
    {
        Category = NodeCategory::Function;
    }

    virtual void Compile(Compiler& compiler, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
        case CompilationStage::BeginInput:
        {
            if (!GraphUtils::IsNodeImplicit(this))
                CompileInputs(compiler, graph);
        }
        break;
        case CompilationStage::PullOutput:
        {
            if (GraphUtils::IsNodeImplicit(this))
                CompileInputs(compiler, graph);
        }
        break;
        }
    }

    void CompileInputs(Compiler& compiler, const Graph& graph) const
    {
        GraphCompiler::CompileInput(compiler, graph, Inputs[0], InputValues[0]);
        GraphCompiler::CompileInput(compiler, graph, Inputs[1], InputValues[1]);
        compiler.emitByte(OpByte(OpCode::OP_ADD));

        GraphCompiler::CompileOutput(compiler, graph, Outputs[0]);
    }
};

static NodePtr CreateAddNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<AddNode>(IDGenerator.GetNextId(), "Add");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "A", PinType::Float);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "B", PinType::Float);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "Result", PinType::Float);

    node->InputValues.emplace_back(Value(0.0f));
    node->InputValues.emplace_back(Value(0.0f));
    return node;
}