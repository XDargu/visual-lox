#pragma once

#include "graphCompiler.h"

#include "graph.h"
#include "node.h"
#include "link.h"

#include <Compiler.h>
#include <Vm.h>

std::vector<NodePtr> GraphCompiler::CompileGraph(Compiler& compiler, Graph& graph, NodePtr startNode, int outputIdx)
{
    tempVarStorage.clear();
    std::vector<NodePtr> processedNodes;
    startNode->Compile(compiler, graph, CompilationStage::BeginSequence, 0);
    CompileRecursive(compiler, graph, startNode, -1, outputIdx, processedNodes);
    startNode->Compile(compiler, graph, CompilationStage::EndSequence, 0);
    return processedNodes;
}

std::list<std::string> GraphCompiler::tempVarStorage;

void GraphCompiler::CompileBackwardsRecursive(Compiler& compiler, Graph& graph, NodePtr startNode, int inputIdx, int outputIdx, std::vector<NodePtr>& processedNodes)
{
    if (std::find(processedNodes.begin(), processedNodes.end(), startNode) == processedNodes.end())
    {
        processedNodes.push_back(startNode);
    }

    for (int i = 0; i < startNode->Inputs.size(); ++i)
    {
        const Pin& inputPin = startNode->Inputs[i];

        if (inputPin.Type != PinType::Flow)
        {
            const std::vector<const Pin*> outputs = GraphUtils::FindConnectedOutputs(graph, inputPin);

            for (const Pin* pOutput : outputs)
            {
                NodePtr prevNode = pOutput->Node;
                if (prevNode && GraphUtils::IsNodeImplicit(prevNode))
                {
                    const int nodeOutputIdx = GraphUtils::FindNodeOutputIdx(*pOutput);
                    CompileBackwardsRecursive(compiler, graph, prevNode, -1, nodeOutputIdx, processedNodes);

                    prevNode->Compile(compiler, graph, CompilationStage::PullOutput, nodeOutputIdx);
                }
            }
        }
    }
}

void GraphCompiler::CompileRecursive(Compiler& compiler, Graph& graph, NodePtr startNode, int inputIdx, int outputIdx, std::vector<NodePtr>& processedNodes)
{
    startNode->Compile(compiler, graph, CompilationStage::BeginNode, -1);
    startNode->Compile(compiler, graph, CompilationStage::BeforeInput, inputIdx);
    CompileBackwardsRecursive(compiler, graph, startNode, inputIdx, outputIdx, processedNodes);

    startNode->Compile(compiler, graph, CompilationStage::BeginInput, inputIdx);

    if (std::find(processedNodes.begin(), processedNodes.end(), startNode) == processedNodes.end())
    {
        processedNodes.push_back(startNode);
    }

    if (outputIdx != -1)
    {
        startNode->Compile(compiler, graph, CompilationStage::BeginOutput, outputIdx);

        const Pin& currentOutput = startNode->Outputs[outputIdx];
        const std::vector<const Pin*> inputPins = GraphUtils::FindConnectedInputs(graph, currentOutput);

        for (const Pin* pNextInput : inputPins)
        {
            const int nodeInputIdx = GraphUtils::FindNodeInputIdx(*pNextInput);
            CompileRecursive(compiler, graph, pNextInput->Node, nodeInputIdx, -1, processedNodes);
        }

        startNode->Compile(compiler, graph, CompilationStage::EndOutput, outputIdx);
    }
    else
    {
        for (int i = 0; i < startNode->Outputs.size(); ++i)
        {
            const Pin& outputPin = startNode->Outputs[i];

            if (outputPin.Type == PinType::Flow)
            {
                startNode->Compile(compiler, graph, CompilationStage::BeginOutput, i);

                const std::vector<const Pin*> inputPins = GraphUtils::FindConnectedInputs(graph, outputPin);

                for (const Pin* pNextInput : inputPins)
                {
                    const int nodeInputIdx = GraphUtils::FindNodeInputIdx(*pNextInput);
                    CompileRecursive(compiler, graph, pNextInput->Node, nodeInputIdx, -1, processedNodes);
                }

                startNode->Compile(compiler, graph, CompilationStage::EndOutput, i);
            }
        }
    }
    startNode->Compile(compiler, graph, CompilationStage::EndInput, inputIdx);
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
