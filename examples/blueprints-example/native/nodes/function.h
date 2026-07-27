
# pragma once

#include "../../graphs/node.h"
#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"

#include "../../graphs/graphCompiler.h"

#include "../../script/script.h"

#include <Compiler.h>
#include <Vm.h>

namespace ed = ax::NodeEditor;

struct GetFunctionNode : public Node
{
    GetFunctionNode(int id, const char* name, BasicFunctionDefPtr pFunctionDef, const ScriptElementID funcID)
        : Node(id, name, ImColor(255, 128, 128))
        , pFunctionDef(pFunctionDef)
    {
        Category = NodeCategory::Function;
        Type = NodeType::SimpleGet;
        refId = funcID;
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
        if (pFunctionDef)
        {
            Compiler& compiler = compilerCtx.compiler;

            Token varToken(TokenType::VAR, pFunctionDef->name.c_str(), pFunctionDef->name.length(), 0);
            compiler.emitVariable(varToken, false);

            GraphCompiler::CompileOutput(compilerCtx, graph, Outputs[0]);
        }
    }

    void Refresh(const Script& script, IDGenerator& IDGenerator) override
    {
        InstanceFlags = ClearFlag(InstanceFlags, NodeInstanceFlags::Error);

        RefreshDefinition(script);

        if (!pFunctionDef)
        {
            InstanceFlags |= NodeInstanceFlags::Error;
            Error = "Missing function with ID: " + std::to_string(refId);
            return;
        }

        Outputs[0].Name = pFunctionDef->name;
        Outputs[0].Description = pFunctionDef->description;
        Description = "Gets function '" + pFunctionDef->name + "' as a typed value. " +
            pFunctionDef->description;
        std::vector<TypeRef> inputs;
        std::vector<TypeRef> outputs;
        for (const auto& input : pFunctionDef->inputs) inputs.push_back(input.type);
        for (const auto& output : pFunctionDef->outputs) outputs.push_back(output.type);
        Outputs[0].Type = Outputs[0].DeclaredType =
            TypeRef::Function(std::move(inputs), std::move(outputs));
    }

    void RefreshDefinition(const Script& script)
    {
        const bool isNative = !refId.IsValid();

        if (!isNative)
        {
            if (ScriptFunctionPtr pFun = ScriptUtils::FindFunctionById(script, refId))
            {
                pFunctionDef = pFun->functionDef;
            }
            else
            {
                pFunctionDef = nullptr;
            }
        }
    }

    BasicFunctionDefPtr pFunctionDef = nullptr;
};

static NodePtr BuildGetFunctionNode(IDGenerator& IDGenerator, const BasicFunctionDefPtr& pFunctionDef, ScriptElementID funcID)
{
    NodePtr node = std::make_shared<GetFunctionNode>(IDGenerator.GetNextId(), "", pFunctionDef, funcID);
    node->SerializationType = "function.get";
    if (!funcID.IsValid() && pFunctionDef)
        node->DefinitionId = pFunctionDef->name;
    if (pFunctionDef)
    {
        node->DefinitionFlags |= NodeDefinitionFlags::Pure;
        node->Description = "Gets function '" + pFunctionDef->name +
            "' as a typed value. " + pFunctionDef->description;
        std::vector<TypeRef> inputs;
        std::vector<TypeRef> outputs;
        for (const auto& input : pFunctionDef->inputs) inputs.push_back(input.type);
        for (const auto& output : pFunctionDef->outputs) outputs.push_back(output.type);
        node->Outputs.emplace_back(IDGenerator.GetNextId(), pFunctionDef->name.c_str(),
            TypeRef::Function(std::move(inputs), std::move(outputs)),
            pFunctionDef->description);
    }

    return node;
}
