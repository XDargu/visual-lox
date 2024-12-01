
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

struct AppendNode : public Node
{
    AppendNode(int id, const char* name)
        : Node(id, name, ImColor(255, 128, 128))
    {
        Category = NodeCategory::Function;
        Flags |= NodeFlags::DynamicInputs;
        Flags |= NodeFlags::CanConstFold;
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

        GraphCompiler::CompileInput(compilerCtx, graph, Inputs[0], InputValues[0]);

        for (int i = 1; i < Inputs.size(); ++i)
        {
            GraphCompiler::CompileInput(compilerCtx, graph, Inputs[i], InputValues[i]);
            compiler.emitByte(OpByte(OpCode::OP_ADD));
        }

        GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[0]);
    }

    virtual void AddInput(IDGenerator& IDGenerator) override
    {
        const char asciiChar = char(65 + Inputs.size());
        Inputs.emplace_back(IDGenerator.GetNextId(), std::string(1, asciiChar).c_str(), PinType::Any);
        InputValues.emplace_back(Value(copyString("", 0)));
    };

    virtual void RemoveInput(ed::PinId pinId) override
    {
        const int inputIdx = GraphUtils::FindNodeInputIdx(this, pinId);
        if (inputIdx != -1)
        {
            Inputs.erase(Inputs.begin() + inputIdx);
            InputValues.erase(InputValues.begin() + inputIdx);

            // Rename inputs!
            for (int i = 1; i < Inputs.size(); ++i)
            {
                Inputs[i].Name = GetInputName(i);
            }
        }
    };

    virtual bool CanRemoveInput(ed::PinId pinId) const override { return Inputs.size() > 2; };
    virtual bool CanAddInput() const override { return Inputs.size() < 16; };

    static std::string GetInputName(int inputIdx) { return std::string(1, char(65 + inputIdx)); }
};

static NodePtr CreateAppendNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<AppendNode>(IDGenerator.GetNextId(), "Append");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "A", PinType::Any);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "B", PinType::Any);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "Result", PinType::String);

    node->InputValues.emplace_back(Value(copyString("", 0)));
    node->InputValues.emplace_back(Value(copyString("", 0)));
    return node;
}