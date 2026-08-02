#pragma once

#include "../../graphs/graph.h"
#include "../../graphs/graphCompiler.h"
#include "../../graphs/idgeneration.h"
#include "../../graphs/node.h"

#include <Compiler.h>
#include <Vm.h>

struct MapForEachNode : public Node
{
    MapForEachNode(int id, const char* name)
        : Node(id, name, ImColor(45, 170, 190))
    {
        Category = NodeCategory::Flow;
    }

    mutable size_t loopStart = 0;
    mutable size_t exitJump = 0;

    void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        Compiler& compiler = compilerCtx.compiler;
        const Token iterToken(TokenType::VAR, "__map_iter", 10, 0);
        const Token mapToken(TokenType::VAR, "__map", 5, 0);

        if (stage == CompilationStage::BeginInputs)
        {
            compiler.beginScope();
            compiler.addLocal(iterToken, false);
            compiler.emitConstant(Value(0.0));
            compiler.emitVariable(iterToken, true);
            compiler.addLocal(mapToken, false);
            GraphCompiler::CompileInput(compilerCtx, graph, Inputs[1], Inputs[1].LiteralValue);
            compiler.emitVariable(mapToken, true);
            return;
        }

        if (stage != CompilationStage::BeginOutput)
            return;
        if (portIdx == 0)
        {
            loopStart = compiler.currentChunk()->code.size();
            compiler.namedVariable(mapToken, false);
            compiler.namedVariable(iterToken, false);
            compiler.emitByte(OpByte(OpCode::OP_MAP_IN_BOUNDS));
            exitJump = compiler.emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
            compiler.emitByte(OpByte(OpCode::OP_POP));
            compiler.beginScope();

            compiler.namedVariable(mapToken, false);
            compiler.namedVariable(iterToken, false);
            compiler.emitByte(OpByte(OpCode::OP_MAP_KEY_AT));
            GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[1]);

            compiler.namedVariable(mapToken, false);
            compiler.namedVariable(iterToken, false);
            compiler.emitByte(OpByte(OpCode::OP_MAP_VALUE_AT));
            GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[2]);
        }
        else if (portIdx == 3)
        {
            compiler.endScope();
            compiler.namedVariable(iterToken, false);
            compiler.emitByte(OpByte(OpCode::OP_INCREMENT));
            compiler.emitVariable(iterToken, true);
            compiler.emitByte(OpByte(OpCode::OP_POP));
            compiler.emitLoop(loopStart);
            compiler.patchJump(exitJump);
            compiler.emitByte(OpByte(OpCode::OP_POP));
            compiler.endScope();
        }
    }
};

inline NodePtr BuildMapForEachNode(IDGenerator& ids)
{
    NodePtr node = std::make_shared<MapForEachNode>(ids.GetNextId(), "For Each");
    const TypeRef key = TypeRef::Variable("K");
    const TypeRef value = TypeRef::Variable("V");
    node->Inputs.emplace_back(ids.GetNextId(), "", PinType::Flow, "Starts the loop.");
    node->Inputs.emplace_back(ids.GetNextId(), "Map", TypeRef::Map(key, value), "The map to iterate.");
    node->Outputs.emplace_back(ids.GetNextId(), "Loop", PinType::Flow, "Executes once for each map entry.");
    node->Outputs.emplace_back(ids.GetNextId(), "Key", key, "The key at the current iteration.");
    node->Outputs.emplace_back(ids.GetNextId(), "Value", value, "The value at the current iteration.");
    node->Outputs.emplace_back(ids.GetNextId(), "End", PinType::Flow, "Executes after iteration finishes.");
    node->Inputs[1].LiteralValue = Value(newMap());
    return node;
}
