
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct BranchNode : public Node
{
    BranchNode(int id, const char* name)
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
            case CompilationStage::BeginOutput:
            {
                if (portIdx == TruePort)
                {
                    thenJump = compiler.emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
                    compiler.emitByte(OpByte(OpCode::OP_POP));

                    compiler.beginScope();
                }
                else if (portIdx == FalsePort)
                {
                    elseJump = compiler.emitJump(OpByte(OpCode::OP_JUMP));
                    compiler.patchJump(thenJump);
                    compiler.emitByte(OpByte(OpCode::OP_POP));

                    compiler.beginScope();
                }
            }
            break;
            case CompilationStage::EndOutput:
            {
                if (portIdx == TruePort)
                {
                    compiler.endScope();
                }
                else if (portIdx == FalsePort)
                {
                    compiler.endScope();
                }
            }
            break;
            case CompilationStage::EndInput:
            {
                compiler.patchJump(elseJump);
            }
            break;
        }
    }

    void CompileInputs(Compiler& compiler, const Graph& graph) const
    {
        GraphCompiler::CompileInput(compiler, graph, Inputs[1], InputValues[1]);
    }
};

static NodePtr BuildBranchNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<BranchNode>(IDGenerator.GetNextId(), "Branch");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "Condition", PinType::Bool);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "True", PinType::Flow);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "False", PinType::Flow);

    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value(false));
    return node;
}