
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct ForInNode : public Node
{
    ForInNode(int id, const char* name)
        : Node(id, name, ImColor(255, 255, 255))
    {
        Category = NodeCategory::Flow;
    }

    // TODO: Come up with a better way of doing this!
    mutable size_t loopStart = 0;
    mutable size_t exitJump = 0;

    virtual void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        Compiler& compiler = compilerCtx.compiler;
        switch (stage)
        {
        case CompilationStage::BeginInputs:
        {
            compiler.beginScope();

            // Initialize our hidden local
            const Token iterToken(TokenType::VAR, "__iter", 6, 0);
            compiler.addLocal(iterToken, false);
            // Push the initial iterator value (0)
            compiler.emitConstant(Value(0.0));
            // Set the iter local to 0
            compiler.emitVariable(iterToken, true);

            // Initialize our hidden local range var
            const Token rangeToken(TokenType::VAR, "__range", 7, 0);
            compiler.addLocal(rangeToken, false);
            
            // Get actual value
            GraphCompiler::CompileInput(compilerCtx, graph, Inputs[1], InputValues[1]);

            compiler.emitVariable(rangeToken, true); // Set the range value
        }
        break;
        case CompilationStage::BeginOutput:
        {
            if (portIdx == 0) // Loop
            {
                const Token iterToken(TokenType::VAR, "__iter", 6, 0);
                const Token rangeToken(TokenType::VAR, "__range", 7, 0);

                loopStart = compiler.currentChunk()->code.size();

                // Condition
                compiler.namedVariable(rangeToken, false); // Load range
                compiler.namedVariable(iterToken, false);
                compiler.emitByte(OpByte(OpCode::OP_RANGE_IN_BOUNDS));

                exitJump = compiler.emitJump(OpByte(OpCode::OP_JUMP_IF_FALSE));
                compiler.emitByte(OpByte(OpCode::OP_POP));

                compiler.beginScope();

                // Set value from iterator
                compiler.namedVariable(rangeToken, false); // Load range
                compiler.namedVariable(iterToken, false); // Load iterator

                // Set the local variable value before running the statement
                compiler.emitByte(OpByte(OpCode::OP_INDEX_SUBSCR));

                GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[1]); // Set the output value
            }
            else if (portIdx == 2) // End
            {
                const Token iterToken(TokenType::VAR, "__iter", 6, 0);

                compiler.endScope();

                // Increment iterator
                compiler.namedVariable(iterToken, false); // Get iterator value
                compiler.emitByte(OpByte(OpCode::OP_INCREMENT));
                compiler.emitVariable(iterToken, true); // Set iterator value incremented
                compiler.emitByte(OpByte(OpCode::OP_POP)); // Pop iterator

                compiler.emitLoop(loopStart);

                compiler.patchJump(exitJump);
                compiler.emitByte(OpByte(OpCode::OP_POP));

                compiler.endScope();
            }
        }
        break;
        }
    }
};

static NodePtr BuildForInNode(IDGenerator& IDGenerator)
{
    NodePtr node = std::make_shared<ForInNode>(IDGenerator.GetNextId(), "For In");
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Inputs.emplace_back(IDGenerator.GetNextId(), "Value", PinType::Any);

    node->Outputs.emplace_back(IDGenerator.GetNextId(), "Loop", PinType::Flow);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "Value", PinType::Any);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "End", PinType::Flow);

    node->InputValues.emplace_back(Value());
    node->InputValues.emplace_back(Value(newList()));

    return node;
}