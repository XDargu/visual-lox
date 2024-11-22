
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
        : Node(id, name, ImColor(255, 128, 128))
    {
        Category = NodeCategory::Function;
        Flags |= NodeFlags::CanConstFold;
    }

    virtual void Compile(Compiler& compiler, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
        case CompilationStage::BeginInputs:
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


struct ListSetByIndex : public Node
{
    ListSetByIndex(int id, const char* name)
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
            CompileInputs(compiler, graph);
        }
        break;
        }
    }

    void CompileInputs(Compiler& compiler, const Graph& graph) const
    {
        GraphCompiler::CompileInput(compiler, graph, Inputs[1], InputValues[1]);
        GraphCompiler::CompileInput(compiler, graph, Inputs[2], InputValues[2]);
        GraphCompiler::CompileInput(compiler, graph, Inputs[3], InputValues[3]);
        compiler.emitByte(OpByte(OpCode::OP_STORE_SUBSCR));

        GraphCompiler::CompileOutput(compiler, graph, Outputs[1]);
    }
};

static NodePtr BuildListSetByIndexNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<ListSetByIndex>(IDGenerator.GetNextId(), "Set By Index");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "List", PinType::List);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "Index", PinType::Float);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "Value", PinType::Any);

    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "Value", PinType::List);

    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value(newList()));
    node->InputValues.emplace_back(Value(0.0));
    node->InputValues.emplace_back(Value());
    return node;
}