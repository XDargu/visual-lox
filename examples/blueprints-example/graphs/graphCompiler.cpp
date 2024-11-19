#pragma once

#include "graphCompiler.h"

#include "graph.h"
#include "node.h"
#include "link.h"

#include <Compiler.h>
#include <Vm.h>

void GraphCompiler::CompileGraph(Compiler& compiler, Graph& graph, NodePtr startNode, int outputIdx)
{
    std::vector<NodePtr> processedNodes;
    startNode->Compile(compiler, graph, CompilationStage::BeginSequence, 0);
    CompileRecursive(compiler, graph, startNode, -1, outputIdx, processedNodes);
    startNode->Compile(compiler, graph, CompilationStage::BeginSequence, 0);
}

void GraphCompiler::CompileBackwardsRecursive(Compiler& compiler, Graph& graph, NodePtr startNode, int inputIdx, int outputIdx, std::vector<NodePtr>& processedNodes)
{
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
            const Token outputToken(TokenType::VAR, outputName.c_str(), outputName.length(), 0);
            compiler.namedVariable(outputToken, false);
        }
    }
    else
    {
        compiler.emitConstant(value);
    }
}

void GraphCompiler::CompileOutput(Compiler& compiler, const Graph& graph, const Pin& output)
{
    const std::string outputName = "__lv__" + std::to_string(output.ID.Get());
    const Token outputToken(TokenType::VAR, outputName.c_str(), outputName.length(), 0);

    const uint32_t constant = compiler.identifierConstant(outputToken);
    compiler.defineVariable(constant);
}
