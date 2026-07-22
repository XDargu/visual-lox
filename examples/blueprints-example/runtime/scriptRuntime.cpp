#include "scriptRuntime.h"

#include "constantFolder.h"
#include "../graphs/graphCompiler.h"

#include <Debug.h>

#include <utility>

void ScriptRuntime::CompileGraph(const Graph& graph, Compiler& compiler,
                                 const std::vector<Value>& foldedValues,
                                 const std::vector<ed::NodeId>& foldedNodeIds)
{
    const NodePtr begin = graph.FindNodeIf([](const NodePtr& node)
    {
        return node->Category == NodeCategory::Begin;
    });
    if (!begin)
        return;

    GraphCompiler graphCompiler(compiler);
    graphCompiler.context.constFoldingValues = foldedValues;
    graphCompiler.context.constFoldingIDs = foldedNodeIds;

    graphCompiler.CompileGraph(graph, begin, 0,
        [&](const NodePtr& node, const Graph& currentGraph, CompilationStage stage, int portIdx)
        {
            if (stage == CompilationStage::ConstFoldedInputs)
            {
                compiler.emitConstant(foldedValues[portIdx]);
                GraphCompiler::CompileOutput(graphCompiler.context, currentGraph, node->Outputs[0]);
            }
            else
            {
                node->Compile(graphCompiler.context, currentGraph, stage, portIdx);
            }
        });
}

ScriptCompileResult ScriptRuntime::Compile(VM& vm, const Script& script,
                                           const ScriptCompileOptions& options)
{
    ValidationReport validation = ScriptValidator::Validate(script);
    if (validation.HasErrors())
        return { nullptr, InterpretResult::INTERPRET_COMPILE_ERROR, std::move(validation), {}, {} };

    ConstantFoldingResult folding;
    if (options.enableConstantFolding)
    {
        const bool wasGcAllowed = vm.isGarbageCollectionAllowed();
        vm.allowGarbageCollection(false);
        folding = ConstantFolder::Fold(vm, script);
        vm.allowGarbageCollection(wasGcAllowed);

        for (const ConstantFoldingFailure& failure : folding.failures)
        {
            std::string graphName = "<graph>";
            if (script.main && script.main->ID.id == failure.functionId.id)
                graphName = script.main->functionDef->name;
            else
                for (const ScriptFunctionPtr& function : script.functions)
                    if (function && function->ID.id == failure.functionId.id)
                        graphName = function->functionDef->name;
            validation.diagnostics.push_back({ DiagnosticSeverity::Warning, "constant-fold-skipped",
                failure.message, graphName, failure.functionId, failure.nodeId });
        }
    }

    if (!script.main)
        return { nullptr, InterpretResult::INTERPRET_COMPILE_ERROR, std::move(validation), {}, {} };

    const NodePtr mainBegin = script.main->Graph.FindNodeIf([](const NodePtr& node)
    {
        return node->Category == NodeCategory::Begin;
    });
    if (!mainBegin)
        return { nullptr, InterpretResult::INTERPRET_COMPILE_ERROR, std::move(validation), {}, {} };

    vm.resetStack();
    for (Value& value : folding.values)
        vm.push(value);
    Compiler& compiler = vm.getCompiler();
    compiler.beginCompile();
    compiler.parser.hadError = false;
    compiler.parser.panicMode = false;

    for (const ScriptPropertyPtr& property : script.variables)
    {
        compiler.emitConstant(property->defaultValue);
        const Token token(TokenType::VAR, property->Name.c_str(), property->Name.length(), 0);
        compiler.defineVariable(compiler.identifierConstant(token));
    }

    for (const ScriptFunctionPtr& scriptFunction : script.functions)
    {
        Token functionToken(TokenType::IDENTIFIER, scriptFunction->functionDef->name.c_str(),
                            scriptFunction->functionDef->name.length(), 0);
        const uint32_t global = compiler.parseVariableDirectly(false, functionToken);
        compiler.markInitialized();

        CompilerScope functionScope(FunctionType::FUNCTION, compiler.current, &functionToken);
        compiler.current = &functionScope;
        compiler.beginScope();

        for (const BasicFunctionDef::Input& input : scriptFunction->functionDef->inputs)
        {
            const Token inputToken(TokenType::IDENTIFIER, input.name.c_str(), input.name.length(), 0);
            ++compiler.current->function->arity;
            if (compiler.current->function->arity > 255)
                compiler.errorAtCurrent("Can't have more than 255 parameters.");
            compiler.defineVariable(compiler.parseVariableDirectly(false, inputToken));
        }

        CompileGraph(scriptFunction->Graph, compiler, folding.values, folding.nodeIds);
        ObjFunction* function = compiler.endCompiler();
        const uint32_t constant = compiler.makeConstant(Value(function));
        compiler.emitOpWithValue(OpCode::OP_CLOSURE, OpCode::OP_CLOSURE_LONG, constant);
        for (int i = 0; i < function->upvalueCount; ++i)
        {
            compiler.emitByte(functionScope.upvalues[i].isLocal ? 1 : 0);
            compiler.emitByte(functionScope.upvalues[i].index);
        }
        compiler.defineVariable(global);
    }

    compiler.beginScope();
    CompileGraph(script.main->Graph, compiler, folding.values, folding.nodeIds);
    compiler.endScope();
    ObjFunction* function = compiler.endCompiler();

    if (compiler.parser.hadError)
    {
        vm.resetStack();
        return { nullptr, InterpretResult::INTERPRET_COMPILE_ERROR, std::move(validation),
                 std::move(folding.values), std::move(folding.nodeIds) };
    }
    if (options.disassemble)
        disassembleChunk(function->chunk, function->name ? function->name->chars.c_str() : "<script>");
    vm.resetStack();
    return { function, InterpretResult::INTERPRET_OK, std::move(validation),
             std::move(folding.values), std::move(folding.nodeIds) };
}

InterpretResult ScriptRuntime::Execute(VM& vm, ObjFunction* function)
{
    if (!function)
        return InterpretResult::INTERPRET_COMPILE_ERROR;

    vm.resetStack();
    vm.push(Value(function));
    ObjClosure* closure = newClosure(function);
    vm.pop();
    vm.push(Value(closure));
    if (!vm.callValue(Value(closure), 0))
        return InterpretResult::INTERPRET_RUNTIME_ERROR;

    const InterpretResult result = vm.run(0);
    if (result == InterpretResult::INTERPRET_OK)
        vm.pop();
    return result;
}

InterpretResult ScriptRuntime::Run(VM& vm, const Script& script,
                                   const ScriptCompileOptions& options)
{
    const ScriptCompileResult compiled = Compile(vm, script, options);
    return compiled ? Execute(vm, compiled.function) : InterpretResult::INTERPRET_COMPILE_ERROR;
}
