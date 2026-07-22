#include "constantFolder.h"

#include "../graphs/graph.h"
#include "../graphs/graphCompiler.h"
#include "../script/script.h"

#include <Compiler.h>
#include <Object.h>
#include <Vm.h>

#include <algorithm>

bool ConstantFolder::IsSafeConstant(const Value& value)
{
    // Lists and callable objects are intentionally excluded: embedding mutable
    // objects would share optimizer state between executions of the script.
    return isNil(value) || isBoolean(value) || isNumber(value) || isString(value);
}

bool ConstantFolder::Evaluate(VM& vm, const Graph& graph, const NodePtr& node, Value& result)
{
    if (!GraphUtils::IsNodeConstFoldable(graph, node))
        return false;

    vm.resetStack();
    Compiler& compiler = vm.getCompiler();
    compiler.beginCompile();
    compiler.parser.hadError = false;
    compiler.parser.panicMode = false;
    compiler.beginScope();

    GraphCompiler graphCompiler(compiler);
    graphCompiler.CompileSingle(graph, node, -1, 0,
        [&](const NodePtr& current, const Graph& currentGraph, CompilationStage stage, int portIdx)
        {
            current->Compile(graphCompiler.context, currentGraph, stage, portIdx);
        });

    const std::string resultName = "__vlox_fold_" + std::to_string(node->ID.Get());
    const Token resultToken(TokenType::VAR, resultName.c_str(), resultName.length(), 0);
    const uint32_t resultConstant = compiler.identifierConstant(resultToken);
    compiler.emitOpWithValue(OpCode::OP_DEFINE_GLOBAL, OpCode::OP_DEFINE_GLOBAL_LONG, resultConstant);
    compiler.endScope();
    ObjFunction* function = compiler.endCompiler();

    if (compiler.parser.hadError || !function)
    {
        vm.resetStack();
        return false;
    }

    vm.push(Value(function));
    ObjClosure* closure = newClosure(function);
    vm.pop();
    vm.push(Value(closure));
    if (!vm.callValue(Value(closure), 0))
    {
        vm.resetStack();
        return false;
    }

    const InterpretResult status = vm.run(0);
    ObjString* resultKey = copyString(resultName.c_str(), resultName.length());
    if (status != InterpretResult::INTERPRET_OK || !vm.globalTable().get(resultKey, &result))
    {
        vm.globalTable().remove(resultKey);
        vm.resetStack();
        return false;
    }

    const bool safe = IsSafeConstant(result);
    vm.globalTable().remove(resultKey);
    vm.resetStack();
    return safe;
}

ConstantFoldingResult ConstantFolder::Fold(VM& vm, const Script& script)
{
    ConstantFoldingResult result;
    std::vector<std::pair<const ScriptFunction*, const Graph*>> graphs;
    if (script.main)
        graphs.push_back({ script.main.get(), &script.main->Graph });
    for (const ScriptFunctionPtr& function : script.functions)
        if (function)
            graphs.push_back({ function.get(), &function->Graph });

    for (const auto& entry : graphs)
    {
        const ScriptFunction& function = *entry.first;
        const Graph& graph = *entry.second;
        const NodePtr begin = graph.FindNodeIf([](const NodePtr& node)
        {
            return node && node->Category == NodeCategory::Begin;
        });
        if (!begin || begin->Outputs.empty())
            continue;

        std::vector<NodePtr> candidates;
        GraphCompiler collector(vm.getCompiler());
        collector.CompileGraph(graph, begin, 0,
            [&](const NodePtr& node, const Graph&, CompilationStage, int)
            {
                if (node && std::find(candidates.begin(), candidates.end(), node) == candidates.end())
                    candidates.push_back(node);
            });

        for (const NodePtr& node : candidates)
        {
            if (!GraphUtils::IsNodeConstFoldable(graph, node))
                continue;

            Value value;
            if (Evaluate(vm, graph, node, value))
            {
                result.values.push_back(value);
                result.nodeIds.push_back(node->ID);
            }
            else
            {
                result.failures.push_back({ node->ID, function.ID,
                    "Pure node could not be safely evaluated at compile time; runtime evaluation will be used." });
            }
        }
    }
    return result;
}
