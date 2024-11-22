#pragma once

#include "graphCompiler.h"

#include "graph.h"
#include "node.h"
#include "link.h"

#include <Compiler.h>
#include <Vm.h>

void GraphCompiler::CompileGraph(Compiler& compiler, Graph& graph, const NodePtr& startNode, int outputIdx, const Callback& callback)
{
    tempVarStorage.clear();
    callback(startNode, compiler, graph, CompilationStage::BeginSequence, 0);
    CompileRecursive(compiler, graph, startNode, -1, outputIdx, callback);
    callback(startNode, compiler, graph, CompilationStage::EndSequence, 0);
}

std::list<std::string> GraphCompiler::tempVarStorage;

void GraphCompiler::CompileBackwardsRecursive(Compiler& compiler, Graph& graph, const NodePtr& startNode, int inputIdx, int outputIdx, const Callback& callback)
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
                    const auto foldIt = std::find(m_constFoldingIDs.begin(), m_constFoldingIDs.end(), prevNode->ID);
                    const bool shouldFold = foldIt != m_constFoldingIDs.end();

                    if (shouldFold)
                    {
                        const size_t index = std::distance(m_constFoldingIDs.begin(), foldIt);
                        compiler.emitConstant(m_constFoldingValues[index]);
                        CompileOutput(compiler, graph, prevNode->Outputs[0]);
                    }
                    else
                    {
                        const int nodeOutputIdx = GraphUtils::FindNodeOutputIdx(*pOutput);
                        CompileBackwardsRecursive(compiler, graph, prevNode, -1, nodeOutputIdx, callback);

                        callback(prevNode, compiler, graph, CompilationStage::PullOutput, -1);
                    }
                }
            }
        }
    }
}

void GraphCompiler::CompileRecursive(Compiler& compiler, Graph& graph, const NodePtr& startNode, int inputIdx, int outputIdx, const Callback& callback)
{
    const auto foldIt = std::find(m_constFoldingIDs.begin(), m_constFoldingIDs.end(), startNode->ID);
    const bool shouldFold = foldIt != m_constFoldingIDs.end();

    if (shouldFold)
    {
        const size_t index = std::distance(m_constFoldingIDs.begin(), foldIt);
        compiler.emitConstant(m_constFoldingValues[index]);
        const int outputIdx = GraphUtils::IsNodeImplicit(startNode) ? 0 : 1;
        CompileOutput(compiler, graph, startNode->Outputs[outputIdx]);
    }
    else
    {
        callback(startNode, compiler, graph, CompilationStage::BeginNode, -1);
        callback(startNode, compiler, graph, CompilationStage::BeforeInput, inputIdx);
        CompileBackwardsRecursive(compiler, graph, startNode, inputIdx, outputIdx, callback);

        callback(startNode, compiler, graph, CompilationStage::BeginInputs, inputIdx);
    }

    if (outputIdx != -1)
    {
        // Compile one specific output. We assume it's a flow output.
        callback(startNode, compiler, graph, CompilationStage::BeginOutput, outputIdx);

        const Pin& currentOutput = startNode->Outputs[outputIdx];
        const std::vector<const Pin*> inputPins = GraphUtils::FindConnectedInputs(graph, currentOutput);

        for (const Pin* pNextInput : inputPins)
        {
            const int nodeInputIdx = GraphUtils::FindNodeInputIdx(*pNextInput);
            CompileRecursive(compiler, graph, pNextInput->Node, nodeInputIdx, -1, callback);
        }

        callback(startNode, compiler, graph, CompilationStage::EndOutput, outputIdx);
    }
    else
    {
        // Compile all outputs
        for (int i = 0; i < startNode->Outputs.size(); ++i)
        {
            const Pin& outputPin = startNode->Outputs[i];

            if (outputPin.Type == PinType::Flow)
            {
                callback(startNode, compiler, graph, CompilationStage::BeginOutput, i);

                const std::vector<const Pin*> inputPins = GraphUtils::FindConnectedInputs(graph, outputPin);

                for (const Pin* pNextInput : inputPins)
                {
                    const int nodeInputIdx = GraphUtils::FindNodeInputIdx(*pNextInput);
                    CompileRecursive(compiler, graph, pNextInput->Node, nodeInputIdx, -1, callback);
                }

                callback(startNode, compiler, graph, CompilationStage::EndOutput, i);
            }
        }
    }

    if (!shouldFold)
        callback(startNode, compiler, graph, CompilationStage::EndInputs, inputIdx);
}

void GraphCompiler::CompileSingle(Compiler& compiler, Graph& graph, const NodePtr& startNode, int inputIdx, int outputIdx, const Callback& callback)
{
    tempVarStorage.clear();

    if (GraphUtils::IsNodeImplicit(startNode))
    {
        CompileBackwardsRecursive(compiler, graph, startNode, -1, outputIdx, callback);
        callback(startNode, compiler, graph, CompilationStage::PullOutput, outputIdx);
    }
    else
    {
        CompileRecursive(compiler, graph, startNode, inputIdx, -1, callback);
    }
}

void GraphCompiler::RegisterNatives(VM& vm)
{
    
}

void GraphCompiler::CompileInput(Compiler& compiler, const Graph& graph, const Pin& input, const Value& value)
{
    if (graph.IsPinLinked(input.ID))
    {
        if (const Pin* pOutput = GraphUtils::FindConnectedOutput(graph, input))
        {
            const std::string outputName = tempVarPrefix + std::to_string(pOutput->ID.Get());
            const Token outputToken = StoreTempVariable(outputName);
            compiler.emitVariable(outputToken, false);
        }
    }
    else
    {
        compiler.emitConstant(value);
    }
}

void GraphCompiler::CompileOutput(Compiler& compiler, const Graph& graph, const Pin& output)
{
    const std::string outputName = tempVarPrefix + std::to_string(output.ID.Get());
    const Token outputToken = StoreTempVariable(outputName);

    compiler.addLocal(outputToken, true);
    compiler.emitVariable(outputToken, true, true);

    // Swap the local variable for this to use globals instead. Much nicer for debugging!
    //const uint32_t constant = compiler.identifierConstant(outputToken);
    //compiler.defineVariable(constant);
}

Token GraphCompiler::StoreTempVariable(const std::string& name)
{
    tempVarStorage.push_back(name);
    return Token(TokenType::VAR, tempVarStorage.back().c_str(), name.length(), 0);
}
