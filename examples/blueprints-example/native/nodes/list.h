
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct ListGetByIndex : public Node
{
    ListGetByIndex(int id, const char* name)
        : Node(id, name, ImColor(255, 255, 255))
    {
        Category = NodeCategory::Branch;
    }

    static constexpr int TruePort = 0;
    static constexpr int FalsePort = 1;

    // TODO: Come up with a better way of doing this!
    mutable size_t thenJump = 0;
    mutable size_t elseJump = 0;

    virtual void Compile(Compiler& compiler, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
        case CompilationStage::BeginInput:
        {
            CompileInputs(compiler, graph);
        }
        break;
        }
    }

    void CompileInputs(Compiler& compiler, const Graph& graph) const
    {
        GraphCompiler::CompileInput(compiler, graph, Inputs[1], InputValues[1]);
        GraphCompiler::CompileInput(compiler, graph, Inputs[2], InputValues[2]);
        compiler.emitByte(OpByte(OpCode::OP_INDEX_SUBSCR));

        GraphCompiler::CompileOutput(compiler, graph, Outputs[1]);
    }
};

static NodePtr BuildListGetByIndexNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<ListGetByIndex>(IDGenerator.GetNextId(), "Get By Index");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "List", PinType::List);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "Index", PinType::Float);

    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "Value", PinType::Any);

    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value(newList()));
    node->InputValues.emplace_back(Value(0.0));
    return node;
}