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

        if (inputPin.Type != PinType::Flow && !startNode->IsInputDeferred(i))
            CompileInputDependency(graph, startNode, i, callback);
    }
}

void GraphCompiler::CompileInputDependency(const Graph& graph, const NodePtr& node,
                                           int inputIndex, const Callback& callback)
{
    if (inputIndex < 0 || inputIndex >= static_cast<int>(node->Inputs.size()))
        return;

    const Pin& inputPin = node->Inputs[inputIndex];
    if (const Pin* pOutput = GraphUtils::FindConnectedOutput(graph, inputPin))
    {
        NodePtr previousNode = pOutput->Node;
        if (!previousNode || !GraphUtils::IsNodeImplicit(previousNode))
            return;

        const int constFoldIdx = context.FindConstFoldedIdx(previousNode);
        if (constFoldIdx >= 0)
        {
            callback(previousNode, graph, CompilationStage::ConstFoldedInputs,
                     constFoldIdx);
            return;
        }

        callback(previousNode, graph, CompilationStage::BeforeInput, -1);
        CompileBackwardsRecursive(graph, previousNode, -1,
                                  GraphUtils::FindNodeOutputIdx(*pOutput), callback);
        CompileDeferredInputs(graph, previousNode, -1, callback);
        callback(previousNode, graph, CompilationStage::PullOutput, -1);
    }
}

void GraphCompiler::CompileDeferredInputs(const Graph& graph, const NodePtr& node,
                                          int outputIndex, const Callback& callback)
{
    for (int inputIndex = 0;
         inputIndex < static_cast<int>(node->Inputs.size()); ++inputIndex)
    {
        if (!node->IsInputDeferred(inputIndex) ||
            !node->ShouldCompileDeferredInput(inputIndex, outputIndex))
            continue;

        callback(node, graph, CompilationStage::BeforeDeferredInput, inputIndex);
        CompileInputDependency(graph, node, inputIndex, callback);
        callback(node, graph, CompilationStage::AfterDeferredInput, inputIndex);
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
        callback(startNode, graph, CompilationStage::BeforeOutput, outputIdx);
        CompileDeferredInputs(graph, startNode, outputIdx, callback);
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
                callback(startNode, graph, CompilationStage::BeforeOutput, i);
                CompileDeferredInputs(graph, startNode, i, callback);
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
        CompileDeferredInputs(graph, startNode, -1, callback);
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
            if (HasFlag(pOutput->Node->InstanceFlags, NodeInstanceFlags::Error))
            {
                compiler.emitConstant(value);
                return;
            }

            if (pOutput->Node->Category == NodeCategory::Begin)
            {
                // Inputs from the begin node are already locals, we can access them with the input name
                
                const Token outputToken = compilerCtx.StoreTempVariable(pOutput->Name);
                compiler.emitVariable(outputToken, false);
                return;
            }

            if (pOutput->Node->Category == NodeCategory::Variable &&
                pOutput->Node->SerializationType == "variable.get")
            {
                // We can load the variable directly
                GetVariableNode* pGetVar = static_cast<GetVariableNode*>(pOutput->Node.get());

                if (pGetVar->pPropertyDef)
                {
                    Token varToken(TokenType::VAR, pGetVar->pPropertyDef->Name.c_str(), pGetVar->pPropertyDef->Name.length(), 0);
                    compiler.emitVariable(varToken, false);
                    return;
                }
            }
            
            const std::string outputName = CompilerContext::tempVarPrefix + std::to_string(pOutput->ID.Get());
            const Token outputToken = compilerCtx.StoreTempVariable(outputName);
            compiler.emitVariable(outputToken, false);
            return;
        }
    }

    compiler.emitConstant(value);
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

void GraphCompiler::CompileCallResult(CompilerContext& compilerCtx, const Graph& graph,
                                      const std::vector<Pin>& outputs, size_t dataOutputStart)
{
    Compiler& compiler = compilerCtx.compiler;
    if (dataOutputStart >= outputs.size())
    {
        compiler.emitByte(OpByte(OpCode::OP_POP));
        return;
    }

    const size_t outputCount = outputs.size() - dataOutputStart;
    if (outputCount == 1)
    {
        CompileOutput(compilerCtx, graph, outputs[dataOutputStart]);
        return;
    }

    // Functions with multiple outputs return one list. Keep that package in a hidden local while 
    // each item is copied into the local backing its pin.
    const std::string packageName =
        std::string(CompilerContext::tempVarPrefix) + "return_" +
        std::to_string(outputs[dataOutputStart].ID.Get());

    const Token packageToken = compilerCtx.StoreTempVariable(packageName);
    compiler.addLocal(packageToken, true);
    compiler.emitVariable(packageToken, true, true);

    for (size_t outputIndex = 0; outputIndex < outputCount; ++outputIndex)
    {
        compiler.emitVariable(packageToken, false);
        compiler.emitConstant(Value(static_cast<double>(outputIndex)));
        compiler.emitByte(OpByte(OpCode::OP_INDEX_SUBSCR));
        CompileOutput(compilerCtx, graph, outputs[dataOutputStart + outputIndex]);
    }
}
