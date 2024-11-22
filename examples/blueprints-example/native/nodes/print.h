
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct PrintNode : public Node
{
    PrintNode(int id, const char* name)
        : Node(id, name, ImColor(255, 128, 128))
    {
        Category = NodeCategory::Function;
    }

    virtual void Compile(Compiler& compiler, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
        case CompilationStage::BeginInputs:
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
        GraphCompiler::CompileInput(compiler, graph, Inputs[1], InputValues[1]);
        compiler.emitByte(OpByte(OpCode::OP_PRINT));
    }
};

static NodePtr BuildPrintNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<PrintNode>(IDGenerator.GetNextId(), "Print");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "Content", PinType::Any);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);

    // TODO: These values will be garbage collected! We will need to mark them somewhow
    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value(copyString("", 0)));
    return node;
}