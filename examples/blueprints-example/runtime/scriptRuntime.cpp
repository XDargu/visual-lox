#include "scriptRuntime.h"

#include "constantFolder.h"
#include "../graphs/graphCompiler.h"

#include <Debug.h>
#include <VMUtils.h>

#include <utility>
#include <map>
#include <mutex>

namespace
{
struct PendingExecution
{
    size_t initialStackSize = 0;
    size_t initialFrameCount = 0;
};

std::mutex& PendingExecutionMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<VM*, PendingExecution>& PendingExecutions()
{
    static std::map<VM*, PendingExecution> executions;
    return executions;
}

void EmitPropertyInitializer(CompilerContext& context, const ScriptProperty& property)
{
    Compiler& compiler = context.compiler;
    static constexpr char thisName[] = "this";
    compiler.namedVariable(Token(TokenType::THIS, thisName, 4, 0), false);
    GraphCompiler::CompileLiteral(compiler, property.defaultValue);
    const Token propertyToken(TokenType::IDENTIFIER, property.Name.c_str(), property.Name.length(), 0);
    compiler.emitOpWithValue(OpCode::OP_SET_PROPERTY, OpCode::OP_SET_PROPERTY_LONG,
                             compiler.identifierConstant(propertyToken));
    GraphCompiler::EmitVariableProbe(context, nullptr, property.PersistentId, property.Name);
    compiler.emitByte(OpByte(OpCode::OP_POP));
}

void EmitLocalInitializer(CompilerContext& context, const ScriptProperty& variable)
{
    Compiler& compiler = context.compiler;
    GraphCompiler::CompileLiteral(compiler, variable.defaultValue);
    GraphCompiler::EmitVariableProbe(context, nullptr, variable.PersistentId, variable.Name);
    const Token token(TokenType::VAR, variable.Name.c_str(), variable.Name.length(), 0);
    compiler.defineVariable(compiler.parseVariableDirectly(false, token));
}
}

void ScriptRuntime::CompileGraph(const Script& script, const ScriptFunction& function,
                                 Compiler& compiler,
                                 const std::vector<Value>& foldedValues,
                                 const std::vector<ed::NodeId>& foldedNodeIds,
                                 ScriptDebugInfo* debugInfo)
{
    const Graph& graph = function.Graph;
    const NodePtr begin = graph.FindNodeIf([](const NodePtr& node)
    {
        return node->Category == NodeCategory::Begin;
    });
    if (!begin)
        return;

    GraphCompiler graphCompiler(compiler, &script, function.ID, function.PersistentId, debugInfo);
    graphCompiler.context.constFoldingValues = foldedValues;
    graphCompiler.context.constFoldingIDs = foldedNodeIds;

    graphCompiler.CompileGraph(graph, begin, 0,
        [&](const NodePtr& node, const Graph& currentGraph, CompilationStage stage, int portIdx)
        {
            if (stage == CompilationStage::BeginInputs || (stage == CompilationStage::PullOutput && GraphUtils::IsNodeImplicit(node)))
            {
                for (int inputIndex = 0; inputIndex < static_cast<int>(node->Inputs.size()); ++inputIndex)
                {
                    const InputPin& input = node->Inputs[inputIndex];
                    const bool implicitReceiver = inputIndex == node->GetReceiverInputIndex() && !currentGraph.IsPinLinked(input.ID) && GraphUtils::UsesImplicitReceiver(script, function.ID, currentGraph, *node);

                    if (input.Type == PinType::Flow || node->IsInputDeferred(inputIndex) || implicitReceiver)
                        continue;

                    GraphCompiler::CompileInput(graphCompiler.context, currentGraph, input, input.LiteralValue);
                    compiler.emitByte(OpByte(OpCode::OP_POP));
                }
                GraphCompiler::EmitNodeProbe(graphCompiler.context, *node);
            }

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

ScriptCompileResult ScriptRuntime::Compile(VM& vm, const Script& script, const ScriptCompileOptions& options)
{
    ValidationReport validation = ScriptValidator::Validate(script);
    if (validation.HasErrors())
        return { nullptr, InterpretResult::INTERPRET_COMPILE_ERROR, std::move(validation), {}, {}, {} };

    ConstantFoldingResult folding;
    if (options.enableConstantFolding && !options.enableDebugging)
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
        return { nullptr, InterpretResult::INTERPRET_COMPILE_ERROR, std::move(validation), {}, {}, {} };

    const NodePtr mainBegin = script.main->Graph.FindNodeIf([](const NodePtr& node)
    {
        return node->Category == NodeCategory::Begin;
    });
    if (!mainBegin)
        return { nullptr, InterpretResult::INTERPRET_COMPILE_ERROR, std::move(validation), {}, {}, {} };

    vm.resetStack();
    ObjList* programArguments = newList();
    const Value programArgumentsValue(programArguments);
    vm.push(programArgumentsValue);
    for (const std::string& argument : options.programArguments)
        programArguments->append(
            Value(copyString(argument.c_str(), static_cast<int>(argument.size()))));
    for (Value& value : folding.values)
        vm.push(value);
    Compiler& compiler = vm.getCompiler();
    std::shared_ptr<ScriptDebugInfo> debugInfo = options.enableDebugging ? std::make_shared<ScriptDebugInfo>() : nullptr;
    compiler.beginCompile();
    compiler.parser.hadError = false;
    compiler.parser.panicMode = false;

    auto compileClosure = [&](const ScriptFunctionPtr& scriptFunction, FunctionType type, const ScriptClassPtr& classOwner = nullptr)
    {
        Token functionToken(TokenType::IDENTIFIER, scriptFunction->functionDef->name.c_str(),
                            scriptFunction->functionDef->name.length(), 0);
        CompilerScope functionScope(type, compiler.current, &functionToken);
        compiler.current = &functionScope;
        compiler.beginScope();
        CompilerContext debugContext(compiler, &script, scriptFunction->ID, scriptFunction->PersistentId, debugInfo.get());

        for (const BasicFunctionDef::Input& input : scriptFunction->functionDef->inputs)
        {
            const Token inputToken(TokenType::IDENTIFIER, input.name.c_str(), input.name.length(), 0);
            ++compiler.current->function->arity;
            if (compiler.current->function->arity > 255)
                compiler.errorAtCurrent("Can't have more than 255 parameters.");

            compiler.defineVariable(compiler.parseVariableDirectly(false, inputToken));
        }

        for (const ScriptPropertyPtr& variable : scriptFunction->variables)
            EmitLocalInitializer(debugContext, *variable);

        if (type == FunctionType::INITIALIZER && classOwner)
        {
            for (const ScriptPropertyPtr& property : classOwner->properties)
                EmitPropertyInitializer(debugContext, *property);
        }

        CompileGraph(script, *scriptFunction, compiler, folding.values, folding.nodeIds, debugInfo.get());

        ObjFunction* function = compiler.endCompiler();
        if (debugInfo)
        {
            debugInfo->AddFunction(function, classOwner ? classOwner->Name + "." + scriptFunction->functionDef->name : scriptFunction->functionDef->name, scriptFunction->PersistentId, scriptFunction);
        }

        const uint32_t constant = compiler.makeConstant(Value(function));
        compiler.emitOpWithValue(OpCode::OP_CLOSURE, OpCode::OP_CLOSURE_LONG, constant);

        for (int i = 0; i < function->upvalueCount; ++i)
        {
            compiler.emitByte(functionScope.upvalues[i].isLocal ? 1 : 0);
            compiler.emitByte(functionScope.upvalues[i].index);
        }
        return function;
    };

    CompilerContext globalDebugContext(compiler, &script, ScriptElementID::Invalid, {}, debugInfo.get());
    for (const ScriptPropertyPtr& property : script.variables)
    {
        GraphCompiler::CompileLiteral(compiler, property->defaultValue);
        GraphCompiler::EmitVariableProbe(globalDebugContext, nullptr, property->PersistentId, property->Name);
        const Token token(TokenType::VAR, property->Name.c_str(), property->Name.length(), 0);
        compiler.defineVariable(compiler.identifierConstant(token));
    }

    for (const ScriptFunctionPtr& scriptFunction : script.functions)
    {
        Token functionToken(TokenType::IDENTIFIER, scriptFunction->functionDef->name.c_str(),
                            scriptFunction->functionDef->name.length(), 0);
        const uint32_t global = compiler.parseVariableDirectly(false, functionToken);
        compiler.markInitialized();

        compileClosure(scriptFunction, FunctionType::FUNCTION);
        compiler.defineVariable(global);
    }

    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        Token classToken(TokenType::IDENTIFIER, scriptClass->Name.c_str(), scriptClass->Name.length(), 0);
        const uint32_t global = compiler.parseVariableDirectly(false, classToken);
        compiler.markInitialized();
        const uint32_t className = compiler.identifierConstant(classToken);
        compiler.emitOpWithValue(OpCode::OP_CLASS, OpCode::OP_CLASS_LONG, className);
        compiler.defineVariable(global);
        compiler.namedVariable(classToken, false);

        for (const ScriptFunctionPtr& method : scriptClass->methods)
        {
            compileClosure(method, FunctionType::METHOD, scriptClass);
            const Token methodToken(TokenType::IDENTIFIER, method->functionDef->name.c_str(),
                                    method->functionDef->name.length(), 0);
            compiler.emitOpWithValue(OpCode::OP_METHOD, OpCode::OP_METHOD_LONG,
                                     compiler.identifierConstant(methodToken));
        }

        if (scriptClass->constructor || !scriptClass->properties.empty())
        {
            ScriptFunctionPtr constructor = scriptClass->constructor;
            if (!constructor)
                constructor = std::make_shared<ScriptFunction>(ScriptElementID::Invalid, "init");
            compileClosure(constructor, FunctionType::INITIALIZER, scriptClass);
            const Token initToken(TokenType::IDENTIFIER, "init", 4, 0);
            compiler.emitOpWithValue(OpCode::OP_METHOD, OpCode::OP_METHOD_LONG,
                                     compiler.identifierConstant(initToken));
        }
        compiler.emitByte(OpByte(OpCode::OP_POP));
    }

    compiler.beginScope();
    const Token argumentsToken(TokenType::IDENTIFIER, "Arguments", 9, 0);
    GraphCompiler::CompileLiteral(compiler, programArgumentsValue);
    compiler.addLocal(argumentsToken, true);
    compiler.emitVariable(argumentsToken, true, true);

    for (const ScriptPropertyPtr& variable : script.main->variables)
    {
        CompilerContext mainDebugContext(compiler, &script, script.main->ID, script.main->PersistentId, debugInfo.get());
        EmitLocalInitializer(mainDebugContext, *variable);
    }

    CompileGraph(script, *script.main, compiler, folding.values, folding.nodeIds, debugInfo.get());
    compiler.endScope();
    ObjFunction* function = compiler.endCompiler();

    if (debugInfo)
    {
        debugInfo->AddFunction(function, script.main->functionDef->name, script.main->PersistentId, script.main);
    }

    if (compiler.parser.hadError)
    {
        vm.resetStack();
        return { nullptr, InterpretResult::INTERPRET_COMPILE_ERROR, std::move(validation),
                 std::move(folding.values), std::move(folding.nodeIds), std::move(debugInfo) };
    }

    if (options.disassemble)
        disassembleChunk(function->chunk, function->name ? function->name->chars.c_str() : "<script>");
    vm.resetStack();

    return { function, InterpretResult::INTERPRET_OK, std::move(validation), std::move(folding.values), std::move(folding.nodeIds), std::move(debugInfo) };
}

InterpretResult ScriptRuntime::Execute(VM& vm, ObjFunction* function)
{
    if (!function)
        return InterpretResult::INTERPRET_COMPILE_ERROR;

    vm.clearStopRequest();
    vm.resetStack();
    vm.push(Value(function));
    ObjClosure* closure = newClosure(function);
    vm.pop();
    vm.push(Value(closure));
    if (!vm.callValue(Value(closure), 0))
        return InterpretResult::INTERPRET_RUNTIME_ERROR;

    const InterpretResult result = vm.run(0);
    if (result == InterpretResult::INTERPRET_PAUSED)
    {
        std::lock_guard<std::mutex> lock(PendingExecutionMutex());
        PendingExecutions()[&vm] = { 0, 0 };
        return result;
    }
    if (result == InterpretResult::INTERPRET_OK)
        vm.pop();
    return result;
}

InterpretResult ScriptRuntime::Call(VM& vm, const Value& callable,
                                    const std::vector<Value>& arguments)
{
    if (!isCallable(callable) || arguments.size() > 255)
        return InterpretResult::INTERPRET_RUNTIME_ERROR;

    const size_t initialStackSize = vm.getStackSize();
    const size_t initialFrameCount = vm.getFrameCount();
    vm.push(callable);
    for (const Value& argument : arguments)
        vm.push(argument);

    if (!vm.callValue(callable, static_cast<uint8_t>(arguments.size())))
        return InterpretResult::INTERPRET_RUNTIME_ERROR;

    InterpretResult result = InterpretResult::INTERPRET_OK;
    if (vm.getFrameCount() > initialFrameCount)
        result = vm.run(static_cast<int>(initialFrameCount));

    if (result == InterpretResult::INTERPRET_PAUSED)
    {
        std::lock_guard<std::mutex> lock(PendingExecutionMutex());
        PendingExecutions()[&vm] = { initialStackSize, initialFrameCount };
        return result;
    }

    if (result == InterpretResult::INTERPRET_OK)
        while (vm.getStackSize() > initialStackSize)
            vm.pop();
    return result;
}

InterpretResult ScriptRuntime::CallGlobal(VM& vm, const std::string& name,
                                          const std::vector<Value>& arguments)
{
    Value callable;
    ObjString* globalName = copyString(name.c_str(), static_cast<int>(name.size()));
    if (!vm.globalTable().get(globalName, &callable))
        return InterpretResult::INTERPRET_RUNTIME_ERROR;
    return Call(vm, callable, arguments);
}

InterpretResult ScriptRuntime::Run(VM& vm, const Script& script,
                                   const ScriptCompileOptions& options)
{
    const ScriptCompileResult compiled = Compile(vm, script, options);
    return compiled ? Execute(vm, compiled.function) : InterpretResult::INTERPRET_COMPILE_ERROR;
}

bool ScriptRuntime::HasPausedExecution(VM& vm)
{
    std::lock_guard<std::mutex> lock(PendingExecutionMutex());
    return PendingExecutions().count(&vm) != 0;
}

InterpretResult ScriptRuntime::Resume(VM& vm)
{
    PendingExecution pending;
    {
        std::lock_guard<std::mutex> lock(PendingExecutionMutex());
        const auto found = PendingExecutions().find(&vm);
        if (found == PendingExecutions().end())
            return InterpretResult::INTERPRET_RUNTIME_ERROR;
        pending = found->second;
        PendingExecutions().erase(found);
    }

    const InterpretResult result = vm.run(static_cast<int>(pending.initialFrameCount));
    if (result == InterpretResult::INTERPRET_PAUSED)
    {
        std::lock_guard<std::mutex> lock(PendingExecutionMutex());
        PendingExecutions()[&vm] = pending;
        return result;
    }

    if (result == InterpretResult::INTERPRET_OK)
        while (vm.getStackSize() > pending.initialStackSize)
            vm.pop();
    return result;
}

void ScriptRuntime::AbandonPausedExecution(VM& vm)
{
    std::lock_guard<std::mutex> lock(PendingExecutionMutex());
    PendingExecutions().erase(&vm);
    vm.resetStack();
}
