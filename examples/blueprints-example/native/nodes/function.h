
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct CallFunctionNode : public Node
{
    CallFunctionNode(int id, const char* name, const char* functionName)
        : Node(id, name, ImColor(255, 128, 128))
        , FunctionName(functionName)
    {
        Category = NodeCategory::Function;
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

        // Load named variable (native func)
        compiler.namedVariable(Token(TokenType::STRING, FunctionName.c_str(), FunctionName.length(), 10), false);

        int argCount = 0;
        // Gather Inputs normally
        for (int i = 0; i < Inputs.size(); ++i)
        {
            if (Inputs[i].Type != PinType::Flow)
            {
                GraphCompiler::CompileInput(compilerCtx, graph, Inputs[i], InputValues[i]);
                argCount++;
            }
        }

        if (HasFlag(Flags, NodeFlags::DynamicInputs))
        {
            // Get all inputs on a list first
            compiler.emitByte(OpByte(OpCode::OP_BUILD_LIST));
            compiler.emitByte(argCount);

            // We will only call the function with the list!
            argCount = 1;
        }

        compiler.emitBytes(OpByte(OpCode::OP_CALL), argCount);

        // TODO: Assume no output for now
        if (false /*pFunctionDef->outputs.size() > 0*/)
        {
            // Set the output variable
            const int dataOutputIdx = GraphUtils::IsNodeImplicit(this) ? 0 : 1;
            GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[dataOutputIdx]);
        }
    }

    std::string FunctionName;

    // TODO: Do this later!
    //ScriptFunction* pFunctionDef;
};

// TODO: Inputs!
static NodePtr BuildCallFunctionNode(IDGenerator& IDGenerator, const char* functionName)
{
    NodePtr node = std::make_shared<CallFunctionNode>(IDGenerator.GetNextId(), functionName, functionName);

    node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);
    node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow);

    return node;
}