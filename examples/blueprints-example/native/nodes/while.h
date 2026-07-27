#pragma once

#include "../../graphs/graphCompiler.h"
#include "../../graphs/idgeneration.h"

#include <Compiler.h>

struct WhileNode : public Node
{
    WhileNode(int id)
        : Node(id, "While", ImColor(255, 255, 255))
    {
        Category = NodeCategory::Flow;
    }

    bool IsInputDeferred(int inputIndex) const override
    {
        return inputIndex == 1;
    }

    bool ShouldCompileDeferredInput(int inputIndex, int outputIndex) const override
    {
        return inputIndex == 1 && outputIndex == 0;
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int portIdx) const override
    {
        Compiler& compiler = context.compiler;
        if (stage == CompilationStage::BeforeOutput && portIdx == 0)
        {
            loopStart = compiler.currentChunk()->code.size();
            conditionLocalStart = compiler.current->localCount;
            compiler.beginScope();
        }
        else if (stage == CompilationStage::BeginOutput && portIdx == 0)
        {
            conditionLocalCount =
                compiler.current->localCount - conditionLocalStart;
            GraphCompiler::CompileInput(
                context, graph, Inputs[1], InputValues[1]);
            exitJump = compiler.emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
            compiler.emitByte(OpByte(OpCode::OP_POP));
            compiler.beginScope();
        }
        else if (stage == CompilationStage::EndOutput && portIdx == 0)
        {
            compiler.endScope();
            compiler.endScope();
            compiler.emitLoop(loopStart);
            compiler.patchJump(exitJump);
            compiler.emitByte(OpByte(OpCode::OP_POP));
            for (int i = 0; i < conditionLocalCount; ++i)
                compiler.emitByte(OpByte(OpCode::OP_POP));
        }
    }

    mutable size_t loopStart = 0;
    mutable size_t exitJump = 0;
    mutable int conditionLocalStart = 0;
    mutable int conditionLocalCount = 0;
};

inline NodePtr BuildWhileNode(IDGenerator& ids)
{
    NodePtr node = std::make_shared<WhileNode>(ids.GetNextId());
    node->Inputs.emplace_back(
        ids.GetNextId(), "", PinType::Flow,
        "Starts evaluating the loop.");
    node->Inputs.emplace_back(
        ids.GetNextId(), "Condition", PinType::Bool,
        "Evaluated before each iteration; the loop stops when it is false.");
    node->Outputs.emplace_back(
        ids.GetNextId(), "Body", PinType::Flow,
        "Runs once for each iteration while Condition is true.");
    node->Outputs.emplace_back(
        ids.GetNextId(), "Completed", PinType::Flow,
        "Runs after Condition becomes false.");
    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value(false));
    return node;
}
