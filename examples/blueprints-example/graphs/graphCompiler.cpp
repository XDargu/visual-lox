#pragma once

#include "graphCompiler.h"

#include "graph.h"
#include "node.h"
#include "link.h"

#include "../native/nodes/variable.h"

#include <Compiler.h>
#include <Vm.h>

std::list<std::string> CompilerContext::tempVarStorage;

void GraphCompiler::CompileGraph(const Graph& graph, const NodePtr& startNode, int outputIdx, const Callback& callback)
{
    context.tempVarStorage.clear();
    callback(startNode, graph, CompilationStage::BeginSequence, 0);
    CompileRecursive(graph, startNode, -1, outputIdx, callback);
    callback(startNode, graph, CompilationStage::EndSequence, 0);
}

void GraphCompiler::CompileBackwardsRecursive(const Graph& graph, const NodePtr& startNode, int inputIdx, int outputIdx, const Callback& callback)
{
    for (int i = 0; i < startNode->Inputs.size(); ++i)
    {
        const Pin& inputPin = startNode->Inputs[i];

        if (inputPin.Type != PinType::Flow)
        {
            if (const Pin* pOutput = GraphUtils::FindConnectedOutput(graph, inputPin))
            {
                NodePtr prevNode = pOutput->Node;
                if (prevNode && GraphUtils::IsNodeImplicit(prevNode))
                {
                    const int constFoldIdx = context.FindConstFoldedIdx(prevNode);
                    if (constFoldIdx >= 0)
                    {
                        callback(prevNode, graph, CompilationStage::ConstFoldedInputs, constFoldIdx);
                    }
                    else
                    {
                        callback(prevNode, graph, CompilationStage::BeforeInput, -1);
                        const int nodeOutputIdx = GraphUtils::FindNodeOutputIdx(*pOutput);
                        CompileBackwardsRecursive(graph, prevNode, -1, nodeOutputIdx, callback);

                        callback(prevNode, graph, CompilationStage::PullOutput, -1);
                    }
                }
            }
        }
    }
}

void GraphCompiler::CompileRecursive(const Graph& graph, const NodePtr& startNode, int inputIdx, int outputIdx, const Callback& callback)
{
    const int constFoldIdx = context.FindConstFoldedIdx(startNode);
    if (constFoldIdx >= 0)
    {
        callback(startNode, graph, CompilationStage::ConstFoldedInputs, constFoldIdx);
    }
    else
    {
        callback(startNode, graph, CompilationStage::BeginNode, -1);
        callback(startNode, graph, CompilationStage::BeforeInput, inputIdx);
        CompileBackwardsRecursive(graph, startNode, inputIdx, outputIdx, callback);

        callback(startNode, graph, CompilationStage::BeginInputs, inputIdx);
    }

    if (outputIdx != -1)
    {
        // Compile one specific output. We assume it's a flow output.
        callback(startNode, graph, CompilationStage::BeginOutput, outputIdx);

        const Pin& currentOutput = startNode->Outputs[outputIdx];
        const std::vector<const Pin*> inputPins = GraphUtils::FindConnectedInputs(graph, currentOutput);

        for (const Pin* pNextInput : inputPins)
        {
            const int nodeInputIdx = GraphUtils::FindNodeInputIdx(*pNextInput);
            CompileRecursive(graph, pNextInput->Node, nodeInputIdx, -1, callback);
        }

        callback(startNode, graph, CompilationStage::EndOutput, outputIdx);
    }
    else
    {
        // Compile all outputs
        for (int i = 0; i < startNode->Outputs.size(); ++i)
        {
            const Pin& outputPin = startNode->Outputs[i];

            if (outputPin.Type == PinType::Flow)
            {
                callback(startNode, graph, CompilationStage::BeginOutput, i);

                const std::vector<const Pin*> inputPins = GraphUtils::FindConnectedInputs(graph, outputPin);

                for (const Pin* pNextInput : inputPins)
                {
                    const int nodeInputIdx = GraphUtils::FindNodeInputIdx(*pNextInput);
                    CompileRecursive(graph, pNextInput->Node, nodeInputIdx, -1, callback);
                }

                callback(startNode, graph, CompilationStage::EndOutput, i);
            }
        }
    }

    if (constFoldIdx < 0)
        callback(startNode, graph, CompilationStage::EndInputs, inputIdx);
}

void GraphCompiler::CompileSingle(const Graph& graph, const NodePtr& startNode, int inputIdx, int outputIdx, const Callback& callback)
{
    context.tempVarStorage.clear();
    if (GraphUtils::IsNodeImplicit(startNode))
    {
        CompileBackwardsRecursive(graph, startNode, -1, outputIdx, callback);
        callback(startNode, graph, CompilationStage::PullOutput, outputIdx);
    }
    else
    {
        CompileRecursive(graph, startNode, inputIdx, -1, callback);
    }
}

void GraphCompiler::RegisterNatives(VM& vm)
{
    
}

void GraphCompiler::CompileInput(CompilerContext& compilerCtx, const Graph& graph, const Pin& input, const Value& value)
{
    Compiler& compiler = compilerCtx.compiler;

    if (graph.IsPinLinked(input.ID))
    {
        if (const Pin* pOutput = GraphUtils::FindConnectedOutput(graph, input))
        {
            if (HasFlag(pOutput->Node->Flags, NodeFlags::Error))
            {
                compiler.emitConstant(value);
                return;
            }

            if (pOutput->Node->Category == NodeCategory::Begin)
            {
                // Inputs from the begin node are already locals, we can access them with the input name
                
                const Token outputToken = compilerCtx.StoreTempVariable(pOutput->Name);
                compiler.emitVariable(outputToken, false);
            }
            else if (pOutput->Node->Category == NodeCategory::Variable)
            {
                // We can load the variable directly
                GetVariableNode* pGetVar = static_cast<GetVariableNode*>(pOutput->Node.get());
                Token varToken(TokenType::VAR, pGetVar->VariableName.c_str(), pGetVar->VariableName.length(), 0);
                compiler.emitVariable(varToken, false);
            }
            else
            {
                const std::string outputName = CompilerContext::tempVarPrefix + std::to_string(pOutput->ID.Get());
                const Token outputToken = compilerCtx.StoreTempVariable(outputName);
                compiler.emitVariable(outputToken, false);
            }
        }
    }
    else
    {
        compiler.emitConstant(value);
    }
}

void GraphCompiler::CompileOutput(CompilerContext& compilerCtx, const Graph& graph, const Pin& output)
{
    Compiler& compiler = compilerCtx.compiler;

    const std::string outputName = CompilerContext::tempVarPrefix + std::to_string(output.ID.Get());
    const Token outputToken = compilerCtx.StoreTempVariable(outputName);

    compiler.addLocal(outputToken, true);
    compiler.emitVariable(outputToken, true, true);

    // Swap the local variable for this to use globals instead. Much nicer for debugging!
    //const uint32_t constant = compiler.identifierConstant(outputToken);
    //compiler.defineVariable(constant);
}