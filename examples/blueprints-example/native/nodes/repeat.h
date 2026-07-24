#pragma once

#include "../../graphs/graphCompiler.h"
#include "../../graphs/idgeneration.h"

#include <Compiler.h>

struct RepeatNode : public Node
{
    RepeatNode(int id)
        : Node(id, "Repeat", ImColor(255, 255, 255))
    {
        Category = NodeCategory::Flow;
    }

    Token IteratorToken(CompilerContext& context) const
    {
        return context.StoreTempVariable(
            "__repeat_iter_" + std::to_string(ID.Get()));
    }

    Token CountToken(CompilerContext& context) const
    {
        return context.StoreTempVariable(
            "__repeat_count_" + std::to_string(ID.Get()));
    }

    void Compile(CompilerContext& context, const Graph& graph,
                 CompilationStage stage, int portIdx) const override
    {
        Compiler& compiler = context.compiler;
        const Token iterator = IteratorToken(context);
        const Token count = CountToken(context);
        if (stage == CompilationStage::BeginInputs)
        {
            compiler.beginScope();
            compiler.addLocal(iterator, false);
            compiler.emitConstant(Value(0.0));
            compiler.emitVariable(iterator, true);
            compiler.addLocal(count, true);
            GraphCompiler::CompileInput(context, graph, Inputs[1], InputValues[1]);
            compiler.emitVariable(count, true, true);
        }
        else if (stage == CompilationStage::BeginOutput && portIdx == 0)
        {
            loopStart = compiler.currentChunk()->code.size();
            compiler.emitVariable(iterator, false);
            compiler.emitVariable(count, false);
            compiler.emitByte(OpByte(OpCode::OP_LESS));
            exitJump = compiler.emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
            compiler.emitByte(OpByte(OpCode::OP_POP));
            compiler.beginScope();
            compiler.emitVariable(iterator, false);
            GraphCompiler::CompileOutput(context, graph, Outputs[1]);
        }
        else if (stage == CompilationStage::BeginOutput && portIdx == 2)
        {
            compiler.endScope();
            compiler.emitVariable(iterator, false);
            compiler.emitByte(OpByte(OpCode::OP_INCREMENT));
            compiler.emitVariable(iterator, true);
            compiler.emitByte(OpByte(OpCode::OP_POP));
            compiler.emitLoop(loopStart);
            compiler.patchJump(exitJump);
            compiler.emitByte(OpByte(OpCode::OP_POP));
            compiler.endScope();
        }
    }

    mutable size_t loopStart = 0;
    mutable size_t exitJump = 0;
};

inline NodePtr BuildRepeatNode(IDGenerator& ids)
{
    NodePtr node = std::make_shared<RepeatNode>(ids.GetNextId());
    node->Inputs.emplace_back(ids.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(ids.GetNextId(), "Count", PinType::Float);
    node->Outputs.emplace_back(ids.GetNextId(), "Body", PinType::Flow);
    node->Outputs.emplace_back(ids.GetNextId(), "Index", PinType::Float);
    node->Outputs.emplace_back(ids.GetNextId(), "Completed", PinType::Flow);
    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value(1.0));
    return node;
}
