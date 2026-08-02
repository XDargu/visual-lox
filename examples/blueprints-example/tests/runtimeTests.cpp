#include "runtimeTests.h"

#include "testFramework.h"
#include "../graphs/idgeneration.h"
#include "../graphs/nodeRegistry.h"
#include "../native/nodes/begin.h"
#include "../native/nodes/object.h"
#include "../native/nodes/return.h"
#include "../native/nodes/variable.h"
#include "../operations/documentOperations.h"
#include "../runtime/scriptRuntime.h"
#include "../runtime/standardLibrary.h"
#include "../script/script.h"
#include "../script/scriptSerializer.h"
#include "../validation/scriptValidator.h"

#include <Vm.h>
#include <Natives.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
using Tests::Require;

void AttachNode(Graph& graph, const NodePtr& node)
{
    NodeUtils::BuildNode(node);
    graph.AddNode(node);
}

bool HasCode(const ValidationReport& report, const char* code)
{
    return std::any_of(report.diagnostics.begin(), report.diagnostics.end(),
        [&](const ValidationDiagnostic& diagnostic) { return diagnostic.code == code; });
}

std::string MakeListLiteralSource(int itemCount)
{
    std::string source = "var boundaryList = [";
    for (int i = 0; i < itemCount; ++i)
    {
        if (i > 0)
            source += ", ";
        source += std::to_string(i);
    }
    source += "];";
    return source;
}

struct RuntimeFixture
{
    RuntimeFixture()
        : vm(VM::getInstance())
        , wasGcAllowed(vm.isGarbageCollectionAllowed())
    {
        vm.allowGarbageCollection(false);
        RegisterStandardLibrary(registry);
        registry.RegisterNatives(vm);
    }

    ~RuntimeFixture()
    {
        ClearStandardLibraryTimers(vm);
        vm.setExternalMarkingFunc([]() {});
        vm.allowGarbageCollection(wasGcAllowed);
    }

    Value CallCollectionNode(const char* name, std::vector<Value> arguments)
    {
        const NativeFunctionDef* definition = registry.FindNative(name);
        Require(definition != nullptr, "Expected collection node to be registered.");
        Require(HasFlag(definition->functionDef->flags, NodeDefinitionFlags::ReadOnly) &&
                HasFlag(definition->functionDef->flags, NodeDefinitionFlags::Pure),
                "Collection query nodes should be read-only and pure.");
        NodePtr node = definition->functionDef->MakeNode(
            ids, ScriptElementID::Invalid);
        Require(node && node->DefinitionId == definition->functionDef->id,
                "Collection query definitions should construct callable nodes.");
        return definition->nativeFun(static_cast<int>(arguments.size()), arguments.data(), &vm);
    }

    Value CallNative(const char* name, std::vector<Value> arguments)
    {
        const NativeFunctionDef* definition = registry.FindNative(name);
        Require(definition != nullptr, "Expected native node to be registered.");
        return definition->nativeFun(
            static_cast<int>(arguments.size()), arguments.data(), &vm);
    }

    VM& vm;
    bool wasGcAllowed;
    NodeRegistry registry;
    IDGenerator ids;
};

Value StringValue(const std::string& text)
{
    return Value(copyString(text.c_str(), static_cast<int>(text.size())));
}

Value IsEvenTestNative(int, Value* args, VM*)
{
    return Value(isNumber(args[0]) && static_cast<int>(asNumber(args[0])) % 2 == 0);
}

Value ParityTestNative(int, Value* args, VM*)
{
    return StringValue(isNumber(args[0]) && static_cast<int>(asNumber(args[0])) % 2 == 0 ? "even" : "odd");
}

int timerCallbackCount = 0;

Value TimerCallbackTestNative(int, Value*, VM*)
{
    ++timerCallbackCount;
    return Value();
}

Value ReadGlobal(VM& vm, const char* name)
{
    Value value;
    const std::string key(name);
    Require(vm.globalTable().get(
                copyString(key.c_str(), static_cast<int>(key.size())), &value),
            "Expected a script global to exist.");
    return value;
}

ObjList* MakeList(std::initializer_list<Value> values)
{
    ObjList* list = newList();
    for (const Value& value : values)
        list->append(value);
    return list;
}

NodePtr BuildFailingExpressionNode(IDGenerator& ids, PinType outputType)
{
    struct FailingExpressionNode : Node
    {
        explicit FailingExpressionNode(int id)
            : Node(id, "Must Not Execute", ImColor(230, 230, 0))
        {
            Category = NodeCategory::Function;
        }

        void Compile(CompilerContext& context, const Graph& graph,
                     CompilationStage stage, int) const override
        {
            if (stage != CompilationStage::PullOutput)
                return;
            context.compiler.emitConstant(StringValue("not a number"));
            context.compiler.emitByte(OpByte(OpCode::OP_NEGATE));
            GraphCompiler::CompileOutput(context, graph, Outputs[0]);
        }
    };

    NodePtr node =
        std::make_shared<FailingExpressionNode>(ids.GetNextId());
    node->SerializationType = "test.failing-expression";
    node->Outputs.emplace_back(ids.GetNextId(), "Result", outputType);
    return node;
}

void StandardLibraryDeclaresCapabilities()
{
    RuntimeFixture fixture;
    const CompiledNodeDefPtr add = fixture.registry.FindCompiled("Math::Add");
    const CompiledNodeDefPtr print = fixture.registry.FindCompiled("Debug::Print");
    Require(add && HasFlag(add->functionDef->flags, NodeDefinitionFlags::Pure),
            "Math::Add should be declared pure.");
    const NativeFunctionDef* timerAfter = fixture.registry.FindNative("Timer::After");
    const NativeFunctionDef* timerEvery = fixture.registry.FindNative("Timer::Every");
    Require(timerAfter && timerEvery && !fixture.registry.FindNative("Timer::Start") && !fixture.registry.FindNative("Timer::Poll"),
            "Callback timers should replace the polling timer API.");
    const NodePtr timerAfterNode = timerAfter->functionDef->MakeNode(fixture.ids, ScriptElementID::Invalid);
    Require(timerAfterNode->Inputs.size() == 3 && timerAfterNode->Inputs.back().Name == "Callback" && timerAfterNode->Inputs.back().Type == TypeRef::Function({}, {}),
            "Timer::After should expose an explicit zero-argument callback pin.");
    const NativeFunctionDef* jsonParse = fixture.registry.FindNative("JSON::Parse");
    const NativeFunctionDef* jsonStringify = fixture.registry.FindNative("JSON::Stringify");
    const NativeFunctionDef* jsonAsObject = fixture.registry.FindNative("JSON::As Object");
    const TypeRef jsonValue = TypeRef::Object("JsonValue");
    Require(jsonParse && jsonStringify && jsonAsObject &&
            jsonParse->functionDef->MakeNode(fixture.ids, ScriptElementID::Invalid)->Outputs[0].Type == jsonValue &&
            jsonStringify->functionDef->MakeNode(fixture.ids, ScriptElementID::Invalid)->Inputs[0].Type == jsonValue &&
            jsonAsObject->functionDef->MakeNode(fixture.ids, ScriptElementID::Invalid)->Outputs[0].Type == TypeRef::Map(PinType::String, jsonValue),
            "JSON nodes should expose JsonValue and typed object pins instead of implicit Any values.");
    for (const char* name : {
            "String::Append", "Math::Add", "Math::Subtract", "Math::Multiply",
            "Math::Min", "Math::Max", "Logic::And", "Logic::Or" })
    {
        const CompiledNodeDefPtr definition = fixture.registry.FindCompiled(name);
        Require(definition && HasFlag(definition->functionDef->flags, NodeDefinitionFlags::DynamicInputs),
                "Variadic compiled expressions should declare dynamic inputs.");
        NodePtr node = definition->MakeNode(fixture.ids);
        Require(node->Inputs.size() == 2 && node->CanAddInput(),
                "Variadic compiled expressions should start with two inputs.");
        node->AddInput(fixture.ids);
        Require(node->Inputs.size() == 3, "Adding a variadic expression input should preserve the input layout.");
    }

    NodePtr addNode = add->MakeNode(fixture.ids);
    addNode->AddInput(fixture.ids);
    Require(addNode->Inputs[2].Type == PinType::Float && isNumber(addNode->Inputs[2].LiteralValue) && asNumber(addNode->Inputs[2].LiteralValue) == 0.0,
            "Math::Add should use zero as the identity default for additional inputs.");
    while (addNode->CanAddInput())
        addNode->AddInput(fixture.ids);
    Require(addNode->Inputs.size() == 16 && !addNode->CanAddInput() && !addNode->IsValidDynamicInputCount(17),
            "Math::Add should enforce its maximum input count.");
    while (addNode->Inputs.size() > 2)
        addNode->RemoveInput(addNode->Inputs.back().ID);
    Require(!addNode->CanRemoveInput(addNode->Inputs[0].ID) && !addNode->IsValidDynamicInputCount(1),
            "Math::Add should enforce its minimum input count.");
    NodePtr multiplyNode = fixture.registry.FindCompiled("Math::Multiply")->MakeNode(fixture.ids);
    multiplyNode->AddInput(fixture.ids);
    Require(multiplyNode->Inputs[2].Type == PinType::Float && isNumber(multiplyNode->Inputs[2].LiteralValue) && asNumber(multiplyNode->Inputs[2].LiteralValue) == 1.0,
            "Math::Multiply should use one as the identity default for additional inputs.");
    NodePtr andNode = fixture.registry.FindCompiled("Logic::And")->MakeNode(fixture.ids);
    andNode->AddInput(fixture.ids);
    Require(andNode->Inputs[2].Type == PinType::Bool && isBoolean(andNode->Inputs[2].LiteralValue) && asBoolean(andNode->Inputs[2].LiteralValue),
            "Logic::And should use true as the identity default for additional inputs.");
    NodePtr orNode = fixture.registry.FindCompiled("Logic::Or")->MakeNode(fixture.ids);
    orNode->AddInput(fixture.ids);
    Require(orNode->Inputs[2].Type == PinType::Bool && isBoolean(orNode->Inputs[2].LiteralValue) && !asBoolean(orNode->Inputs[2].LiteralValue),
            "Logic::Or should use false as the identity default for additional inputs.");
    NodePtr appendNode = fixture.registry.FindCompiled("String::Append")->MakeNode(fixture.ids);
    appendNode->AddInput(fixture.ids);
    Require(appendNode->Inputs[2].Type == PinType::Any && isString(appendNode->Inputs[2].LiteralValue) && asString(appendNode->Inputs[2].LiteralValue)->length == 0,
            "String::Append should use empty text as the identity default for additional inputs.");
    Require(print && !HasFlag(print->functionDef->flags, NodeDefinitionFlags::Pure),
            "Debug::Print should be declared impure.");
    for (const char* controlFlowName : {
            "Flow::Branch", "Flow::For In", "Flow::While",
            "Flow::Repeat", "Flow::Match", "Flow::Switch" })
    {
        const CompiledNodeDefPtr controlFlow =
            fixture.registry.FindCompiled(controlFlowName);
        const std::string message = std::string(controlFlowName) +
            " should be pure-safe while retaining execution flow.";
        Require(controlFlow &&
                HasFlag(controlFlow->functionDef->flags,
                        NodeDefinitionFlags::Pure) &&
                !HasFlag(controlFlow->functionDef->flags,
                         NodeDefinitionFlags::ReadOnly),
                message.c_str());
    }
    Require(HasFlag(fixture.registry.FindNative("Math::Square")->functionDef->flags,
                    NodeDefinitionFlags::Pure),
            "Math::Square should be declared pure.");
    Require(!HasFlag(fixture.registry.FindNative("File::FileExists")->functionDef->flags,
                     NodeDefinitionFlags::Pure),
            "File::FileExists depends on external state and must not be pure.");
    Require(!HasFlag(fixture.registry.FindNative("Functional::Map")->functionDef->flags,
                     NodeDefinitionFlags::Pure),
            "Higher-order functions cannot be pure without a pure callable contract.");
    const auto isUsefulDescription = [](const std::string& description)
    {
        return !description.empty() &&
               description.back() != '.' &&
               description.find("Performs ") == std::string::npos &&
               description.find("No description has been registered") ==
                   std::string::npos;
    };
    for (const NativeFunctionDef& definition :
         fixture.registry.nativeDefinitions)
    {
        Require(isUsefulDescription(definition.functionDef->description),
                "Every native node should have an authored description.");
        for (const BasicFunctionDef::Input& input :
             definition.functionDef->inputs)
            Require(isUsefulDescription(input.description),
                    "Every native input should have an authored description.");
        for (const BasicFunctionDef::Input& output :
             definition.functionDef->outputs)
            Require(isUsefulDescription(output.description),
                    "Every native output should have an authored description.");

        IDGenerator ids;
        const NodePtr node = definition.functionDef->MakeNode(
            ids, ScriptElementID::Invalid);
        Require(isUsefulDescription(node->Description),
                "Every native node instance should use its authored description.");
        for (const Pin& pin : node->Inputs)
            Require(isUsefulDescription(pin.Description),
                    "Every native input pin should use its authored description.");
        for (const Pin& pin : node->Outputs)
            Require(isUsefulDescription(pin.Description),
                    "Every native output pin should use its authored description.");
    }
    for (const CompiledNodeDefPtr& definition :
         fixture.registry.compiledDefinitions)
    {
        Require(isUsefulDescription(definition->functionDef->description),
                "Every compiled node should have an authored description.");
        for (const BasicFunctionDef::Input& input :
             definition->functionDef->inputs)
            Require(isUsefulDescription(input.description),
                    "Every compiled input should have an authored description.");
        for (const BasicFunctionDef::Input& output :
             definition->functionDef->outputs)
            Require(isUsefulDescription(output.description),
                    "Every compiled output should have an authored description.");
        if (HasFlag(definition->functionDef->flags,
                    NodeDefinitionFlags::DynamicInputs))
            Require(isUsefulDescription(
                        definition->functionDef->dynamicInputProps.description),
                    "Every dynamic input should have an authored description.");

        IDGenerator ids;
        const NodePtr node = definition->MakeNode(ids);
        Require(isUsefulDescription(node->Description),
                "Every compiled node instance should use its authored description.");
        for (const Pin& pin : node->Inputs)
            Require(isUsefulDescription(pin.Description),
                    "Every compiled input pin should use its authored description.");
        for (const Pin& pin : node->Outputs)
            Require(isUsefulDescription(pin.Description),
                    "Every compiled output pin should use its authored description.");
    }

    const NativeFunctionDef* makeList =
        fixture.registry.FindNative("List::MakeList");
    Require(makeList &&
            makeList->functionDef->genericTypeProperties.size() == 1 &&
            makeList->functionDef->genericTypeProperties[0].variableName == "T" &&
            makeList->functionDef->genericTypeProperties[0].label == "Type",
            "MakeList should expose its generic type through definition metadata.");
    const NodePtr makeListNode = makeList->functionDef->MakeNode(
        fixture.ids, ScriptElementID::Invalid);
    Require(makeListNode->GenericTypeProperties ==
                makeList->functionDef->genericTypeProperties,
            "Function nodes should inherit generic type properties from their definitions.");
}

void SimpleNodesHideRedundantPinNames()
{
    RuntimeFixture fixture;
    for (const char* name : {
            "Math::Add", "Math::Subtract", "Math::Multiply", "Math::Min",
            "Math::Max", "Math::Negate", "Math::Not Equals",
            "Math::Greater Or Equal", "Math::Less Or Equal", "Logic::Not",
            "Logic::And", "Logic::Or", "Value::Is Nil", "String::ToString" })
    {
        const CompiledNodeDefPtr definition = fixture.registry.FindCompiled(name);
        Require(definition != nullptr,
                "Expected the simple node definition to be registered.");
        const NodePtr node = definition->MakeNode(fixture.ids);
        Require(!node->ShowInputPinNames && !node->ShowOutputPinNames,
                "Self-evident simple nodes should hide their pin names.");
        Require(node->Type == NodeType::SimpleLargeBody,
                "Self-evident simple nodes should use the compact presentation.");
    }

    for (const char* name : { "Math::Modulo", "Value::Coalesce" })
    {
        const CompiledNodeDefPtr definition = fixture.registry.FindCompiled(name);
        Require(definition != nullptr,
                "Expected the ordered-operand node definition to be registered.");
        const NodePtr node = definition->MakeNode(fixture.ids);
        Require(node->ShowInputPinNames && node->ShowOutputPinNames,
                "Ordered-operand simple nodes should retain their pin names.");
    }

    const ScriptPropertyPtr property =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Score");
    const NodePtr getProperty = BuildGetPropertyNode(fixture.ids, property);
    Require(getProperty->ShowInputPinNames &&
            !getProperty->ShowOutputPinNames,
            "Class property Get nodes should only hide the redundant output name.");
}

void ListNativeNodesOperateOnLists()
{
    RuntimeFixture fixture;
    ObjList* list = newList();
    list->append(Value(10.0));
    list->append(Value(20.0));
    list->append(Value(30.0));

    const Value length = fixture.CallCollectionNode("List::Length", { Value(list) });
    const Value inBounds = fixture.CallCollectionNode("List::In Bounds",
        { Value(list), Value(2.0) });
    const Value outOfBounds = fixture.CallCollectionNode("List::In Bounds",
        { Value(list), Value(3.0) });
    Require(isNumber(length) && asNumber(length) == 3.0,
            "List::Length should return the number of list items.");
    Require(isBoolean(inBounds) && asBoolean(inBounds) &&
            isBoolean(outOfBounds) && !asBoolean(outOfBounds),
            "List::In Bounds should distinguish valid and invalid indices.");

    const NativeFunctionDef* pop = fixture.registry.FindNative("List::Pop");
    Require(pop != nullptr, "List::Pop should be registered.");
    Value arguments[] = { Value(list) };
    const Value popped = pop->nativeFun(1, arguments, &fixture.vm);
    Require(isNumber(popped) && asNumber(popped) == 30.0 && list->items.size() == 2,
            "List::Pop should return and remove the final list item.");
}

void RangeNativeNodesSupportBothDirections()
{
    RuntimeFixture fixture;
    ObjRange* ascending = newRange(2.0, 4.0);
    ObjRange* descending = newRange(4.0, 2.0);
    const Value ascendingLength =
        fixture.CallCollectionNode("Range::Length", { Value(ascending) });
    const Value descendingLength =
        fixture.CallCollectionNode("Range::Length", { Value(descending) });
    const Value inBounds = fixture.CallCollectionNode("Range::In Bounds",
        { Value(ascending), Value(2.0) });
    const Value outOfBounds = fixture.CallCollectionNode("Range::In Bounds",
        { Value(ascending), Value(3.0) });
    const Value contains = fixture.CallCollectionNode("Range::Contains",
        { Value(descending), Value(3.0) });
    const Value doesNotContain = fixture.CallCollectionNode("Range::Contains",
        { Value(descending), Value(5.0) });
    const Value index = fixture.CallCollectionNode("Range::IndexOf",
        { Value(descending), Value(2.0) });

    Require(isNumber(ascendingLength) && asNumber(ascendingLength) == 3.0 &&
            isNumber(descendingLength) && asNumber(descendingLength) == 3.0,
            "Range::Length should support ascending and descending inclusive ranges.");
    Require(isBoolean(inBounds) && asBoolean(inBounds) &&
            isBoolean(outOfBounds) && !asBoolean(outOfBounds),
            "Range::In Bounds should distinguish valid and invalid indices.");
    Require(isBoolean(contains) && asBoolean(contains) &&
            isBoolean(doesNotContain) && !asBoolean(doesNotContain),
            "Range::Contains should find values in ascending or descending ranges.");
    Require(isNumber(index) && asNumber(index) == 2.0,
            "Range::IndexOf should return the zero-based range index.");
}

void RepeatedInterpretationReleasesStack()
{
    RuntimeFixture fixture;
    for (int iteration = 0; iteration < 300; ++iteration)
    {
        Require(fixture.vm.interpret("for value in [1, 2, 3] {}") ==
                    InterpretResult::INTERPRET_OK,
                "Repeated textual for-in execution should succeed.");
        Require(fixture.vm.getStackSize() == 0,
                "VM::interpret should leave the stack empty after successful execution.");
    }
}

void GarbageCollectionTracksConcreteObjectSizes()
{
    RuntimeFixture fixture;
    const size_t before = fixture.vm.getAllocatedBytes();
    ObjFunction* function = newFunction();

    Require(function->allocationSize == sizeof(ObjFunction),
            "A function allocation should retain its concrete object size.");
    Require(fixture.vm.getAllocatedBytes() - before == sizeof(ObjFunction),
            "VM allocation accounting should use the concrete function size.");
}

void CompilerTemporariesSurviveGarbageCollection()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "GcMain");
    AttachNode(script.main->Graph, BuildBeginNode(fixture.ids, script.main));

    ScriptFunctionPtr function = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "GcNamedFunction");
    AttachNode(function->Graph, BuildBeginNode(fixture.ids, function));
    script.functions.push_back(function);

    ScriptClassPtr scriptClass = std::make_shared<ScriptClass>(fixture.ids.GetNextId(), "GcClass");
    ScriptPropertyPtr property = std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Value");
    property->type = PinType::Float;
    property->defaultValue = Value(42.0);
    scriptClass->properties.push_back(property);
    script.classes.push_back(scriptClass);

    ObjFunction* compiledFunction = nullptr;
    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
        if (compiledFunction)
            fixture.vm.markObject(compiledFunction);
    });
    fixture.vm.allowGarbageCollection(true);

    const ScriptCompileResult compiled = ScriptRuntime::Compile(fixture.vm, script);
    compiledFunction = compiled.function;
    Require(static_cast<bool>(compiled),
            "Named functions and implicit class initializers should compile while GC is enabled.");
    Require(ScriptRuntime::Execute(fixture.vm, compiledFunction) == InterpretResult::INTERPRET_OK,
            "A script compiled while GC was enabled should execute.");
}

void LargeListLiteralsPreserveItems()
{
    RuntimeFixture fixture;
    Require(fixture.vm.interpret(MakeListLiteralSource(1000)) ==
                InterpretResult::INTERPRET_OK,
            "A 1000-item list literal should execute successfully.");
    Require(fixture.vm.getStackSize() == 0,
            "List literal construction should leave the stack empty after execution.");
    Value listValue;
    Require(fixture.vm.globalTable().get(copyString("boundaryList", 12), &listValue) &&
            isList(listValue) && asList(listValue)->items.size() == 1000 &&
            isNumber(asList(listValue)->getValue(0)) &&
            isNumber(asList(listValue)->getValue(999)) &&
            asNumber(asList(listValue)->getValue(0)) == 0.0 &&
            asNumber(asList(listValue)->getValue(999)) == 999.0,
            "Large list literals should preserve every item in source order.");
}

void InstanceErrorsDoNotChangeDefinitionCapabilities()
{
    RuntimeFixture fixture;
    NodePtr node = fixture.registry.FindCompiled("Math::Add")->MakeNode(fixture.ids);
    node->InstanceFlags |= NodeInstanceFlags::Error;
    Require(node->IsPure() && HasFlag(node->InstanceFlags, NodeInstanceFlags::Error),
            "Instance errors must not change immutable definition capabilities.");
}

void MissingBeginIsRejected()
{
    IDGenerator ids;
    Script script;
    script.ID = ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(ids.GetNextId(), "Main");
    const ValidationReport report = ScriptValidator::Validate(script);
    Require(report.HasErrors() && HasCode(report, "begin-count"),
            "Validation should reject a graph without Begin.");
}

void DependencyCyclesAreRejected()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");
    AttachNode(script.main->Graph, BuildBeginNode(fixture.ids, script.main));
    const CompiledNodeDefPtr add = fixture.registry.FindCompiled("Math::Add");
    NodePtr first = add->MakeNode(fixture.ids);
    NodePtr second = add->MakeNode(fixture.ids);
    AttachNode(script.main->Graph, first);
    AttachNode(script.main->Graph, second);
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        first->Outputs[0].ID, second->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        second->Outputs[0].ID, first->Inputs[0].ID));

    const ValidationReport report = ScriptValidator::Validate(script);
    Require(report.HasErrors() && HasCode(report, "graph-cycle"),
            "Validation should reject dependency cycles.");
}

void ImplicitSelfReceiversRespectGraphContext()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");

    ScriptPropertyPtr observed =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Observed");
    observed->type = PinType::Float;
    observed->defaultValue = Value(0.0);
    script.variables.push_back(observed);

    ScriptClassPtr counter =
        std::make_shared<ScriptClass>(fixture.ids.GetNextId(), "Counter");
    ScriptPropertyPtr value =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "value");
    value->type = PinType::Float;
    value->defaultValue = Value(1.0);
    counter->properties.push_back(value);

    counter->constructor =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "init");
    NodePtr constructorBegin =
        BuildBeginNode(fixture.ids, counter->constructor);
    NodePtr constructorSet = BuildSetPropertyNode(fixture.ids, value);
    constructorSet->Inputs[2].LiteralValue = Value(7.0);
    AttachNode(counter->constructor->Graph, constructorBegin);
    AttachNode(counter->constructor->Graph, constructorSet);
    counter->constructor->Graph.AddLink(Link(
        fixture.ids.GetNextId(), constructorBegin->Outputs[0].ID,
        constructorSet->Inputs[0].ID));

    ScriptFunctionPtr read =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "read");
    read->functionDef->flags |= NodeDefinitionFlags::ReadOnly;
    read->functionDef->outputs.push_back(
        { "Value", Value(0.0), fixture.ids.GetNextId(), PinType::Float });
    NodePtr readBegin = BuildBeginNode(fixture.ids, read);
    NodePtr propertyGet = BuildGetPropertyNode(fixture.ids, value);
    NodePtr readReturn = BuildReturnNode(fixture.ids, *read);
    AttachNode(read->Graph, readBegin);
    AttachNode(read->Graph, propertyGet);
    AttachNode(read->Graph, readReturn);
    read->Graph.AddLink(Link(fixture.ids.GetNextId(),
        readBegin->Outputs[0].ID, readReturn->Inputs[0].ID));
    read->Graph.AddLink(Link(fixture.ids.GetNextId(),
        propertyGet->Outputs[0].ID, readReturn->Inputs[1].ID));
    counter->methods.push_back(read);

    ScriptFunctionPtr readThroughSelf =
        std::make_shared<ScriptFunction>(
            fixture.ids.GetNextId(), "readThroughSelf");
    readThroughSelf->functionDef->flags |= NodeDefinitionFlags::ReadOnly;
    readThroughSelf->functionDef->outputs.push_back(
        { "Value", Value(0.0), fixture.ids.GetNextId(), PinType::Float });
    NodePtr throughBegin = BuildBeginNode(fixture.ids, readThroughSelf);
    NodePtr callRead = BuildMethodCallNode(fixture.ids, read);
    NodePtr getRead = BuildGetMethodNode(fixture.ids, read);
    NodePtr throughReturn = BuildReturnNode(fixture.ids, *readThroughSelf);
    AttachNode(readThroughSelf->Graph, throughBegin);
    AttachNode(readThroughSelf->Graph, callRead);
    AttachNode(readThroughSelf->Graph, getRead);
    AttachNode(readThroughSelf->Graph, throughReturn);
    readThroughSelf->Graph.AddLink(Link(fixture.ids.GetNextId(),
        throughBegin->Outputs[0].ID, throughReturn->Inputs[0].ID));
    readThroughSelf->Graph.AddLink(Link(fixture.ids.GetNextId(),
        callRead->Outputs[0].ID, throughReturn->Inputs[1].ID));
    counter->methods.push_back(readThroughSelf);
    script.classes.push_back(counter);

    NodePtr mainBegin = BuildBeginNode(fixture.ids, script.main);
    NodePtr construct = BuildConstructObjectNode(fixture.ids, counter);
    NodePtr callThrough =
        BuildMethodCallNode(fixture.ids, readThroughSelf);
    NodePtr storeObserved = BuildSetVariableNode(fixture.ids, observed);
    for (const NodePtr& node :
         { mainBegin, construct, callThrough, storeObserved })
        AttachNode(script.main->Graph, node);
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        mainBegin->Outputs[0].ID, construct->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        construct->Outputs[0].ID, storeObserved->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        construct->Outputs[1].ID, callThrough->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        callThrough->Outputs[0].ID, storeObserved->Inputs[1].ID));

    const ValidationReport valid = ScriptValidator::Validate(script);
    Require(!valid.HasErrors(),
            "Unconnected receivers should resolve to self in their owning class.");

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    const ScriptCompileResult compiled =
        ScriptRuntime::Compile(fixture.vm, script);
    Require(static_cast<bool>(compiled),
            "Implicit property and method receivers should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "Implicit property and method receivers should execute.");
    Value result;
    Require(fixture.vm.globalTable().get(copyString("Observed", 8), &result) &&
                isNumber(result) && asNumber(result) == 7.0,
            "Implicit self should target the current Counter instance.");
    Require(GraphUtils::UsesImplicitReceiver(
                script, readThroughSelf->ID, readThroughSelf->Graph, *getRead) &&
            getRead->Inputs[0].Name == "Instance" &&
            getRead->Outputs[0].Type ==
                TypeRef::Function({}, { PinType::Float }),
            "A method Get node should default its instance to self and expose only the method signature.");

    NodePtr missingInMain = BuildGetPropertyNode(fixture.ids, value);
    NodePtr missingMethodInMain = BuildGetMethodNode(fixture.ids, read);
    AttachNode(script.main->Graph, missingInMain);
    AttachNode(script.main->Graph, missingMethodInMain);
    ScriptClassPtr other =
        std::make_shared<ScriptClass>(fixture.ids.GetNextId(), "Other");
    ScriptFunctionPtr otherMethod =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "readCounter");
    NodePtr otherBegin = BuildBeginNode(fixture.ids, otherMethod);
    NodePtr missingInOtherClass = BuildGetPropertyNode(fixture.ids, value);
    AttachNode(otherMethod->Graph, otherBegin);
    AttachNode(otherMethod->Graph, missingInOtherClass);
    other->methods.push_back(otherMethod);
    script.classes.push_back(other);

    const ValidationReport invalid = ScriptValidator::Validate(script);
    const auto hasMissingInstance =
        [&](ScriptElementID functionId, const NodePtr& node)
    {
        const auto diagnostics =
            invalid.ForNode(functionId, node->ID);
        return std::any_of(diagnostics.begin(), diagnostics.end(),
            [](const ValidationDiagnostic* diagnostic)
            {
                return diagnostic->code == "missing-instance";
            });
    };
    Require(hasMissingInstance(script.main->ID, missingInMain),
            "A member node in Main should require an explicit instance.");
    Require(hasMissingInstance(script.main->ID, missingMethodInMain),
            "A method Get node in Main should require an explicit instance.");
    Require(hasMissingInstance(otherMethod->ID, missingInOtherClass),
            "A member node in another class should require an explicit instance.");
    Require(!HasCode(valid, "missing-instance"),
            "Same-class receivers must not produce missing-instance errors.");
}

void MethodGetFunctionsWorkWithFilter()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");

    ScriptPropertyPtr matchCount =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "MatchCount");
    matchCount->type = PinType::Float;
    matchCount->defaultValue = Value(0.0);
    script.variables.push_back(matchCount);

    ScriptClassPtr tester =
        std::make_shared<ScriptClass>(fixture.ids.GetNextId(), "Tester");
    ScriptFunctionPtr accepts =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Accepts");
    accepts->functionDef->flags |= NodeDefinitionFlags::ReadOnly;
    accepts->functionDef->inputs.push_back(
        { "Value", Value(0.0), fixture.ids.GetNextId(), PinType::Float,
          "The value to test" });
    accepts->functionDef->outputs.push_back(
        { "Accepted", Value(false), fixture.ids.GetNextId(), PinType::Bool,
          "True when Value is greater than two" });
    NodePtr methodBegin = BuildBeginNode(fixture.ids, accepts);
    NodePtr greater =
        fixture.registry.FindCompiled("Math::Greater Than")->MakeNode(
            fixture.ids);
    greater->Inputs[1].LiteralValue = Value(2.0);
    NodePtr methodReturn = BuildReturnNode(fixture.ids, *accepts);
    for (const NodePtr& node : { methodBegin, greater, methodReturn })
        AttachNode(accepts->Graph, node);
    accepts->Graph.AddLink(Link(
        fixture.ids.GetNextId(), methodBegin->Outputs[0].ID,
        methodReturn->Inputs[0].ID));
    accepts->Graph.AddLink(Link(
        fixture.ids.GetNextId(), methodBegin->Outputs[1].ID,
        greater->Inputs[0].ID));
    accepts->Graph.AddLink(Link(
        fixture.ids.GetNextId(), greater->Outputs[0].ID,
        methodReturn->Inputs[1].ID));
    tester->methods.push_back(accepts);
    script.classes.push_back(tester);

    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr construct = BuildConstructObjectNode(fixture.ids, tester);
    NodePtr values =
        fixture.registry.FindNative("List::MakeList")->functionDef->MakeNode(
            fixture.ids, ScriptElementID::Invalid);
    values->AddInput(fixture.ids);
    values->AddInput(fixture.ids);
    values->Inputs[0].LiteralValue = Value(1.0);
    values->Inputs[1].LiteralValue = Value(3.0);
    NodePtr getAccepts = BuildGetMethodNode(
        fixture.ids, accepts, ScriptElementID::Invalid,
        TypeRef::Object(tester->ID.id, tester->Name));
    NodePtr filter =
        fixture.registry.FindNative("Functional::Filter")->functionDef->MakeNode(
            fixture.ids, ScriptElementID::Invalid);
    NodePtr length =
        fixture.registry.FindNative("List::Length")->functionDef->MakeNode(
            fixture.ids, ScriptElementID::Invalid);
    NodePtr store = BuildSetVariableNode(fixture.ids, matchCount);
    for (const NodePtr& node :
         { begin, construct, values, getAccepts, filter, length, store })
        AttachNode(script.main->Graph, node);

    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), begin->Outputs[0].ID,
        construct->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), construct->Outputs[0].ID,
        store->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), construct->Outputs[1].ID,
        getAccepts->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), values->Outputs[0].ID,
        filter->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), getAccepts->Outputs[0].ID,
        filter->Inputs[1].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), filter->Outputs[0].ID,
        length->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), length->Outputs[0].ID,
        store->Inputs[1].ID));

    Require(getAccepts->Type == NodeType::SimpleGet &&
            getAccepts->Inputs[0].Type ==
                TypeRef::Object(tester->ID.id, tester->Name) &&
            getAccepts->Outputs[0].Type ==
                TypeRef::Function({ PinType::Float }, { PinType::Bool }) &&
            filter->Outputs[0].Type == TypeRef::List(PinType::Float),
            "Getting an instance method should preserve its signature for Filter inference.");
    Require(!ScriptValidator::Validate(script).HasErrors(),
            "A method Get node with an explicit instance should validate.");

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    const ScriptCompileResult compiled =
        ScriptRuntime::Compile(fixture.vm, script);
    Require(static_cast<bool>(compiled),
            "A graph using a method Get function should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "A bound method should execute through Filter.");
    Require(isNumber(ReadGlobal(fixture.vm, "MatchCount")) &&
            asNumber(ReadGlobal(fixture.vm, "MatchCount")) == 1.0,
            "Filter should invoke the method on its selected instance.");
}

void PureNodesAreConstantFolded()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");
    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr add = fixture.registry.FindCompiled("Math::Add")->MakeNode(fixture.ids);
    add->Inputs[0].LiteralValue = Value(2.0);
    add->Inputs[1].LiteralValue = Value(3.0);
    add->AddInput(fixture.ids);
    add->Inputs[2].LiteralValue = Value(4.0);
    NodePtr print = fixture.registry.FindCompiled("Debug::Print")->MakeNode(fixture.ids);
    AttachNode(script.main->Graph, begin);
    AttachNode(script.main->Graph, add);
    AttachNode(script.main->Graph, print);
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        begin->Outputs[0].ID, print->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        add->Outputs[0].ID, print->Inputs[1].ID));

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    const ScriptCompileResult compiled = ScriptRuntime::Compile(fixture.vm, script);
    Require(static_cast<bool>(compiled), "A valid script should compile.");
    const auto folded = std::find(compiled.foldedNodeIds.begin(),
        compiled.foldedNodeIds.end(), add->ID);
    Require(folded != compiled.foldedNodeIds.end(),
            "A reachable pure Add node should be folded.");
    const size_t index = static_cast<size_t>(
        std::distance(compiled.foldedNodeIds.begin(), folded));
    Require(isNumber(compiled.foldedValues[index]) &&
            asNumber(compiled.foldedValues[index]) == 9.0,
            "Constant folding should preserve every Add input.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "The folded script should execute successfully.");
}

void ForInKeepsConstantStackFootprint()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "ForInMain");
    ScriptPropertyPtr result =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "ForInResult");
    result->defaultValue = Value(-1.0);
    script.variables.push_back(result);
    ScriptPropertyPtr length =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "CollectionLength");
    length->defaultValue = Value(-1.0);
    script.variables.push_back(length);

    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr forIn = fixture.registry.FindCompiled("Flow::For In")->MakeNode(fixture.ids);
    NodePtr storeResult = BuildSetVariableNode(fixture.ids, result);
    NodePtr listLength = fixture.registry.FindNative("List::Length")->functionDef->MakeNode(
        fixture.ids, ScriptElementID::Invalid);
    NodePtr storeLength = BuildSetVariableNode(fixture.ids, length);
    ObjList* list = newList();
    for (int value = 0; value < 1000; ++value)
        list->append(Value(static_cast<double>(value)));
    forIn->Inputs[1].LiteralValue = Value(list);
    listLength->Inputs[0].LiteralValue = Value(list);

    AttachNode(script.main->Graph, begin);
    AttachNode(script.main->Graph, forIn);
    AttachNode(script.main->Graph, storeResult);
    AttachNode(script.main->Graph, listLength);
    AttachNode(script.main->Graph, storeLength);
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        begin->Outputs[0].ID, forIn->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        forIn->Outputs[0].ID, storeResult->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        forIn->Outputs[1].ID, storeResult->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        forIn->Outputs[2].ID, storeLength->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        listLength->Outputs[0].ID, storeLength->Inputs[1].ID));

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    const ScriptCompileResult compiled = ScriptRuntime::Compile(fixture.vm, script);
    Require(static_cast<bool>(compiled), "A large Flow::For In script should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "Flow::For In should iterate over more than 256 list items.");
    Require(fixture.vm.getStackSize() == 0,
            "Flow::For In should release all loop values after execution.");
    Value observedResult;
    Value observedLength;
    Require(fixture.vm.globalTable().get(copyString("ForInResult", 11), &observedResult) &&
            isNumber(observedResult) && asNumber(observedResult) == 999.0,
            "Flow::For In should expose every list value through its Value output.");
    Require(fixture.vm.globalTable().get(copyString("CollectionLength", 16), &observedLength) &&
            isNumber(observedLength) && asNumber(observedLength) == 1000.0,
            "List::Length should execute through the graph compiler.");
}

void ForInIteratesRangesAndStrings()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(
        fixture.ids.GetNextId(), "IterableMain");

    ScriptPropertyPtr rangeResult =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "RangeResult");
    rangeResult->type = PinType::Float;
    rangeResult->defaultValue = Value(-1.0);
    script.variables.push_back(rangeResult);
    ScriptPropertyPtr stringResult =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "StringResult");
    stringResult->type = PinType::String;
    stringResult->defaultValue = StringValue("");
    script.variables.push_back(stringResult);

    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr rangeLoop =
        fixture.registry.FindCompiled("Flow::For In")->MakeNode(fixture.ids);
    NodePtr storeRange = BuildSetVariableNode(fixture.ids, rangeResult);
    NodePtr stringLoop =
        fixture.registry.FindCompiled("Flow::For In")->MakeNode(fixture.ids);
    NodePtr storeString = BuildSetVariableNode(fixture.ids, stringResult);
    rangeLoop->Inputs[1].LiteralValue = Value(newRange(2.0, 4.0));
    stringLoop->Inputs[1].LiteralValue = StringValue("Lox");

    for (const NodePtr& node :
         { begin, rangeLoop, storeRange, stringLoop, storeString })
        AttachNode(script.main->Graph, node);
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        begin->Outputs[0].ID, rangeLoop->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        rangeLoop->Outputs[0].ID, storeRange->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        rangeLoop->Outputs[1].ID, storeRange->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        rangeLoop->Outputs[2].ID, stringLoop->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        stringLoop->Outputs[0].ID, storeString->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        stringLoop->Outputs[1].ID, storeString->Inputs[1].ID));

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    const ScriptCompileResult compiled = ScriptRuntime::Compile(fixture.vm, script);
    Require(static_cast<bool>(compiled),
            "Range and string Flow::For In nodes should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "Range and string Flow::For In nodes should execute.");

    Value observedRange;
    Value observedString;
    Require(fixture.vm.globalTable().get(
                copyString("RangeResult", 11), &observedRange) &&
            isNumber(observedRange) && asNumber(observedRange) == 4.0,
            "Flow::For In should expose range values.");
    Require(fixture.vm.globalTable().get(
                copyString("StringResult", 12), &observedString) &&
            isString(observedString) &&
            std::string(asString(observedString)->chars) == "x",
            "Flow::For In should expose string characters.");
}

void MainReceivesProgramArgumentsAsAStringList()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(
        fixture.ids.GetNextId(), "Main");
    script.main->functionDef->inputs.push_back(
        { "Arguments", Value(newList()), fixture.ids.GetNextId() });

    ScriptPropertyPtr observed = std::make_shared<ScriptProperty>(
        fixture.ids.GetNextId(), "ArgumentCount");
    observed->defaultValue = Value(-1.0);
    script.variables.push_back(observed);

    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr listLength =
        fixture.registry.FindNative("List::Length")->functionDef->MakeNode(
            fixture.ids, ScriptElementID::Invalid);
    NodePtr storeCount = BuildSetVariableNode(fixture.ids, observed);
    AttachNode(script.main->Graph, begin);
    AttachNode(script.main->Graph, listLength);
    AttachNode(script.main->Graph, storeCount);
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), begin->Outputs[0].ID, storeCount->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), begin->Outputs[1].ID, listLength->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), listLength->Outputs[0].ID, storeCount->Inputs[1].ID));

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    ScriptCompileOptions options;
    options.programArguments = { "first", "second", "third" };
    const ScriptCompileResult compiled =
        ScriptRuntime::Compile(fixture.vm, script, options);
    Require(static_cast<bool>(compiled),
            "A Main graph using Arguments should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "A Main graph using Arguments should execute.");

    Value count;
    Require(fixture.vm.globalTable().get(
                copyString("ArgumentCount", 13), &count) &&
            isNumber(count) && asNumber(count) == 3.0,
            "Main did not receive program arguments as a three-item string list.");
}

void FunctionsAndMethodsSupportMultipleOutputs()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");

    const auto addVariable = [&](const char* name, const Value& defaultValue)
    {
        ScriptPropertyPtr variable =
            std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), name);
        variable->defaultValue = defaultValue;
        script.variables.push_back(variable);
        return variable;
    };

    ScriptPropertyPtr multiNumber = addVariable("MultiNumber", Value(0.0));
    ScriptPropertyPtr multiText =
        addVariable("MultiText", Value(takeString("", 0)));
    ScriptPropertyPtr multiReady = addVariable("MultiReady", Value(false));
    ScriptPropertyPtr methodNumber = addVariable("MethodNumber", Value(0.0));
    ScriptPropertyPtr methodText =
        addVariable("MethodText", Value(takeString("", 0)));

    ScriptFunctionPtr noOutputs =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "NoOutputs");
    NodePtr noOutputsBegin = BuildBeginNode(fixture.ids, noOutputs);
    NodePtr noOutputsReturn = BuildReturnNode(fixture.ids, *noOutputs);
    AttachNode(noOutputs->Graph, noOutputsBegin);
    AttachNode(noOutputs->Graph, noOutputsReturn);
    noOutputs->Graph.AddLink(Link(fixture.ids.GetNextId(),
        noOutputsBegin->Outputs[0].ID, noOutputsReturn->Inputs[0].ID));
    script.functions.push_back(noOutputs);

    ScriptFunctionPtr multiple =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Multiple");
    multiple->functionDef->outputs.push_back(
        { "Number", Value(0.0), fixture.ids.GetNextId() });
    multiple->functionDef->outputs.push_back(
        { "Text", Value(takeString("", 0)), fixture.ids.GetNextId() });
    multiple->functionDef->outputs.push_back(
        { "Ready", Value(false), fixture.ids.GetNextId() });
    NodePtr multipleBegin = BuildBeginNode(fixture.ids, multiple);
    NodePtr multipleReturn = BuildReturnNode(fixture.ids, *multiple);
    multipleReturn->Inputs[1].LiteralValue = Value(42.0);
    multipleReturn->Inputs[2].LiteralValue = Value(takeString("packed", 6));
    multipleReturn->Inputs[3].LiteralValue = Value(true);
    AttachNode(multiple->Graph, multipleBegin);
    AttachNode(multiple->Graph, multipleReturn);
    multiple->Graph.AddLink(Link(fixture.ids.GetNextId(),
        multipleBegin->Outputs[0].ID, multipleReturn->Inputs[0].ID));
    script.functions.push_back(multiple);

    ScriptClassPtr source =
        std::make_shared<ScriptClass>(fixture.ids.GetNextId(), "OutputSource");
    ScriptFunctionPtr method =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "read");
    method->functionDef->outputs.push_back(
        { "Number", Value(0.0), fixture.ids.GetNextId() });
    method->functionDef->outputs.push_back(
        { "Text", Value(takeString("", 0)), fixture.ids.GetNextId() });
    NodePtr methodBegin = BuildBeginNode(fixture.ids, method);
    NodePtr methodReturn = BuildReturnNode(fixture.ids, *method);
    methodReturn->Inputs[1].LiteralValue = Value(7.0);
    methodReturn->Inputs[2].LiteralValue = Value(takeString("method", 6));
    AttachNode(method->Graph, methodBegin);
    AttachNode(method->Graph, methodReturn);
    method->Graph.AddLink(Link(fixture.ids.GetNextId(),
        methodBegin->Outputs[0].ID, methodReturn->Inputs[0].ID));
    source->methods.push_back(method);
    script.classes.push_back(source);

    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr callNoOutputs =
        noOutputs->functionDef->MakeNode(fixture.ids, noOutputs->ID);
    NodePtr callMultiple =
        multiple->functionDef->MakeNode(fixture.ids, multiple->ID);
    NodePtr setMultiNumber = BuildSetVariableNode(fixture.ids, multiNumber);
    NodePtr setMultiText = BuildSetVariableNode(fixture.ids, multiText);
    NodePtr setMultiReady = BuildSetVariableNode(fixture.ids, multiReady);
    NodePtr construct = BuildConstructObjectNode(fixture.ids, source);
    NodePtr callMethod = BuildMethodCallNode(fixture.ids, method);
    NodePtr setMethodNumber = BuildSetVariableNode(fixture.ids, methodNumber);
    NodePtr setMethodText = BuildSetVariableNode(fixture.ids, methodText);
    for (const NodePtr& node : { begin, callNoOutputs, callMultiple,
             setMultiNumber, setMultiText, setMultiReady, construct, callMethod,
             setMethodNumber, setMethodText })
        AttachNode(script.main->Graph, node);

    const auto connectFlow = [&](const NodePtr& from, const NodePtr& to)
    {
        script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
            from->Outputs[0].ID, to->Inputs[0].ID));
    };
    connectFlow(begin, callNoOutputs);
    connectFlow(callNoOutputs, callMultiple);
    connectFlow(callMultiple, setMultiNumber);
    connectFlow(setMultiNumber, setMultiText);
    connectFlow(setMultiText, setMultiReady);
    connectFlow(setMultiReady, construct);
    connectFlow(construct, callMethod);
    connectFlow(callMethod, setMethodNumber);
    connectFlow(setMethodNumber, setMethodText);

    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        callMultiple->Outputs[1].ID, setMultiNumber->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        callMultiple->Outputs[2].ID, setMultiText->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        callMultiple->Outputs[3].ID, setMultiReady->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        construct->Outputs[1].ID, callMethod->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        callMethod->Outputs[1].ID, setMethodNumber->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        callMethod->Outputs[2].ID, setMethodText->Inputs[1].ID));

    const ScriptCompileResult compiled = ScriptRuntime::Compile(fixture.vm, script);
    Require(static_cast<bool>(compiled),
            "Functions with zero or multiple outputs should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "Functions and methods with multiple outputs should execute.");

    const auto readGlobal = [&](const char* name)
    {
        Value value;
        const std::string key(name);
        Require(fixture.vm.globalTable().get(
                    copyString(key.c_str(), static_cast<int>(key.size())), &value),
                "Expected a multi-output result variable.");
        return value;
    };
    const Value observedMultiNumber = readGlobal("MultiNumber");
    const Value observedMultiText = readGlobal("MultiText");
    const Value observedMultiReady = readGlobal("MultiReady");
    const Value observedMethodNumber = readGlobal("MethodNumber");
    const Value observedMethodText = readGlobal("MethodText");
    Require(isNumber(observedMultiNumber) && asNumber(observedMultiNumber) == 42.0,
            "The first function output was not unpacked correctly.");
    Require(isString(observedMultiText) &&
            asString(observedMultiText)->chars == "packed",
            "The second function output was not unpacked correctly.");
    Require(isBoolean(observedMultiReady) && asBoolean(observedMultiReady),
            "The third function output was not unpacked correctly.");
    Require(isNumber(observedMethodNumber) && asNumber(observedMethodNumber) == 7.0,
            "The first method output was not unpacked correctly.");
    Require(isString(observedMethodText) &&
            asString(observedMethodText)->chars == "method",
            "The second method output was not unpacked correctly.");
}

Script BuildClassRangeMatchScript(IDGenerator& ids, NodeRegistry& registry)
{
    Script script;
    script.ID = ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(ids.GetNextId(), "FeatureMain");
    ScriptPropertyPtr classResult =
        std::make_shared<ScriptProperty>(ids.GetNextId(), "ClassFeatureResult");
    classResult->defaultValue = Value(0.0);
    script.variables.push_back(classResult);
    ScriptPropertyPtr matchResult =
        std::make_shared<ScriptProperty>(ids.GetNextId(), "MatchFeatureResult");
    matchResult->defaultValue = Value(false);
    script.variables.push_back(matchResult);
    ScriptPropertyPtr flowMatchResult =
        std::make_shared<ScriptProperty>(ids.GetNextId(), "FlowMatchFeatureResult");
    flowMatchResult->defaultValue = Value(0.0);
    script.variables.push_back(flowMatchResult);

    ScriptClassPtr counter = std::make_shared<ScriptClass>(ids.GetNextId(), "Counter");
    ScriptPropertyPtr counterValue = std::make_shared<ScriptProperty>(ids.GetNextId(), "value");
    counterValue->defaultValue = Value(1.0);
    counter->properties.push_back(counterValue);

    counter->constructor = std::make_shared<ScriptFunction>(ids.GetNextId(), "init");
    counter->constructor->functionDef->inputs.push_back(
        { "start", Value(0.0), ids.GetNextId() });
    NodePtr constructorBegin = BuildBeginNode(ids, counter->constructor);
    NodePtr constructorThis = BuildThisNode(ids);
    NodePtr constructorSet = BuildSetPropertyNode(ids, counterValue);
    AttachNode(counter->constructor->Graph, constructorBegin);
    AttachNode(counter->constructor->Graph, constructorThis);
    AttachNode(counter->constructor->Graph, constructorSet);
    counter->constructor->Graph.AddLink(Link(ids.GetNextId(),
        constructorBegin->Outputs[0].ID, constructorSet->Inputs[0].ID));
    counter->constructor->Graph.AddLink(Link(ids.GetNextId(),
        constructorThis->Outputs[0].ID, constructorSet->Inputs[1].ID));
    counter->constructor->Graph.AddLink(Link(ids.GetNextId(),
        constructorBegin->Outputs[1].ID, constructorSet->Inputs[2].ID));

    ScriptFunctionPtr getValue = std::make_shared<ScriptFunction>(ids.GetNextId(), "getValue");
    getValue->functionDef->outputs.push_back({ "Value", Value(0.0), ids.GetNextId() });
    NodePtr methodBegin = BuildBeginNode(ids, getValue);
    NodePtr methodThis = BuildThisNode(ids);
    NodePtr propertyGet = BuildGetPropertyNode(ids, counterValue);
    NodePtr methodReturn = BuildReturnNode(ids, *getValue);
    AttachNode(getValue->Graph, methodBegin);
    AttachNode(getValue->Graph, methodThis);
    AttachNode(getValue->Graph, propertyGet);
    AttachNode(getValue->Graph, methodReturn);
    getValue->Graph.AddLink(Link(ids.GetNextId(),
        methodBegin->Outputs[0].ID, methodReturn->Inputs[0].ID));
    getValue->Graph.AddLink(Link(ids.GetNextId(),
        methodThis->Outputs[0].ID, propertyGet->Inputs[0].ID));
    getValue->Graph.AddLink(Link(ids.GetNextId(),
        propertyGet->Outputs[0].ID, methodReturn->Inputs[1].ID));
    counter->methods.push_back(getValue);
    script.classes.push_back(counter);

    NodePtr begin = BuildBeginNode(ids, script.main);
    NodePtr construct = BuildConstructObjectNode(ids, counter);
    construct->Inputs[1].LiteralValue = Value(7.0);
    NodePtr callGetValue = BuildMethodCallNode(ids, getValue);
    NodePtr storeClassResult = BuildSetVariableNode(ids, classResult);
    NodePtr storeMatchResult = BuildSetVariableNode(ids, matchResult);
    storeMatchResult->Inputs[1].LiteralValue = Value(true);
    NodePtr flowMatch = registry.FindCompiled("Flow::Match")->MakeNode(ids);
    flowMatch->AddInput(ids);
    flowMatch->Inputs[1].LiteralValue = Value(3.0);
    flowMatch->Inputs[2].LiteralValue = Value(1.0);
    NodePtr flowRange = registry.FindCompiled("Range::Make")->MakeNode(ids);
    flowRange->Inputs[0].LiteralValue = Value(2.0);
    flowRange->Inputs[1].LiteralValue = Value(4.0);
    NodePtr firstCase = BuildSetVariableNode(ids, flowMatchResult);
    firstCase->Inputs[1].LiteralValue = Value(10.0);
    NodePtr secondCase = BuildSetVariableNode(ids, flowMatchResult);
    secondCase->Inputs[1].LiteralValue = Value(20.0);
    NodePtr defaultCase = BuildSetVariableNode(ids, flowMatchResult);
    defaultCase->Inputs[1].LiteralValue = Value(99.0);
    for (const NodePtr& node : { begin, construct, callGetValue, storeClassResult,
             storeMatchResult, flowMatch, flowRange, firstCase, secondCase, defaultCase })
        AttachNode(script.main->Graph, node);

    script.main->Graph.AddLink(Link(ids.GetNextId(),
        begin->Outputs[0].ID, construct->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        construct->Outputs[0].ID, callGetValue->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        construct->Outputs[1].ID, callGetValue->Inputs[1].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        callGetValue->Outputs[0].ID, storeClassResult->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        callGetValue->Outputs[1].ID, storeClassResult->Inputs[1].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        storeClassResult->Outputs[0].ID, flowMatch->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        flowRange->Outputs[0].ID, flowMatch->Inputs[3].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        flowMatch->Outputs[0].ID, firstCase->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        flowMatch->Outputs[1].ID, storeMatchResult->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        storeMatchResult->Outputs[0].ID, secondCase->Inputs[0].ID));
    script.main->Graph.AddLink(Link(ids.GetNextId(),
        flowMatch->Outputs[2].ID, defaultCase->Inputs[0].ID));
    return script;
}

void ClassesRangesAndMatchingRoundTripAndExecute()
{
    RuntimeFixture fixture;
    Script source = BuildClassRangeMatchScript(fixture.ids, fixture.registry);
    std::string document;
    const SerializationResult serialized = ScriptSerializer::SerializeToString(source, document);
    Require(static_cast<bool>(serialized), serialized.error.c_str());
    Script restored;
    IDGenerator restoredIds;
    Require(static_cast<bool>(ScriptSerializer::DeserializeFromString(
                document, fixture.registry, restored, restoredIds)),
            "Classes and object nodes should deserialize.");
    Require(restored.classes.size() == 1 && restored.classes[0]->constructor &&
            restored.classes[0]->methods.size() == 1 &&
            restored.classes[0]->properties.size() == 1,
            "Class structure should survive a serialization round trip.");

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(restored);
    });
    const ScriptCompileResult compiled = ScriptRuntime::Compile(fixture.vm, restored);
    Require(static_cast<bool>(compiled),
            "The complete class/range/match script should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "The complete class/range/match script should execute.");
    Value classResult;
    Value matchResult;
    Value flowMatchResult;
    Require(fixture.vm.globalTable().get(copyString("ClassFeatureResult", 18), &classResult) &&
            isNumber(classResult) && asNumber(classResult) == 7.0,
            "Constructor property assignment and method property access should produce 7.");
    Require(fixture.vm.globalTable().get(copyString("MatchFeatureResult", 18), &matchResult) &&
            isBoolean(matchResult) && asBoolean(matchResult),
            "Matching 3 against range 2..4 should produce true.");
    Require(fixture.vm.globalTable().get(
                copyString("FlowMatchFeatureResult", 22), &flowMatchResult) &&
            isNumber(flowMatchResult) && asNumber(flowMatchResult) == 20.0,
            "Flow Match should execute the first matching case and skip Default.");
}

void RuntimeListMutationDoesNotChangeDocumentDefaults()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "ListIsolationMain");

    ScriptClassPtr itemClass =
        std::make_shared<ScriptClass>(fixture.ids.GetNextId(), "ListItem");
    script.classes.push_back(itemClass);

    ScriptPropertyPtr items =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Items");
    items->type = TypeRef::List(
        TypeRef::Object(itemClass->ID.id, itemClass->Name));
    items->defaultValue = Value(newList());
    script.variables.push_back(items);

    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr construct =
        BuildConstructObjectNode(fixture.ids, itemClass);
    NodePtr getItems =
        BuildGetVariableNode(fixture.ids, items);
    const NativeFunctionDef* pushDefinition =
        fixture.registry.FindNative("List::Push");
    Require(pushDefinition != nullptr, "List::Push should be registered.");
    NodePtr push = pushDefinition->functionDef->MakeNode(
        fixture.ids, ScriptElementID::Invalid);

    for (const NodePtr& node : { begin, construct, getItems, push })
        AttachNode(script.main->Graph, node);

    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        begin->Outputs[0].ID, construct->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        construct->Outputs[0].ID, push->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        getItems->Outputs[0].ID, push->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        construct->Outputs[1].ID, push->Inputs[2].ID));

    std::string before;
    const SerializationResult serializedBefore = ScriptSerializer::SerializeToString(script, before);
    Require(static_cast<bool>(serializedBefore), serializedBefore.error.c_str());

    const ScriptCompileResult compiled =
        ScriptRuntime::Compile(fixture.vm, script);
    Require(static_cast<bool>(compiled),
            "The list-isolation graph should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "The list-isolation graph should execute.");

    Require(isList(items->defaultValue) &&
            asList(items->defaultValue)->items.empty(),
            "Runtime list mutation must not change the document default.");
    std::string after;
    Require(static_cast<bool>(
                ScriptSerializer::SerializeToString(script, after)) &&
            after == before,
            "A run that appends an instance must leave the document persistable and unchanged.");
}

void VisualClassesUseToStringWhenPrinted()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "ToStringMain");

    ScriptClassPtr defaultClass =
        std::make_shared<ScriptClass>(fixture.ids.GetNextId(), "DefaultDisplay");
    script.classes.push_back(defaultClass);

    ScriptClassPtr customClass =
        std::make_shared<ScriptClass>(fixture.ids.GetNextId(), "CustomDisplay");
    ScriptFunctionPtr toString =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "toString");
    toString->functionDef->flags |= NodeDefinitionFlags::Pure;
    toString->functionDef->outputs.push_back(
        { "Text", StringValue("custom display"), fixture.ids.GetNextId(),
          TypeRef(PinType::String) });
    NodePtr methodBegin = BuildBeginNode(fixture.ids, toString);
    NodePtr methodReturn = BuildReturnNode(fixture.ids, *toString);
    methodReturn->Inputs[1].LiteralValue = StringValue("custom display");
    AttachNode(toString->Graph, methodBegin);
    AttachNode(toString->Graph, methodReturn);
    toString->Graph.AddLink(Link(fixture.ids.GetNextId(),
        methodBegin->Outputs[0].ID, methodReturn->Inputs[0].ID));
    customClass->methods.push_back(toString);
    script.classes.push_back(customClass);

    ScriptPropertyPtr concatenated =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "ConcatenatedDisplay");
    concatenated->type = PinType::String;
    concatenated->defaultValue = StringValue("");
    script.variables.push_back(concatenated);

    ScriptPropertyPtr displays =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Displays");
    displays->type = TypeRef::List(
        TypeRef::Object(customClass->ID.id, customClass->Name));
    displays->defaultValue = Value(newList());
    script.variables.push_back(displays);

    ScriptPropertyPtr concatenatedList =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "ConcatenatedList");
    concatenatedList->type = PinType::String;
    concatenatedList->defaultValue = StringValue("");
    script.variables.push_back(concatenatedList);

    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr constructDefault =
        BuildConstructObjectNode(fixture.ids, defaultClass);
    const CompiledNodeDefPtr printDefinition =
        fixture.registry.FindCompiled("Debug::Print");
    Require(printDefinition != nullptr, "Debug::Print should be registered.");
    NodePtr printDefault = printDefinition->MakeNode(fixture.ids);
    NodePtr constructCustom =
        BuildConstructObjectNode(fixture.ids, customClass);
    NodePtr getDisplays =
        BuildGetVariableNode(fixture.ids, displays);
    const NativeFunctionDef* pushDefinition =
        fixture.registry.FindNative("List::Push");
    Require(pushDefinition != nullptr, "List::Push should be registered.");
    NodePtr pushDisplay = pushDefinition->functionDef->MakeNode(
        fixture.ids, ScriptElementID::Invalid);
    NodePtr printCustom = printDefinition->MakeNode(fixture.ids);
    NodePtr printDisplays = printDefinition->MakeNode(fixture.ids);
    NodePtr append =
        fixture.registry.FindCompiled("String::Append")->MakeNode(fixture.ids);
    append->Inputs[0].LiteralValue = StringValue("prefix: ");
    NodePtr storeConcatenated =
        BuildSetVariableNode(fixture.ids, concatenated);
    NodePtr appendList =
        fixture.registry.FindCompiled("String::Append")->MakeNode(fixture.ids);
    appendList->Inputs[0].LiteralValue = StringValue("list: ");
    NodePtr storeConcatenatedList =
        BuildSetVariableNode(fixture.ids, concatenatedList);
    for (const NodePtr& node :
         { begin, constructDefault, printDefault, constructCustom, printCustom,
           getDisplays, pushDisplay, printDisplays, append, storeConcatenated,
           appendList, storeConcatenatedList })
        AttachNode(script.main->Graph, node);

    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        begin->Outputs[0].ID, constructDefault->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        constructDefault->Outputs[0].ID, printDefault->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        constructDefault->Outputs[1].ID, printDefault->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        printDefault->Outputs[0].ID, constructCustom->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        constructCustom->Outputs[0].ID, pushDisplay->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        getDisplays->Outputs[0].ID, pushDisplay->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        constructCustom->Outputs[1].ID, pushDisplay->Inputs[2].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        pushDisplay->Outputs[0].ID, printCustom->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        constructCustom->Outputs[1].ID, printCustom->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        printCustom->Outputs[0].ID, printDisplays->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        getDisplays->Outputs[0].ID, printDisplays->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        printDisplays->Outputs[0].ID, storeConcatenated->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        constructCustom->Outputs[1].ID, append->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        append->Outputs[0].ID, storeConcatenated->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        storeConcatenated->Outputs[0].ID, storeConcatenatedList->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        getDisplays->Outputs[0].ID, appendList->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        appendList->Outputs[0].ID, storeConcatenatedList->Inputs[1].ID));

    const ScriptCompileResult compiled =
        ScriptRuntime::Compile(fixture.vm, script);
    if (!compiled)
    {
        std::string error = "The toString graph should compile.";
        for (const ValidationDiagnostic& diagnostic :
             compiled.validation.diagnostics)
            error += "\n" + FormatDiagnostic(diagnostic);
        throw std::runtime_error(error);
    }

    std::ostringstream captured;
    std::streambuf* previous = std::cout.rdbuf(captured.rdbuf());
    const InterpretResult result =
        ScriptRuntime::Execute(fixture.vm, compiled.function);
    std::cout.rdbuf(previous);

    Require(result == InterpretResult::INTERPRET_OK,
            "The toString graph should execute.");
    if (captured.str() !=
        "DefaultDisplay instance\ncustom display\n[custom display]\n")
        throw std::runtime_error(
            "Printing instances, including those in lists, should use the default text or a class toString override; got '" +
            captured.str() + "'.");
    const Value concatenatedValue =
        ReadGlobal(fixture.vm, "ConcatenatedDisplay");
    Require(isString(concatenatedValue) &&
            asString(concatenatedValue)->chars == "prefix: custom display",
            "String concatenation should use a class toString override without corrupting the stack.");
    const Value concatenatedListValue =
        ReadGlobal(fixture.vm, "ConcatenatedList");
    Require(isString(concatenatedListValue) &&
            asString(concatenatedListValue)->chars == "list: custom display",
            "String concatenation should use class toString overrides for list elements.");
}

void FlowSwitchEvaluatesConditionsInOrder()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "SwitchMain");

    ScriptPropertyPtr selected = std::make_shared<ScriptProperty>(
        fixture.ids.GetNextId(), "SwitchSelected");
    selected->defaultValue = Value(0.0);
    script.variables.push_back(selected);
    ScriptPropertyPtr defaultSelected = std::make_shared<ScriptProperty>(
        fixture.ids.GetNextId(), "SwitchDefaultSelected");
    defaultSelected->defaultValue = Value(0.0);
    script.variables.push_back(defaultSelected);

    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr firstSwitch =
        fixture.registry.FindCompiled("Flow::Switch")->MakeNode(fixture.ids);
    firstSwitch->AddInput(fixture.ids);
    firstSwitch->AddInput(fixture.ids);
    NodePtr falseCondition =
        fixture.registry.FindCompiled("Logic::Not")->MakeNode(fixture.ids);
    falseCondition->Inputs[0].LiteralValue = Value(true);
    NodePtr trueCondition =
        fixture.registry.FindCompiled("Logic::Not")->MakeNode(fixture.ids);
    trueCondition->Inputs[0].LiteralValue = Value(false);

    NodePtr firstCase = BuildSetVariableNode(fixture.ids, selected);
    firstCase->Inputs[1].LiteralValue = Value(10.0);
    NodePtr secondCase = BuildSetVariableNode(fixture.ids, selected);
    secondCase->Inputs[1].LiteralValue = Value(20.0);
    NodePtr thirdCase = BuildSetVariableNode(fixture.ids, selected);
    thirdCase->Inputs[1].LiteralValue = Value(30.0);
    NodePtr unexpectedDefault = BuildSetVariableNode(fixture.ids, selected);
    unexpectedDefault->Inputs[1].LiteralValue = Value(99.0);

    NodePtr defaultSwitch =
        fixture.registry.FindCompiled("Flow::Switch")->MakeNode(fixture.ids);
    defaultSwitch->AddInput(fixture.ids);
    NodePtr unexpectedFirst = BuildSetVariableNode(fixture.ids, defaultSelected);
    unexpectedFirst->Inputs[1].LiteralValue = Value(1.0);
    NodePtr unexpectedSecond = BuildSetVariableNode(fixture.ids, defaultSelected);
    unexpectedSecond->Inputs[1].LiteralValue = Value(2.0);
    NodePtr defaultCase = BuildSetVariableNode(fixture.ids, defaultSelected);
    defaultCase->Inputs[1].LiteralValue = Value(40.0);

    NodePtr failingCondition =
        BuildFailingExpressionNode(fixture.ids, PinType::Bool);

    for (const NodePtr& node : {
             begin, firstSwitch, falseCondition, trueCondition, firstCase,
             secondCase, thirdCase, unexpectedDefault, defaultSwitch,
             unexpectedFirst, unexpectedSecond, defaultCase, failingCondition })
        AttachNode(script.main->Graph, node);

    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), begin->Outputs[0].ID,
        firstSwitch->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), falseCondition->Outputs[0].ID,
        firstSwitch->Inputs[1].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), trueCondition->Outputs[0].ID,
        firstSwitch->Inputs[2].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), failingCondition->Outputs[0].ID,
        firstSwitch->Inputs[3].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), firstSwitch->Outputs[0].ID,
        firstCase->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), firstSwitch->Outputs[1].ID,
        secondCase->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), firstSwitch->Outputs[2].ID,
        thirdCase->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), firstSwitch->Outputs[3].ID,
        unexpectedDefault->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), secondCase->Outputs[0].ID,
        defaultSwitch->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), defaultSwitch->Outputs[0].ID,
        unexpectedFirst->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), defaultSwitch->Outputs[1].ID,
        unexpectedSecond->Inputs[0].ID));
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), defaultSwitch->Outputs[2].ID,
        defaultCase->Inputs[0].ID));

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    ScriptCompileOptions options;
    options.enableConstantFolding = false;
    const ScriptCompileResult compiled =
        ScriptRuntime::Compile(fixture.vm, script, options);
    Require(static_cast<bool>(compiled),
            "Flow::Switch should compile with dynamic condition pins.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "Flow::Switch should skip conditions after the first true one.");
    Require(isNumber(ReadGlobal(fixture.vm, "SwitchSelected")) &&
                asNumber(ReadGlobal(fixture.vm, "SwitchSelected")) == 20.0,
            "Flow::Switch should run the first true condition's case.");
    Require(isNumber(ReadGlobal(fixture.vm, "SwitchDefaultSelected")) &&
                asNumber(ReadGlobal(fixture.vm, "SwitchDefaultSelected")) == 40.0,
            "Flow::Switch should run Default when every condition is false.");

    Script roundTrip;
    roundTrip.ID = fixture.ids.GetNextId();
    roundTrip.main =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "SwitchRoundTrip");
    NodePtr roundTripBegin = BuildBeginNode(fixture.ids, roundTrip.main);
    NodePtr roundTripSwitch =
        fixture.registry.FindCompiled("Flow::Switch")->MakeNode(fixture.ids);
    roundTripSwitch->AddInput(fixture.ids);
    AttachNode(roundTrip.main->Graph, roundTripBegin);
    AttachNode(roundTrip.main->Graph, roundTripSwitch);
    roundTrip.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), roundTripBegin->Outputs[0].ID,
        roundTripSwitch->Inputs[0].ID));

    std::string document;
    Require(static_cast<bool>(
                ScriptSerializer::SerializeToString(roundTrip, document)),
            "A dynamic Flow::Switch should serialize.");
    Script restored;
    IDGenerator restoredIds;
    Require(static_cast<bool>(ScriptSerializer::DeserializeFromString(
                document, fixture.registry, restored, restoredIds)),
            "A dynamic Flow::Switch should deserialize.");
    const NodePtr restoredSwitch = restored.main->Graph.FindNodeIf(
        [](const NodePtr& node)
        {
            return node->DefinitionId == "vlox.std.compiled.flow.switch";
        });
    Require(restoredSwitch && restoredSwitch->Inputs.size() == 3 &&
                restoredSwitch->Outputs.size() == 3,
            "Flow::Switch should preserve its dynamic cases and Default output.");
}

void CompleteExpressionNodesCompileAndExecute()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "ExpressionMain");
    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    AttachNode(script.main->Graph, begin);
    ed::PinId previousFlow = begin->Outputs[0].ID;

    const auto addExpression =
        [&](const char* definitionName, const char* variableName,
            const Value& first, const Value& second, bool hasSecond,
            const Value& resultDefault)
    {
        const CompiledNodeDefPtr definition =
            fixture.registry.FindCompiled(definitionName);
        Require(definition != nullptr, "Expected expression node to be registered.");
        NodePtr expression = definition->MakeNode(fixture.ids);
        expression->Inputs[0].LiteralValue = first;
        if (hasSecond)
            expression->Inputs[1].LiteralValue = second;

        ScriptPropertyPtr result = std::make_shared<ScriptProperty>(
            fixture.ids.GetNextId(), variableName);
        result->defaultValue = resultDefault;
        script.variables.push_back(result);
        NodePtr setter = BuildSetVariableNode(fixture.ids, result);
        AttachNode(script.main->Graph, expression);
        AttachNode(script.main->Graph, setter);
        script.main->Graph.AddLink(Link(
            fixture.ids.GetNextId(), previousFlow, setter->Inputs[0].ID));
        script.main->Graph.AddLink(Link(
            fixture.ids.GetNextId(), expression->Outputs[0].ID,
            setter->Inputs[1].ID));
        previousFlow = setter->Outputs[0].ID;
        return expression;
    };

    addExpression("Logic::Not", "ExprNot", Value(true), Value(), false,
                  Value(false));
    addExpression("Math::Negate", "ExprNegate", Value(5.0), Value(), false,
                  Value(0.0));
    addExpression("Math::Not Equals", "ExprNotEquals",
                  StringValue("left"), StringValue("right"), true, Value(false));
    addExpression("Math::Greater Or Equal", "ExprGreaterEqual",
                  Value(4.0), Value(4.0), true, Value(false));
    addExpression("Math::Less Or Equal", "ExprLessEqual",
                  Value(3.0), Value(4.0), true, Value(false));
    addExpression("Math::Equals", "ExprAnyEquals",
                  StringValue("same"), StringValue("same"), true, Value(false));
    addExpression("Value::Is Nil", "ExprIsNil", Value(), Value(), false,
                  Value(false));
    addExpression("String::ToString", "ExprStringNil", Value(), Value(), false, StringValue(""));
    addExpression("String::ToString", "ExprStringBool", Value(true), Value(), false, StringValue(""));
    addExpression("String::ToString", "ExprStringNumber", Value(12.5), Value(), false, StringValue(""));
    addExpression("String::ToString", "ExprStringList", Value(MakeList({ StringValue("value"), Value(3.0) })), Value(), false, StringValue(""));
    addExpression("Logic::And", "ExprAnd", Value(true), Value(true), true,
                  Value(false));
    NodePtr shortAnd = addExpression(
        "Logic::And", "ExprAndShort", Value(false), Value(true), true, Value(true));
    addExpression("Logic::Or", "ExprOr", Value(false), Value(true), true,
                  Value(false));
    NodePtr shortOr = addExpression(
        "Logic::Or", "ExprOrShort", Value(true), Value(false), true, Value(false));
    NodePtr manyAdd = addExpression(
        "Math::Add", "ExprAddMany", Value(1.0), Value(2.0), true, Value(0.0));
    manyAdd->AddInput(fixture.ids);
    manyAdd->AddInput(fixture.ids);
    manyAdd->Inputs[2].LiteralValue = Value(3.0);
    manyAdd->Inputs[3].LiteralValue = Value(4.0);
    NodeUtils::BuildNode(manyAdd);
    NodePtr manySubtract = addExpression(
        "Math::Subtract", "ExprSubtractMany", Value(20.0), Value(3.0), true, Value(0.0));
    manySubtract->AddInput(fixture.ids);
    manySubtract->Inputs[2].LiteralValue = Value(2.0);
    NodeUtils::BuildNode(manySubtract);
    NodePtr manyMultiply = addExpression(
        "Math::Multiply", "ExprMultiplyMany", Value(2.0), Value(3.0), true, Value(0.0));
    manyMultiply->AddInput(fixture.ids);
    manyMultiply->Inputs[2].LiteralValue = Value(4.0);
    NodeUtils::BuildNode(manyMultiply);
    NodePtr manyMin = addExpression(
        "Math::Min", "ExprMinMany", Value(4.0), Value(-2.0), true, Value(0.0));
    manyMin->AddInput(fixture.ids);
    manyMin->Inputs[2].LiteralValue = Value(8.0);
    NodeUtils::BuildNode(manyMin);
    NodePtr manyMax = addExpression(
        "Math::Max", "ExprMaxMany", Value(4.0), Value(-2.0), true, Value(0.0));
    manyMax->AddInput(fixture.ids);
    manyMax->Inputs[2].LiteralValue = Value(8.0);
    NodeUtils::BuildNode(manyMax);
    NodePtr manyAnd = addExpression(
        "Logic::And", "ExprAndMany", Value(true), Value(true), true, Value(false));
    manyAnd->AddInput(fixture.ids);
    manyAnd->Inputs[2].LiteralValue = Value(true);
    NodeUtils::BuildNode(manyAnd);
    NodePtr manyOr = addExpression(
        "Logic::Or", "ExprOrMany", Value(false), Value(false), true, Value(false));
    manyOr->AddInput(fixture.ids);
    manyOr->Inputs[2].LiteralValue = Value(true);
    NodeUtils::BuildNode(manyOr);
    NodePtr lateShortAnd = addExpression(
        "Logic::And", "ExprAndLateShort", Value(true), Value(false), true, Value(true));
    lateShortAnd->AddInput(fixture.ids);
    NodeUtils::BuildNode(lateShortAnd);
    NodePtr lateShortOr = addExpression(
        "Logic::Or", "ExprOrLateShort", Value(false), Value(true), true, Value(false));
    lateShortOr->AddInput(fixture.ids);
    NodeUtils::BuildNode(lateShortOr);
    addExpression("Value::Coalesce", "ExprCoalesce",
                  Value(), StringValue("fallback"), true, StringValue(""));
    NodePtr shortCoalesce = addExpression(
        "Value::Coalesce", "ExprCoalesceKeep", StringValue("left"),
        StringValue("right"), true, StringValue(""));

    for (const NodePtr& expression : { shortAnd, shortOr, shortCoalesce })
    {
        NodePtr failing = BuildFailingExpressionNode(
            fixture.ids, expression->Inputs[1].Type);
        AttachNode(script.main->Graph, failing);
        script.main->Graph.AddLink(Link(
            fixture.ids.GetNextId(), failing->Outputs[0].ID,
            expression->Inputs[1].ID));
    }
    for (const NodePtr& expression : { lateShortAnd, lateShortOr })
    {
        NodePtr failing = BuildFailingExpressionNode(fixture.ids, expression->Inputs[2].Type);
        AttachNode(script.main->Graph, failing);
        script.main->Graph.AddLink(Link(
            fixture.ids.GetNextId(), failing->Outputs[0].ID,
            expression->Inputs[2].ID));
    }

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    ScriptCompileOptions options;
    options.enableConstantFolding = false;
    const ScriptCompileResult compiled =
        ScriptRuntime::Compile(fixture.vm, script, options);
    Require(static_cast<bool>(compiled),
            "The complete expression graph should compile without folding.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "The complete expression graph should execute.");

    Require(isBoolean(ReadGlobal(fixture.vm, "ExprNot")) &&
            !asBoolean(ReadGlobal(fixture.vm, "ExprNot")),
            "Logic::Not should invert its input.");
    Require(asNumber(ReadGlobal(fixture.vm, "ExprNegate")) == -5.0,
            "Math::Negate should negate its input.");
    for (const char* name : { "ExprNotEquals", "ExprGreaterEqual",
             "ExprLessEqual", "ExprAnyEquals", "ExprIsNil", "ExprAnd",
             "ExprOr", "ExprOrShort", "ExprAndMany", "ExprOrMany",
             "ExprOrLateShort" })
        Require(isBoolean(ReadGlobal(fixture.vm, name)) &&
                asBoolean(ReadGlobal(fixture.vm, name)),
                "Expected expression result to be true.");
    Require(isBoolean(ReadGlobal(fixture.vm, "ExprAndShort")) &&
            !asBoolean(ReadGlobal(fixture.vm, "ExprAndShort")),
            "Logic::And should preserve a false left operand.");
    Require(isBoolean(ReadGlobal(fixture.vm, "ExprAndLateShort")) &&
            !asBoolean(ReadGlobal(fixture.vm, "ExprAndLateShort")),
            "Logic::And should stop after a false middle operand.");
    Require(isNumber(ReadGlobal(fixture.vm, "ExprAddMany")) &&
            asNumber(ReadGlobal(fixture.vm, "ExprAddMany")) == 10.0,
            "Math::Add should fold every input from left to right.");
    Require(isNumber(ReadGlobal(fixture.vm, "ExprSubtractMany")) &&
            asNumber(ReadGlobal(fixture.vm, "ExprSubtractMany")) == 15.0,
            "Math::Subtract should fold every input from left to right.");
    Require(isNumber(ReadGlobal(fixture.vm, "ExprMultiplyMany")) &&
            asNumber(ReadGlobal(fixture.vm, "ExprMultiplyMany")) == 24.0,
            "Math::Multiply should fold every input from left to right.");
    Require(isNumber(ReadGlobal(fixture.vm, "ExprMinMany")) &&
            asNumber(ReadGlobal(fixture.vm, "ExprMinMany")) == -2.0,
            "Math::Min should select the smallest input.");
    Require(isNumber(ReadGlobal(fixture.vm, "ExprMaxMany")) &&
            asNumber(ReadGlobal(fixture.vm, "ExprMaxMany")) == 8.0,
            "Math::Max should select the largest input.");
    Require(isString(ReadGlobal(fixture.vm, "ExprCoalesce")) &&
            asString(ReadGlobal(fixture.vm, "ExprCoalesce"))->chars == "fallback",
            "Value::Coalesce should use its fallback for nil.");
    Require(isString(ReadGlobal(fixture.vm, "ExprCoalesceKeep")) &&
            asString(ReadGlobal(fixture.vm, "ExprCoalesceKeep"))->chars == "left",
            "Value::Coalesce should preserve a non-nil left operand.");
    Require(asString(ReadGlobal(fixture.vm, "ExprStringNil"))->chars == "nil", "String::ToString should convert nil.");
    Require(asString(ReadGlobal(fixture.vm, "ExprStringBool"))->chars == "true", "String::ToString should convert booleans.");
    Require(asString(ReadGlobal(fixture.vm, "ExprStringNumber"))->chars == "12.5", "String::ToString should convert numbers.");
    Require(asString(ReadGlobal(fixture.vm, "ExprStringList"))->chars == "value,3", "String::ToString should convert lists.");
}

void WhileAndRepeatNodesCompileAndExecute()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "LoopMain");

    const auto addVariable = [&](const char* name, const Value& initial)
    {
        ScriptPropertyPtr property = std::make_shared<ScriptProperty>(
            fixture.ids.GetNextId(), name);
        property->defaultValue = initial;
        script.variables.push_back(property);
        return property;
    };
    ScriptPropertyPtr counter = addVariable("LoopCounter", Value(0.0));
    ScriptPropertyPtr whileDone = addVariable("WhileCompleted", Value(false));
    ScriptPropertyPtr repeatSum = addVariable("RepeatSum", Value(0.0));
    ScriptPropertyPtr repeatDone = addVariable("RepeatCompleted", Value(false));

    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr whileNode =
        fixture.registry.FindCompiled("Flow::While")->MakeNode(fixture.ids);
    NodePtr condition =
        fixture.registry.FindCompiled("Math::Less Than")->MakeNode(fixture.ids);
    NodePtr conditionCounter = BuildGetVariableNode(fixture.ids, counter);
    condition->Inputs[1].LiteralValue = Value(3.0);
    NodePtr increment =
        fixture.registry.FindCompiled("Math::Add")->MakeNode(fixture.ids);
    NodePtr bodyCounter = BuildGetVariableNode(fixture.ids, counter);
    increment->Inputs[1].LiteralValue = Value(1.0);
    NodePtr setCounter = BuildSetVariableNode(fixture.ids, counter);
    NodePtr setWhileDone = BuildSetVariableNode(fixture.ids, whileDone);
    setWhileDone->Inputs[1].LiteralValue = Value(true);

    NodePtr repeat =
        fixture.registry.FindCompiled("Flow::Repeat")->MakeNode(fixture.ids);
    repeat->Inputs[1].LiteralValue = Value(3.0);
    NodePtr sum =
        fixture.registry.FindCompiled("Math::Add")->MakeNode(fixture.ids);
    NodePtr currentSum = BuildGetVariableNode(fixture.ids, repeatSum);
    NodePtr setSum = BuildSetVariableNode(fixture.ids, repeatSum);
    NodePtr setRepeatDone = BuildSetVariableNode(fixture.ids, repeatDone);
    setRepeatDone->Inputs[1].LiteralValue = Value(true);

    for (const NodePtr& node : { begin, whileNode, condition, conditionCounter,
             increment, bodyCounter, setCounter, setWhileDone, repeat, sum,
             currentSum, setSum, setRepeatDone })
        AttachNode(script.main->Graph, node);

    const auto link = [&](const Pin& output, const Pin& input)
    {
        script.main->Graph.AddLink(
            Link(fixture.ids.GetNextId(), output.ID, input.ID));
    };
    link(begin->Outputs[0], whileNode->Inputs[0]);
    link(conditionCounter->Outputs[0], condition->Inputs[0]);
    link(condition->Outputs[0], whileNode->Inputs[1]);
    link(whileNode->Outputs[0], setCounter->Inputs[0]);
    link(bodyCounter->Outputs[0], increment->Inputs[0]);
    link(increment->Outputs[0], setCounter->Inputs[1]);
    link(whileNode->Outputs[1], setWhileDone->Inputs[0]);
    link(setWhileDone->Outputs[0], repeat->Inputs[0]);
    link(repeat->Outputs[0], setSum->Inputs[0]);
    link(currentSum->Outputs[0], sum->Inputs[0]);
    link(repeat->Outputs[1], sum->Inputs[1]);
    link(sum->Outputs[0], setSum->Inputs[1]);
    link(repeat->Outputs[2], setRepeatDone->Inputs[0]);

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    ScriptCompileOptions options;
    options.enableConstantFolding = false;
    const ScriptCompileResult compiled =
        ScriptRuntime::Compile(fixture.vm, script, options);
    Require(static_cast<bool>(compiled),
            "While and Repeat graphs should compile.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) ==
                InterpretResult::INTERPRET_OK,
            "While and Repeat graphs should execute.");
    Require(isNumber(ReadGlobal(fixture.vm, "LoopCounter")) &&
            asNumber(ReadGlobal(fixture.vm, "LoopCounter")) == 3.0,
            "While should re-evaluate its condition and execute three times.");
    Require(isBoolean(ReadGlobal(fixture.vm, "WhileCompleted")) &&
            asBoolean(ReadGlobal(fixture.vm, "WhileCompleted")),
            "While should continue through Completed.");
    Require(isNumber(ReadGlobal(fixture.vm, "RepeatSum")) &&
            asNumber(ReadGlobal(fixture.vm, "RepeatSum")) == 3.0,
            "Repeat should expose indices 0, 1, and 2.");
    Require(isBoolean(ReadGlobal(fixture.vm, "RepeatCompleted")) &&
            asBoolean(ReadGlobal(fixture.vm, "RepeatCompleted")),
            "Repeat should continue through Completed.");
}

void ExpandedMathAndStringNodesOperate()
{
    RuntimeFixture fixture;
    const auto number = [&](const char* name, std::vector<Value> arguments)
    {
        const Value result = fixture.CallNative(name, std::move(arguments));
        Require(isNumber(result), "Expected a numeric native result.");
        return asNumber(result);
    };
    Require(number("Math::Abs", { Value(-4.0) }) == 4.0,
            "Math::Abs should return magnitude.");
    Require(number("Math::Min", { Value(2.0), Value(5.0) }) == 2.0 &&
            number("Math::Max", { Value(2.0), Value(5.0) }) == 5.0,
            "Math::Min and Math::Max should select endpoints.");
    Require(number("Math::Clamp",
                { Value(7.0), Value(1.0), Value(5.0) }) == 5.0,
            "Math::Clamp should constrain values.");
    Require(number("Math::Power", { Value(2.0), Value(3.0) }) == 8.0 &&
            number("Math::Sqrt", { Value(9.0) }) == 3.0,
            "Power and square root should calculate expected values.");
    Require(number("Math::Floor", { Value(2.8) }) == 2.0 &&
            number("Math::Ceil", { Value(2.2) }) == 3.0 &&
            number("Math::Round", { Value(2.6) }) == 3.0,
            "Rounding nodes should use their documented direction.");
    const double random =
        number("Math::Random", { Value(-2.0), Value(2.0) });
    Require(random >= -2.0 && random <= 2.0,
            "Math::Random should stay within its requested bounds.");

    const auto string = [&](const char* name, std::vector<Value> arguments)
    {
        const Value result = fixture.CallNative(name, std::move(arguments));
        Require(isString(result), "Expected a string native result.");
        return asString(result)->chars;
    };
    const CompiledNodeDefPtr toString = fixture.registry.FindCompiled("String::ToString");
    Require(toString && !fixture.registry.FindNative("String::ToString") && toString->functionDef->inputs[0].type == PinType::Any &&
            toString->functionDef->outputs[0].type == PinType::String, "String::ToString should be a compiled Any-to-String node.");
    Require(string("String::Trim", { StringValue("  hello \n") }) == "hello",
            "String::Trim should remove surrounding whitespace.");
    Require(string("String::Replace",
                { StringValue("a-b-a"), StringValue("a"), StringValue("x") }) ==
                "x-b-x",
            "String::Replace should replace every match.");
    Require(string("String::Join",
                { Value(MakeList({ StringValue("a"), StringValue("b") })),
                  StringValue(",") }) == "a,b",
            "String::Join should join list items.");
    Require(asBoolean(fixture.CallNative("String::Starts With",
                { StringValue("visual-lox"), StringValue("visual") })) &&
            asBoolean(fixture.CallNative("String::Ends With",
                { StringValue("visual-lox"), StringValue("lox") })),
            "Prefix and suffix nodes should match boundaries.");
    Require(string("String::Format",
                { StringValue("{0}:{1}"),
                  Value(MakeList({ StringValue("value"), Value(3.0) })) }) ==
                "value:3.000000",
            "String::Format should replace indexed placeholders.");

    const Value parsedNumber = fixture.CallNative(
        "String::Parse Number", { StringValue(" 12.5 ") });
    const Value parsedBool = fixture.CallNative(
        "String::Parse Bool", { StringValue("TRUE") });
    Require(isList(parsedNumber) && asList(parsedNumber)->items.size() == 2 &&
            asNumber(asList(parsedNumber)->items[0]) == 12.5 &&
            asBoolean(asList(parsedNumber)->items[1]),
            "String::Parse Number should package the value and success flag.");
    Require(isList(parsedBool) && asList(parsedBool)->items.size() == 2 &&
            asBoolean(asList(parsedBool)->items[0]) &&
            asBoolean(asList(parsedBool)->items[1]),
            "String::Parse Bool should package the value and success flag.");
}

void ExpandedListAndRangeNodesOperate()
{
    RuntimeFixture fixture;
    ObjList* source =
        MakeList({ Value(3.0), Value(1.0), Value(3.0), Value(2.0) });
    const Value inserted = fixture.CallNative(
        "List::Insert", { Value(source), Value(1.0), Value(9.0) });
    Require(isNumber(inserted) && asNumber(inserted) == 5.0 &&
            asNumber(source->items[1]) == 9.0,
            "List::Insert should mutate the list and return its new length.");
    source->items.erase(source->items.begin() + 1);

    const Value slice = fixture.CallNative(
        "List::Slice", { Value(source), Value(1.0), Value(2.0) });
    const Value reversed = fixture.CallNative("List::Reverse", { Value(source) });
    const Value sorted = fixture.CallNative("List::Sort", { Value(source) });
    const Value distinct = fixture.CallNative("List::Distinct", { Value(source) });
    const Value enumerated =
        fixture.CallNative("List::Enumerate", { Value(source) });
    const Value zipped = fixture.CallNative(
        "List::Zip",
        { Value(source), Value(MakeList({ StringValue("a"), StringValue("b") })) });
    Require(isList(slice) && asList(slice)->items.size() == 2 &&
            asNumber(asList(slice)->items[0]) == 1.0 &&
            asNumber(asList(slice)->items[1]) == 3.0,
            "List::Slice should return the selected window.");
    Require(isList(reversed) && asNumber(asList(reversed)->items[0]) == 2.0 &&
            asNumber(asList(reversed)->items[3]) == 3.0,
            "List::Reverse should return reverse order.");
    Require(isList(sorted) && asNumber(asList(sorted)->items[0]) == 1.0 &&
            asNumber(asList(sorted)->items[3]) == 3.0,
            "List::Sort should return ascending order.");
    Require(isList(distinct) && asList(distinct)->items.size() == 3 &&
            asNumber(asList(distinct)->items[0]) == 3.0 &&
            asNumber(asList(distinct)->items[2]) == 2.0,
            "List::Distinct should preserve first occurrences.");
    Require(isList(enumerated) && isList(asList(enumerated)->items[0]) &&
            asNumber(asList(asList(enumerated)->items[0])->items[0]) == 0.0 &&
            asNumber(asList(asList(enumerated)->items[0])->items[1]) == 3.0,
            "List::Enumerate should pair each value with its index.");
    Require(isList(zipped) && asList(zipped)->items.size() == 2 &&
            isList(asList(zipped)->items[1]) &&
            asString(asList(asList(zipped)->items[1])->items[1])->chars == "b",
            "List::Zip should pair inputs up to the shorter length.");

    ObjList* clearTarget = MakeList({ Value(1.0), Value(2.0) });
    const Value cleared =
        fixture.CallNative("List::Clear", { Value(clearTarget) });
    Require(isNumber(cleared) && asNumber(cleared) == 0.0 &&
            clearTarget->items.empty(),
            "List::Clear should empty the list.");

    const Value rangeValue = fixture.CallNative("Range::Make Advanced",
        { Value(0.0), Value(6.0), Value(2.0), Value(false), Value(false) });
    Require(isRange(rangeValue), "Range::Make Advanced should return a range.");
    ObjRange* range = asRange(rangeValue);
    Require(range->length() == 2 && range->getValue(0) == 2.0 &&
            range->getValue(1) == 4.0 && range->contains(4.0) &&
            !range->contains(6.0),
            "Advanced ranges should honor step and endpoint inclusion.");
}

void MapForEachIteratesKeysAndValues()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "MapLoopMain");

    ScriptPropertyPtr observedKey = std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "ObservedMapKey");
    observedKey->type = PinType::String;
    observedKey->defaultValue = StringValue("");
    script.variables.push_back(observedKey);
    ScriptPropertyPtr observedValue = std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "ObservedMapValue");
    observedValue->type = PinType::Float;
    observedValue->defaultValue = Value(-1.0);
    script.variables.push_back(observedValue);

    ObjMap* values = newMap();
    values->set(StringValue("first"), Value(1.0));
    values->set(StringValue("second"), Value(2.0));
    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr loop = fixture.registry.FindCompiled("Map::For Each")->MakeNode(fixture.ids);
    NodePtr storeKey = BuildSetVariableNode(fixture.ids, observedKey);
    NodePtr storeValue = BuildSetVariableNode(fixture.ids, observedValue);
    for (const NodePtr& node : { begin, loop, storeKey, storeValue })
        AttachNode(script.main->Graph, node);
    loop->Inputs[1].LiteralValue = Value(values);
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), begin->Outputs[0].ID, loop->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), loop->Outputs[0].ID, storeKey->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), loop->Outputs[1].ID, storeKey->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), storeKey->Outputs[0].ID, storeValue->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), loop->Outputs[2].ID, storeValue->Inputs[1].ID));

    fixture.vm.setExternalMarkingFunc([&]()
    {
        MarkNodeRegistryRoots(fixture.registry, fixture.vm);
        ScriptUtils::MarkScriptRoots(script);
    });
    const ScriptCompileResult compiled = ScriptRuntime::Compile(fixture.vm, script);
    Require(static_cast<bool>(compiled), "Map::For Each should compile with typed key and value outputs.");
    Require(ScriptRuntime::Execute(fixture.vm, compiled.function) == InterpretResult::INTERPRET_OK,
            "Map::For Each should execute.");
    Require(isString(ReadGlobal(fixture.vm, "ObservedMapKey")) && asString(ReadGlobal(fixture.vm, "ObservedMapKey"))->chars == "second" &&
            isNumber(ReadGlobal(fixture.vm, "ObservedMapValue")) && asNumber(ReadGlobal(fixture.vm, "ObservedMapValue")) == 2.0,
            "Map::For Each should expose keys and values in insertion order.");
}

void MapNodesOperateAndPreserveInsertionOrder()
{
    RuntimeFixture fixture;
    const Value made = fixture.CallNative("Map::Make Map", {});
    Require(isMap(made), "Map::Make Map should create a map.");
    ObjMap* map = asMap(made);

    Require(asBoolean(fixture.CallNative("Map::Set", { made, StringValue("first"), Value(1.0) })),
            "Map::Set should report a newly added key.");
    Require(asBoolean(fixture.CallNative("Map::Set", { made, StringValue("second"), Value(2.0) })),
            "Map::Set should add a second key.");
    Require(!asBoolean(fixture.CallNative("Map::Set", { made, StringValue("first"), Value(3.0) })),
            "Map::Set should report replacement of an existing key.");
    Require(map->size() == 2 && map->entries.size() == 2,
            "Replacing a map value should update its entry in place.");
    Require(map->replaceKey(StringValue("first"), StringValue("renamed")) && map->entries.size() == 2 &&
            asString(map->entryAt(0)->key)->chars == "renamed" && asNumber(map->entryAt(1)->value) == 2.0,
            "Editing one map key should preserve its entry position and the other entries.");
    Require(!map->replaceKey(StringValue("renamed"), StringValue("second")) && map->get(StringValue("renamed"), nullptr) &&
            map->get(StringValue("second"), nullptr) && map->replaceKey(StringValue("renamed"), StringValue("first")),
            "Editing a key to an existing key should not overwrite either entry.");

    const Value found = fixture.CallCollectionNode("Map::Find", { made, StringValue("first") });
    Require(isList(found) && asList(found)->items.size() == 2 && isBoolean(asList(found)->items[0]) && asBoolean(asList(found)->items[0]) &&
            isNumber(asList(found)->items[1]) && asNumber(asList(found)->items[1]) == 3.0,
            "Map::Find should return Found followed by the associated value.");
    const Value missing = fixture.CallCollectionNode("Map::Find", { made, StringValue("missing") });
    Require(isList(missing) && !asBoolean(asList(missing)->items[0]) && isNil(asList(missing)->items[1]),
            "Map::Find should return false and nil for an absent key.");
    Require(asBoolean(fixture.CallCollectionNode("Map::Contains Key", { made, StringValue("second") })),
            "Map::Contains Key should find an existing key.");
    Require(asNumber(fixture.CallCollectionNode("Map::Length", { made })) == 2.0,
            "Map::Length should count active entries.");

    const Value keys = fixture.CallCollectionNode("Map::Keys", { made });
    const Value values = fixture.CallCollectionNode("Map::Values", { made });
    Require(isList(keys) && asList(keys)->items.size() == 2 && asString(asList(keys)->items[0])->chars == "first" && asString(asList(keys)->items[1])->chars == "second",
            "Map::Keys should preserve insertion order after replacement.");
    Require(isList(values) && asNumber(asList(values)->items[0]) == 3.0 && asNumber(asList(values)->items[1]) == 2.0,
            "Map::Values should match key insertion order.");

    const Value removed = fixture.CallNative("Map::Remove", { made, StringValue("first") });
    Require(isList(removed) && asBoolean(asList(removed)->items[0]) && asNumber(asList(removed)->items[1]) == 3.0 && map->size() == 1,
            "Map::Remove should return Found and the removed value.");
    fixture.CallNative("Map::Set", { made, StringValue("first"), Value(4.0) });
    const Value reorderedKeys = fixture.CallCollectionNode("Map::Keys", { made });
    Require(asString(asList(reorderedKeys)->items[0])->chars == "second" && asString(asList(reorderedKeys)->items[1])->chars == "first",
            "Removing and re-adding a key should move it to the end.");

    ObjClass* klass = newClass(copyString("Key", 3));
    ObjInstance* firstInstance = newInstance(klass);
    ObjInstance* secondInstance = newInstance(klass);
    fixture.CallNative("Map::Set", { made, Value(firstInstance), StringValue("identity") });
    const Value sameInstance = fixture.CallCollectionNode("Map::Find", { made, Value(firstInstance) });
    const Value otherInstance = fixture.CallCollectionNode("Map::Find", { made, Value(secondInstance) });
    Require(asBoolean(asList(sameInstance)->items[0]) && asString(asList(sameInstance)->items[1])->chars == "identity" && !asBoolean(asList(otherInstance)->items[0]),
            "Class instances should use identity semantics as map keys.");

    const Value copied = fixture.CallCollectionNode("Map::Copy", { made });
    Require(isMap(copied) && asMap(copied) != map && asMap(copied)->size() == map->size(),
            "Map::Copy should create a shallow container copy.");
    fixture.CallNative("Map::Clear", { made });
    Require(map->size() == 0, "Map::Clear should remove every entry.");

    const NativeFunctionDef* makeDefinition = fixture.registry.FindNative("Map::Make Map");
    const NativeFunctionDef* findDefinition = fixture.registry.FindNative("Map::Find");
    Require(makeDefinition && makeDefinition->functionDef->genericTypeProperties.size() == 2 && findDefinition &&
            findDefinition->functionDef->outputs[0].name == "Found" && findDefinition->functionDef->outputs[1].name == "Value",
            "Map definitions should expose key/value generics and the requested Find output order.");
}

void FilePathAndConsoleNodesOperate()
{
    RuntimeFixture fixture;
    const auto unique =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("visual-lox-tests-" + std::to_string(unique));
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    Require(!error, "Expected to create a standard-library test directory.");
    const std::filesystem::path file = directory / "sample.txt";
    const std::string fileText = file.string();

    const Value write = fixture.CallNative(
        "File::Write Text", { StringValue(fileText), StringValue("first") });
    const Value append = fixture.CallNative(
        "File::Append Text", { StringValue(fileText), StringValue("-second") });
    const Value read =
        fixture.CallNative("File::Read Text", { StringValue(fileText) });
    const Value listing = fixture.CallNative(
        "File::List Directory", { StringValue(directory.string()) });
    Require(isList(write) && asBoolean(asList(write)->items[0]) &&
            isList(append) && asBoolean(asList(append)->items[0]),
            "File write and append should return structured success results.");
    Require(isList(read) && asString(asList(read)->items[0])->chars ==
                "first-second" && asBoolean(asList(read)->items[1]),
            "File::Read Text should return content, success, and error outputs.");
    Require(isList(listing) && isList(asList(listing)->items[0]) &&
            asList(asList(listing)->items[0])->items.size() == 1 &&
            asBoolean(asList(listing)->items[1]),
            "File::List Directory should return entries and status.");

    const Value combined = fixture.CallNative("Path::Combine",
        { StringValue(directory.string()), StringValue("sample.txt") });
    const Value extension =
        fixture.CallNative("Path::Extension", { StringValue(fileText) });
    const Value filename =
        fixture.CallNative("Path::Filename", { StringValue(fileText) });
    const Value parent =
        fixture.CallNative("Path::Parent", { StringValue(fileText) });
    Require(isString(combined) &&
            std::filesystem::path(asString(combined)->chars) == file &&
            asString(extension)->chars == ".txt" &&
            asString(filename)->chars == "sample.txt" &&
            std::filesystem::path(asString(parent)->chars) == directory,
            "Path nodes should expose combine, extension, filename, and parent.");

    std::istringstream input("typed input\n");
    std::streambuf* originalInput = std::cin.rdbuf(input.rdbuf());
    const Value console = fixture.CallNative("Console::Read Input", {});
    std::cin.rdbuf(originalInput);
    std::cin.clear();
    Require(isString(console) && asString(console)->chars == "typed input",
            "Console::Read Input should return one line.");

    setInputProvider([]() { return std::string("provided input"); });
    const Value providedConsole = fixture.CallNative("Console::Read Input", {});
    clearInputProvider();
    Require(isString(providedConsole) && asString(providedConsole)->chars == "provided input",
            "Console::Read Input should use an installed input provider.");

    std::filesystem::remove_all(directory, error);
    Require(!error, "Expected to clean up the standard-library test directory.");
}

void JsonTextMathAndCollectionNodesOperate()
{
    RuntimeFixture fixture;
    const Value parsed = fixture.CallNative("JSON::Parse", { StringValue(R"({"name":"Ada","scores":[3,5],"active":true})") });
    const Value json = asList(parsed)->items[0];
    Value jsonClass;
    Require(isList(parsed) && asList(parsed)->items.size() == 3 && asBoolean(asList(parsed)->items[1]) &&
            fixture.vm.globalTable().get(copyString("JsonValue", 9), &jsonClass) && isClass(jsonClass) && isInstance(json) &&
            asInstance(json)->klass == asClass(jsonClass) && TypeOfValue(json) == TypeRef::Object("JsonValue"),
            "JSON::Parse should return an instance of the native JsonValue class.");
    Require(asString(fixture.CallNative("JSON::Kind", { json }))->chars == "Object",
            "JSON::Kind should make a parsed value's runtime shape explicit.");
    fixture.vm.setExternalMarkingFunc([&fixture]() { MarkNodeRegistryRoots(fixture.registry, fixture.vm); });
    fixture.vm.push(json);
    fixture.vm.allowGarbageCollection(true);
    fixture.vm.collectGarbage();
    const Value afterCollection = fixture.CallNative("JSON::Stringify", { json });
    fixture.vm.allowGarbageCollection(false);
    fixture.vm.pop();
    Require(asBoolean(asList(afterCollection)->items[1]) && asString(asList(afterCollection)->items[0])->chars.find("\"Ada\"") != std::string::npos,
            "JsonValue instances should keep their complete recursive payload alive during garbage collection.");

    const Value nameMember = fixture.CallNative("JSON::Get", { json, StringValue("name") });
    const Value name = fixture.CallNative("JSON::As String", { asList(nameMember)->items[0] });
    const Value scoresMember = fixture.CallNative("JSON::Get", { json, StringValue("scores") });
    const Value scores = fixture.CallNative("JSON::As Array", { asList(scoresMember)->items[0] });
    const Value secondScore = fixture.CallNative("JSON::As Number", { asList(asList(scores)->items[0])->items[1] });
    Require(asBoolean(asList(nameMember)->items[1]) && asBoolean(asList(name)->items[1]) && asString(asList(name)->items[0])->chars == "Ada" &&
            asBoolean(asList(scoresMember)->items[1]) && asBoolean(asList(scores)->items[1]) && asList(asList(scores)->items[0])->items.size() == 2 &&
            asBoolean(asList(secondScore)->items[1]) && asNumber(asList(secondScore)->items[0]) == 5.0,
            "Typed JSON accessors should read object, array, string, and number values without implicit Any outputs.");

    const Value native = fixture.CallNative("JSON::To Native", { json });
    Value nativeName;
    Require(asBoolean(asList(native)->items[1]) && isMap(asList(native)->items[0]) &&
            asMap(asList(native)->items[0])->get(StringValue("name"), &nativeName) && asString(nativeName)->chars == "Ada",
            "JSON::To Native should provide an explicit bridge to ordinary Vlox maps and lists.");

    const Value compact = fixture.CallNative("JSON::Stringify", { json });
    const Value pretty = fixture.CallNative("JSON::Pretty Print", { json, Value(2.0) });
    Require(asBoolean(asList(compact)->items[1]) && asString(asList(compact)->items[0])->chars.find("\"Ada\"") != std::string::npos &&
            asBoolean(asList(pretty)->items[1]) && asString(asList(pretty)->items[0])->chars.find('\n') != std::string::npos,
            "JSON stringify nodes should support compact and pretty output.");
    const Value entries = fixture.CallNative("JSON::Object To Entries", { json });
    const Value remapped = fixture.CallNative("JSON::Entries To Object", { entries });
    Require(isList(entries) && asList(entries)->items.size() == 3 && isInstance(asList(asList(entries)->items[0])->items[1]) &&
            asBoolean(asList(remapped)->items[1]) && isInstance(asList(remapped)->items[0]),
            "JSON object/list mapping should round-trip JsonValue entries.");

    const Value regexSearch = fixture.CallNative("Regex::Search", { StringValue("item-42"), StringValue(R"((\w+)-(\d+))") });
    Require(asBoolean(asList(regexSearch)->items[0]) && asString(asList(regexSearch)->items[1])->chars == "item-42" &&
            asNumber(asList(regexSearch)->items[2]) == 0.0 && asList(asList(regexSearch)->items[3])->items.size() == 2,
            "Regex::Search should report the match, byte index, and capture groups.");
    const Value regexReplace = fixture.CallNative("Regex::Replace", { StringValue("a1 b2"), StringValue(R"(\d)"), StringValue("#") });
    Require(asString(asList(regexReplace)->items[0])->chars == "a# b#" && asBoolean(asList(regexReplace)->items[1]),
            "Regex::Replace should replace every match.");

    const std::string unicode = "A\xF0\x9F\x98\x80\xC3\xA9";
    Require(asNumber(fixture.CallNative("String::Unicode Length", { StringValue(unicode) })) == 3.0 &&
            asString(fixture.CallNative("String::Unicode Substring", { StringValue(unicode), Value(1.0), Value(1.0) }))->chars == "\xF0\x9F\x98\x80",
            "Unicode string nodes should count and slice code points rather than UTF-8 bytes.");
    Require(asString(fixture.CallNative("String::Pad Left", { StringValue("7"), Value(3.0), StringValue("0") }))->chars == "007" &&
            asString(fixture.CallNative("String::Repeat", { StringValue("ab"), Value(3.0) }))->chars == "ababab" &&
            asNumber(fixture.CallNative("String::Count", { StringValue("aaaa"), StringValue("aa") })) == 2.0,
            "Padding, repeat, and count should expose common text operations.");
    const Value lines = fixture.CallNative("String::Lines", { StringValue("a\r\nb\nc") });
    const Value base64 = fixture.CallNative("Encoding::Base64 Encode", { StringValue("hello") });
    const Value decoded = fixture.CallNative("Encoding::Base64 Decode", { base64 });
    Require(asList(lines)->items.size() == 3 && asString(asList(lines)->items[1])->chars == "b" && asString(base64)->chars == "aGVsbG8=" &&
            asBoolean(asList(decoded)->items[1]) && asString(asList(decoded)->items[0])->chars == "hello",
            "Line and base64 nodes should preserve text content.");

    fixture.CallNative("Math::Random Seed", { Value(123.0) });
    const Value randomFirst = fixture.CallNative("Math::Random Integer", { Value(1.0), Value(1000.0) });
    fixture.CallNative("Math::Random Seed", { Value(123.0) });
    const Value randomSecond = fixture.CallNative("Math::Random Integer", { Value(1.0), Value(1000.0) });
    const double pi = asNumber(fixture.CallNative("Math::Pi", {}));
    Require(randomFirst == randomSecond && std::fabs(asNumber(fixture.CallNative("Math::Sin", { Value(pi / 2.0) })) - 1.0) < 1e-12 &&
            std::fabs(asNumber(fixture.CallNative("Math::Log", { Value(std::exp(2.0)) })) - 2.0) < 1e-12,
            "Seeded random, trigonometric, and logarithm nodes should be deterministic and accurate.");

    ObjList* source = MakeList({ Value(1.0), Value(2.0), Value(3.0), Value(4.0), Value(2.0) });
    const Value count = fixture.CallNative("List::Count Value", { Value(source), Value(2.0) });
    const Value removed = fixture.CallNative("List::Remove Value", { Value(source), Value(2.0) });
    const Value chunks = fixture.CallNative("List::Chunk", { Value(source), Value(2.0) });
    const Value taken = fixture.CallNative("List::Take", { Value(source), Value(2.0) });
    const Value skipped = fixture.CallNative("List::Skip", { Value(source), Value(3.0) });
    Require(asNumber(count) == 2.0 && asNumber(asList(removed)->items[1]) == 2.0 && asList(asList(removed)->items[0])->items.size() == 3 &&
            asList(chunks)->items.size() == 3 && asList(taken)->items.size() == 2 && asList(skipped)->items.size() == 2,
            "Count, remove, chunk, take, and skip should operate on list copies.");
    ObjList* nested = MakeList({ Value(MakeList({ Value(1.0), Value(2.0) })), Value(MakeList({ Value(3.0) })) });
    Require(asList(fixture.CallNative("List::Flatten", { Value(nested) }))->items.size() == 3,
            "List::Flatten should flatten one nesting level.");

    const Value isEven(newNative(1, &IsEvenTestNative, false));
    const Value parity(newNative(1, &ParityTestNative, false));
    const Value any = fixture.CallNative("Functional::Any", { Value(source), isEven });
    const Value all = fixture.CallNative("Functional::All", { Value(source), isEven });
    const Value matching = fixture.CallNative("Functional::Count", { Value(source), isEven });
    const Value found = fixture.CallNative("Functional::Find", { Value(source), isEven });
    const Value groups = fixture.CallNative("Functional::Group By", { Value(source), parity });
    Value evenGroup;
    Require(asBoolean(any) && !asBoolean(all) && asNumber(matching) == 3.0 && asBoolean(asList(found)->items[1]) &&
            asNumber(asList(found)->items[0]) == 2.0 && isMap(groups) && asMap(groups)->get(StringValue("even"), &evenGroup) && asList(evenGroup)->items.size() == 3,
            "Any, All, Count, Find, and Group By should invoke typed callbacks.");
}

void ExtendedFileProcessAndTimeNodesOperate()
{
    RuntimeFixture fixture;
    const auto unique = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("visual-lox-extended-" + std::to_string(unique));
    const std::filesystem::path source = root / "source.bin";
    const std::filesystem::path copied = root / "copied.bin";
    const std::filesystem::path moved = root / "moved.bin";
    const std::filesystem::path jsonFile = root / "config.json";
    const std::filesystem::path textFile = root / "encoded.txt";
    std::error_code cleanupError;

    const Value created = fixture.CallNative("Directory::Create", { StringValue(root.string()), Value(true) });
    ObjList* bytes = MakeList({ Value(0.0), Value(127.0), Value(255.0) });
    const Value written = fixture.CallNative("File::Write Bytes", { StringValue(source.string()), Value(bytes), Value(false) });
    const Value read = fixture.CallNative("File::Read Bytes", { StringValue(source.string()) });
    const Value metadata = fixture.CallNative("File::Metadata", { StringValue(source.string()) });
    Require(asBoolean(asList(created)->items[0]) && asBoolean(asList(written)->items[0]) && asBoolean(asList(read)->items[1]) &&
            asList(asList(read)->items[0])->items.size() == 3 && asNumber(asList(asList(read)->items[0])->items[2]) == 255.0 &&
            asBoolean(asList(metadata)->items[0]) && asBoolean(asList(metadata)->items[1]) && asNumber(asList(metadata)->items[3]) == 3.0,
            "Directory, binary file, and metadata nodes should preserve bytes and report file size.");
    ObjMap* configuration = newMap();
    configuration->set(StringValue("enabled"), Value(true));
    const Value configurationJson = fixture.CallNative("JSON::From Native", { Value(configuration) });
    const Value jsonWritten = fixture.CallNative("JSON::Write File",
        { StringValue(jsonFile.string()), asList(configurationJson)->items[0], Value(true), Value(2.0), Value(false) });
    const Value jsonRead = fixture.CallNative("JSON::Read File", { StringValue(jsonFile.string()) });
    const Value enabledMember = fixture.CallNative("JSON::Get", { asList(jsonRead)->items[0], StringValue("enabled") });
    const Value enabled = fixture.CallNative("JSON::As Boolean", { asList(enabledMember)->items[0] });
    Require(asBoolean(asList(configurationJson)->items[1]) && asBoolean(asList(jsonWritten)->items[0]) && asBoolean(asList(jsonRead)->items[1]) &&
            asBoolean(asList(enabledMember)->items[1]) && asBoolean(asList(enabled)->items[1]) && asBoolean(asList(enabled)->items[0]),
            "JSON file helpers should serialize and parse structured files directly.");
    const Value textWritten = fixture.CallNative("File::Write Text Encoded",
        { StringValue(textFile.string()), StringValue("plain-ascii"), StringValue("ascii"), Value(false) });
    const Value overwriteRejected = fixture.CallNative("File::Write Text Encoded",
        { StringValue(textFile.string()), StringValue("replacement"), StringValue("ascii"), Value(false) });
    const Value textRead = fixture.CallNative("File::Read Text Encoded", { StringValue(textFile.string()), StringValue("ascii") });
    Require(asBoolean(asList(textWritten)->items[0]) && !asBoolean(asList(overwriteRejected)->items[0]) && asBoolean(asList(textRead)->items[1]) &&
            asString(asList(textRead)->items[0])->chars == "plain-ascii",
            "Encoded text helpers should validate encodings and reject implicit overwrites.");
    Require(asBoolean(asList(fixture.CallNative("File::Copy", { StringValue(source.string()), StringValue(copied.string()), Value(false) }))->items[0]) &&
            asBoolean(asList(fixture.CallNative("File::Move", { StringValue(copied.string()), StringValue(moved.string()), Value(false) }))->items[0]) &&
            std::filesystem::exists(moved) && !std::filesystem::exists(copied),
            "Copy and move nodes should honor destination paths.");
    const Value absolute = fixture.CallNative("Path::Absolute", { StringValue(source.string()) });
    const Value canonical = fixture.CallNative("Path::Canonical", { StringValue(source.string()) });
    const Value relative = fixture.CallNative("Path::Relative", { StringValue(source.string()), StringValue(root.string()) });
    Require(asBoolean(asList(absolute)->items[1]) && asBoolean(asList(canonical)->items[1]) && asString(asList(relative)->items[0])->chars == "source.bin" &&
            asString(fixture.CallNative("Path::Stem", { StringValue(source.string()) }))->chars == "source",
            "Absolute, canonical, relative, and stem path nodes should resolve expected paths.");

    ObjList* arguments = newList();
#ifdef _WIN32
    const std::string executable = "cmd.exe";
    arguments->append(StringValue("/d"));
    arguments->append(StringValue("/c"));
    arguments->append(StringValue("echo captured-output"));
#else
    const std::string executable = "/bin/sh";
    arguments->append(StringValue("-c"));
    arguments->append(StringValue("printf captured-output"));
#endif
    const Value process = fixture.CallNative("Process::Run",
        { StringValue(executable), Value(arguments), StringValue(root.string()), Value(newMap()), Value(5.0) });
    Require(asBoolean(asList(process)->items[5]) && asNumber(asList(process)->items[0]) == 0.0 &&
            asString(asList(process)->items[1])->chars.find("captured-output") != std::string::npos,
            "Process::Run should capture stdout and return the executable exit code.");
    const NativeFunctionDef* runCommand = fixture.registry.FindNative("System::RunCommand");
    Require(runCommand && runCommand->functionDef->revision == 2 && runCommand->functionDef->inputs.size() == 5 && runCommand->functionDef->outputs.size() == 7,
            "System::RunCommand should use the structured process schema instead of a shell command string.");

    const Value started = fixture.CallNative("Process::Start",
        { StringValue(executable), Value(arguments), StringValue(root.string()), Value(newMap()), Value(5.0) });
    Require(asBoolean(asList(started)->items[1]), "Process::Start should return an asynchronous handle.");
    Value polled;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        polled = fixture.CallNative("Process::Poll", { asList(started)->items[0] });
        if (asBoolean(asList(polled)->items[0]))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Require(isList(polled) && asBoolean(asList(polled)->items[0]) && asString(asList(polled)->items[2])->chars.find("captured-output") != std::string::npos,
            "Process::Poll should complete without blocking and retain captured output.");

    ObjList* slowArguments = newList();
#ifdef _WIN32
    slowArguments->append(StringValue("/d"));
    slowArguments->append(StringValue("/c"));
    slowArguments->append(StringValue("ping -n 3 127.0.0.1 >nul"));
#else
    slowArguments->append(StringValue("-c"));
    slowArguments->append(StringValue("sleep 1"));
#endif
    const Value timedOut = fixture.CallNative("Process::Run",
        { StringValue(executable), Value(slowArguments), StringValue(root.string()), Value(newMap()), Value(0.05) });
    Require(asBoolean(asList(timedOut)->items[3]) && !asBoolean(asList(timedOut)->items[5]),
            "Process::Run should terminate and report processes that exceed their timeout.");
    const Value cancellable = fixture.CallNative("Process::Start",
        { StringValue(executable), Value(slowArguments), StringValue(root.string()), Value(newMap()), Value(5.0) });
    const Value cancelled = fixture.CallNative("Process::Cancel", { asList(cancellable)->items[0] });
    Require(asBoolean(asList(cancelled)->items[0]), "Process::Cancel should accept an active asynchronous handle.");
    Value cancelledPoll;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        cancelledPoll = fixture.CallNative("Process::Poll", { asList(cancellable)->items[0] });
        if (asBoolean(asList(cancelledPoll)->items[0]))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Require(asBoolean(asList(cancelledPoll)->items[0]) && asBoolean(asList(cancelledPoll)->items[5]),
            "Cancelled asynchronous processes should finish with explicit cancellation status.");

    const Value parsed = fixture.CallNative("Time::Parse", { StringValue("2024-01-02 03:04:05"), StringValue("%Y-%m-%d %H:%M:%S"), Value(true) });
    const Value formatted = fixture.CallNative("Time::Format", { asList(parsed)->items[0], StringValue("%Y-%m-%d %H:%M:%S"), Value(true) });
    Require(asBoolean(asList(parsed)->items[1]) && asBoolean(asList(formatted)->items[1]) && asString(asList(formatted)->items[0])->chars == "2024-01-02 03:04:05" &&
            asNumber(fixture.CallNative("Duration::From Milliseconds", { Value(1500.0) })) == 1.5,
            "Date/time parsing, formatting, and duration conversion should round-trip UTC values.");
    timerCallbackCount = 0;
    const Value timerCallback(newNative(0, &TimerCallbackTestNative, false));
    const Value afterTimer = fixture.CallNative("Timer::After", { Value(0.0), timerCallback });
    Require(isNumber(afterTimer) && HasPendingStandardLibraryTimers(fixture.vm),
            "Timer::After should return a handle and retain its callback until it fires.");
    fixture.vm.setExternalMarkingFunc([&fixture]() { MarkNodeRegistryRoots(fixture.registry, fixture.vm); });
    fixture.vm.allowGarbageCollection(true);
    fixture.vm.collectGarbage();
    Require(PumpStandardLibraryTimers(fixture.vm) && timerCallbackCount == 1 && !HasPendingStandardLibraryTimers(fixture.vm),
            "A due one-shot timer should invoke its callback exactly once, including after garbage collection.");
    fixture.vm.allowGarbageCollection(false);

    const Value repeatingTimer = fixture.CallNative("Timer::Every", { Value(0.001), timerCallback });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Require(PumpStandardLibraryTimers(fixture.vm) && timerCallbackCount == 2,
            "Timer::Every should invoke its callback when its interval elapses.");
    const Value cancelledTimer = fixture.CallNative("Timer::Cancel", { repeatingTimer });
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    Require(asBoolean(asList(cancelledTimer)->items[0]) && PumpStandardLibraryTimers(fixture.vm) && timerCallbackCount == 2,
            "Timer::Cancel should prevent future repeating callbacks.");

    fixture.CallNative("Timer::After", { Value(0.001), timerCallback });
    Require(RunStandardLibraryTimers(fixture.vm) && timerCallbackCount == 3 && !HasPendingStandardLibraryTimers(fixture.vm),
            "The synchronous host loop should wait for one-shot timers without blocking timer creation.");

    fixture.CallNative("File::Delete", { StringValue(root.string()), Value(true) });
    std::filesystem::remove_all(root, cleanupError);
    Require(!std::filesystem::exists(root), "Extended filesystem tests should clean up their temporary directory.");
}

void NewLinksReplaceOccupiedConnections()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");
    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    const CompiledNodeDefPtr addDefinition = fixture.registry.FindCompiled("Math::Add");
    const CompiledNodeDefPtr printDefinition = fixture.registry.FindCompiled("Debug::Print");
    NodePtr add = addDefinition->MakeNode(fixture.ids);
    NodePtr print = printDefinition->MakeNode(fixture.ids);
    NodePtr replacementAdd = addDefinition->MakeNode(fixture.ids);
    NodePtr replacementPrint = printDefinition->MakeNode(fixture.ids);
    for (const NodePtr& node : { begin, add, print, replacementAdd, replacementPrint })
        AttachNode(script.main->Graph, node);
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        begin->Outputs[0].ID, print->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        add->Outputs[0].ID, print->Inputs[1].ID));

    Link replacementData(fixture.ids.GetNextId(),
        replacementAdd->Outputs[0].ID, print->Inputs[1].ID);
    Require(script.main->Graph.CanCreateLink(
                &replacementAdd->Outputs[0], &print->Inputs[1], {}) ==
                ELinkQueryResult::Possible,
            "An occupied data input should accept a valid replacement connection.");
    script.main->Graph.AddLink(replacementData);
    Require(script.main->Graph.GetLinks().size() == 2,
            "Replacing a data input connection should remove the previous link.");
    Require(GraphUtils::FindConnectedOutput(script.main->Graph, print->Inputs[1])->Node ==
                replacementAdd,
            "The replacement data connection should become active.");

    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(),
        begin->Outputs[0].ID, replacementPrint->Inputs[0].ID));
    Require(script.main->Graph.GetLinks().size() == 2,
            "Replacing a flow output connection should remove the previous link.");
    const std::vector<const Pin*> targets =
        GraphUtils::FindConnectedInputs(script.main->Graph, begin->Outputs[0]);
    Require(targets.size() == 1 && targets[0]->Node == replacementPrint,
            "The replacement flow connection should become active.");
}

void TypeDescriptorsAreDirectionalAndComposable()
{
    const TypeRef stringList = TypeRef::List(PinType::String);
    const TypeRef anyList = TypeRef::List(PinType::Any);
    Require(CanAssign(stringList, anyList),
            "A specifically typed list should be assignable to List<Any>.");
    Require(!CanAssign(anyList, stringList),
            "List<Any> should not silently become List<String>.");
    Require(CanAssign(PinType::Nil, PinType::String) &&
            CanAssign(PinType::Nil, TypeRef::List(PinType::Float)),
            "Nil should be assignable to every runtime value declaration.");
    Require(!CanAssign(TypeRef::Object(10, "Student"),
                       TypeRef::Object(11, "Teacher")),
            "Different script classes should be distinct types.");

    ObjList* strings = MakeList({ StringValue("Ada"), StringValue("Grace") });
    Require(TypeOfValue(Value(strings)) == stringList,
            "List literal inference should inspect its element values.");

    const TypeRef scoreMap = TypeRef::Map(PinType::String, PinType::Float);
    ObjMap* scores = newMap();
    scores->set(StringValue("Ada"), Value(10.0));
    Require(TypeOfValue(Value(scores)) == scoreMap && scoreMap.ToString() == "Map<String, Number>",
            "Map inference and display should retain key and value types.");
    Require(CanAssign(scoreMap, scoreMap) && !CanAssign(scoreMap, TypeRef::Map(PinType::Any, PinType::Float)) &&
            !CanAssign(TypeRef::Map(PinType::String, PinType::Any), scoreMap),
            "Mutable map key and value parameters should be invariant.");

    ObjMap* numericKeys = newMap();
    numericKeys->set(Value(0.0), StringValue("preserved"));
    Value nextKey;
    Value originalValue;
    Require(MakeUniqueMapKey(*numericKeys, PinType::Int, nextKey) && isNumber(nextKey) && asNumber(nextKey) == 1.0 &&
            numericKeys->get(Value(0.0), &originalValue) && isString(originalValue) && asString(originalValue)->chars == "preserved",
            "Adding a default map entry should choose an unused key without replacing the existing entry.");

    const TypeRef signature = TypeRef::Function(
        { PinType::String, TypeRef::List(TypeRef::Object(10, "Student")) },
        { PinType::Float });
    Require(signature.ToString().find("Student") != std::string::npos &&
            signature.ToString().find("Number") != std::string::npos,
            "Function signatures should retain nested declaration types.");
    Require(CanAssign(signature, signature) &&
            !CanAssign(signature,
                TypeRef::Function({ PinType::String }, { PinType::Float })) &&
            CanAssign(signature, TypeRef(PinType::Function)),
            "Typed function signatures should be exact while bare Function remains dynamic.");
    Require(CanAssign(
                TypeRef::Function({ PinType::Any }, { PinType::String }),
                TypeRef::Function({ PinType::String }, { PinType::Any })) &&
            !CanAssign(
                TypeRef::Function({ PinType::String }, { PinType::Any }),
                TypeRef::Function({ PinType::Any }, { PinType::String }),
                false),
            "Function inputs should be contravariant and outputs covariant.");
}

void GenericNodesInferAcrossConnections()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");

    ScriptPropertyPtr students =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Names");
    students->type = TypeRef::List(PinType::String);
    students->defaultValue = Value(newList());
    script.variables.push_back(students);

    NodePtr source = BuildGetVariableNode(fixture.ids, students);
    NodePtr forIn = fixture.registry.FindCompiled("Flow::For In")->MakeNode(fixture.ids);
    AttachNode(script.main->Graph, source);
    AttachNode(script.main->Graph, forIn);

    Link link(fixture.ids.GetNextId(), source->Outputs[0].ID, forIn->Inputs[1].ID);
    script.main->Graph.AddLink(link);
    Require(forIn->Inputs[1].Type == TypeRef::List(PinType::String),
            "For In should retain the connected list type.");
    Require(forIn->Outputs[1].Type == PinType::String,
            "For In should infer its value output from List<String>.");

    script.main->Graph.DeleteLink(link.ID);
    Require(forIn->Inputs[1].Type == TypeRef::Iterable(PinType::Any) &&
            forIn->Outputs[1].Type == PinType::Any,
            "Removing a constraint should reset a generic node to Any.");

    ScriptPropertyPtr range =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Range");
    range->type = PinType::Range;
    range->defaultValue = Value(newRange(0.0, 2.0));
    NodePtr rangeSource = BuildGetVariableNode(fixture.ids, range);
    AttachNode(script.main->Graph, rangeSource);
    Link rangeLink(fixture.ids.GetNextId(), rangeSource->Outputs[0].ID,
                   forIn->Inputs[1].ID);
    script.main->Graph.AddLink(rangeLink);
    Require(forIn->Inputs[1].Type == PinType::Range &&
            forIn->Outputs[1].Type == PinType::Float,
            "For In should infer Number while iterating a range.");
    script.main->Graph.DeleteLink(rangeLink.ID);

    ScriptPropertyPtr text =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Text");
    text->type = PinType::String;
    text->defaultValue = StringValue("abc");
    NodePtr textSource = BuildGetVariableNode(fixture.ids, text);
    AttachNode(script.main->Graph, textSource);
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), textSource->Outputs[0].ID,
        forIn->Inputs[1].ID));
    Require(forIn->Inputs[1].Type == PinType::String &&
            forIn->Outputs[1].Type == PinType::String,
            "For In should infer String while iterating a string.");

    NodePtr equals = fixture.registry.FindCompiled("Math::Equals")->MakeNode(fixture.ids);
    ScriptPropertyPtr name =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Name");
    name->type = PinType::String;
    name->defaultValue = StringValue("");
    NodePtr nameSource = BuildGetVariableNode(fixture.ids, name);
    AttachNode(script.main->Graph, equals);
    AttachNode(script.main->Graph, nameSource);
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), nameSource->Outputs[0].ID, equals->Inputs[0].ID));
    Require(equals->Inputs[0].Type == PinType::String &&
            equals->Inputs[1].Type == PinType::String,
            "A shared Equals<T> constraint should update both operands.");

    NodePtr filter =
        fixture.registry.FindNative("Functional::Filter")->functionDef->MakeNode(
            fixture.ids, ScriptElementID::Invalid);
    ScriptPropertyPtr predicate =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Predicate");
    predicate->type =
        TypeRef::Function({ PinType::String }, { PinType::Bool });
    predicate->defaultValue = Value();
    NodePtr predicateSource = BuildGetVariableNode(fixture.ids, predicate);
    AttachNode(script.main->Graph, filter);
    AttachNode(script.main->Graph, predicateSource);

    Require(script.main->Graph.CanCreateLink(
                &predicateSource->Outputs[0], &filter->Inputs[1], {}) ==
                ELinkQueryResult::Possible,
            "Filter should accept a typed predicate before its iterable is connected.");
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), predicateSource->Outputs[0].ID,
        filter->Inputs[1].ID));
    Require(filter->Inputs[0].Type == TypeRef::Iterable(PinType::String) &&
            filter->Inputs[1].Type ==
                TypeRef::Function({ PinType::String }, { PinType::Bool }) &&
            filter->Outputs[0].Type == TypeRef::List(PinType::String),
            "Filter should infer T from Function<(T) -> (Bool)>.");

    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), source->Outputs[0].ID, filter->Inputs[0].ID));
    Require(filter->Inputs[0].Type == TypeRef::List(PinType::String) &&
            filter->Outputs[0].Type == TypeRef::List(PinType::String),
            "Filter should preserve List<T> as its output type.");

    NodePtr numericEquals =
        fixture.registry.FindCompiled("Math::Equals")->MakeNode(fixture.ids);
    ScriptPropertyPtr number =
        std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Number");
    number->type = PinType::Float;
    number->defaultValue = Value(0.0);
    NodePtr numberSource = BuildGetVariableNode(fixture.ids, number);
    AttachNode(script.main->Graph, numericEquals);
    AttachNode(script.main->Graph, numberSource);
    script.main->Graph.AddLink(Link(
        fixture.ids.GetNextId(), numberSource->Outputs[0].ID,
        numericEquals->Inputs[0].ID));
    Require(numericEquals->Inputs[1].Type == PinType::Float &&
            isNumber(numericEquals->Inputs[1].LiteralValue) &&
            asNumber(numericEquals->Inputs[1].LiteralValue) == 0.0,
            "An inferred numeric Equals operand should receive a numeric zero default.");

    ScriptPropertyPtr labels = std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Labels");
    labels->type = TypeRef::Map(PinType::Int, PinType::String);
    labels->defaultValue = Value(newMap());
    script.variables.push_back(labels);
    NodePtr labelsSource = BuildGetVariableNode(fixture.ids, labels);
    NodePtr find = fixture.registry.FindNative("Map::Find")->functionDef->MakeNode(fixture.ids, ScriptElementID::Invalid);
    AttachNode(script.main->Graph, labelsSource);
    AttachNode(script.main->Graph, find);
    Require(script.main->Graph.CanCreateLink(&labelsSource->Outputs[0], &find->Inputs[0], {}) == ELinkQueryResult::Possible,
            "A concrete Map<Int, String> should connect to an unbound Map<K, V> input.");
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), labelsSource->Outputs[0].ID, find->Inputs[0].ID));
    Require(find->Inputs[0].Type == TypeRef::Map(PinType::Int, PinType::String) && find->Inputs[1].Type == PinType::Int && find->Outputs[1].Type == PinType::String &&
            find->ResolvedTypeVariables.at("K") == PinType::Int && find->ResolvedTypeVariables.at("V") == PinType::String,
            "Map nodes should infer K and V from a connected concrete map.");
}

void PureGraphsRejectImpureNodes()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");
    AttachNode(script.main->Graph, BuildBeginNode(fixture.ids, script.main));

    ScriptFunctionPtr pure =
        std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "PureFunction");
    pure->functionDef->flags |= NodeDefinitionFlags::Pure;
    pure->functionDef->outputs.push_back(
        { "Result", Value(false), fixture.ids.GetNextId(),
          TypeRef(PinType::Bool), "The computed result." });
    NodePtr begin = BuildBeginNode(fixture.ids, pure);
    NodePtr returnNode = BuildReturnNode(fixture.ids, *pure);
    NodePtr print =
        fixture.registry.FindCompiled("Debug::Print")->MakeNode(fixture.ids);
    NodePtr branch =
        fixture.registry.FindCompiled("Flow::Branch")->MakeNode(fixture.ids);
    AttachNode(pure->Graph, begin);
    AttachNode(pure->Graph, returnNode);
    AttachNode(pure->Graph, print);
    AttachNode(pure->Graph, branch);
    pure->Graph.AddLink(Link(
        fixture.ids.GetNextId(), begin->Outputs[0].ID, print->Inputs[0].ID));
    pure->Graph.AddLink(Link(
        fixture.ids.GetNextId(), print->Outputs[0].ID,
        returnNode->Inputs[0].ID));
    script.functions.push_back(pure);

    const ValidationReport invalid = ScriptValidator::Validate(script);
    const bool foundImpure = std::any_of(
        invalid.diagnostics.begin(), invalid.diagnostics.end(),
        [](const ValidationDiagnostic& diagnostic)
        {
            return diagnostic.code == "impure-node" &&
                   diagnostic.severity == DiagnosticSeverity::Error;
        });
    Require(foundImpure,
            "A non-pure node in a pure graph should be a compilation error.");
    const ScriptCompileResult rejected =
        ScriptRuntime::Compile(fixture.vm, script);
    Require(!rejected,
            "Compilation should stop when a pure graph contains an impure node.");

    pure->Graph.DeleteNode(print->ID);
    NodePtr equals =
        fixture.registry.FindCompiled("Math::Equals")->MakeNode(fixture.ids);
    AttachNode(pure->Graph, equals);
    pure->Graph.AddLink(Link(
        fixture.ids.GetNextId(), begin->Outputs[0].ID,
        branch->Inputs[0].ID));
    pure->Graph.AddLink(Link(
        fixture.ids.GetNextId(), branch->Outputs[0].ID,
        returnNode->Inputs[0].ID));
    pure->Graph.AddLink(Link(
        fixture.ids.GetNextId(), equals->Outputs[0].ID,
        branch->Inputs[1].ID));
    pure->Graph.AddLink(Link(
        fixture.ids.GetNextId(), equals->Outputs[0].ID,
        returnNode->Inputs[1].ID));
    const ValidationReport valid = ScriptValidator::Validate(script);
    const bool stillInvalid = std::any_of(
        valid.diagnostics.begin(), valid.diagnostics.end(),
        [](const ValidationDiagnostic& diagnostic)
        {
            return diagnostic.code == "impure-node";
        });
    Require(!stillInvalid,
            "Pure expression and control-flow nodes should be accepted in a pure graph.");
    Require(!begin->Outputs.empty() &&
            begin->Outputs[0].Type == PinType::Flow &&
            !returnNode->Inputs.empty() &&
            returnNode->Inputs[0].Type == PinType::Flow,
            "A pure function body should retain its execution-flow pins.");
    const NodePtr call = pure->functionDef->MakeNode(fixture.ids, pure->ID);
    Require(GraphUtils::IsNodeImplicit(call),
            "Calling a pure script function should use data pins only.");
}

void DeclaredTypesDoNotFollowDefaults()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");
    AttachNode(script.main->Graph, BuildBeginNode(fixture.ids, script.main));
    DocumentOperations operations(script, fixture.ids, fixture.registry);

    const int variableId = fixture.ids.GetNextId();
    Require(operations.AddVariable(variableId, "Username", StringValue("Ada")).success,
            "Expected to add a typed variable.");
    Require(operations.ChangeVariableValue(variableId, StringValue("Grace")).success,
            "Expected to change the variable default.");
    ScriptPropertyPtr variable = ScriptUtils::FindVariableById(script, variableId);
    Require(variable && variable->type == PinType::String,
            "Changing a default should not change the declared type.");

    const TypeRef students = TypeRef::List(TypeRef::Object(42, "Student"));
    Require(operations.ChangeVariableType(variableId, students).success,
            "Expected to change a variable declaration.");
    Require(variable->type == students && isList(variable->defaultValue),
            "Changing a declaration should create a suitable fresh default.");
}

void FunctionLocalsAreFreshPerInvocation()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");
    NodePtr mainBegin = BuildBeginNode(fixture.ids, script.main);
    AttachNode(script.main->Graph, mainBegin);

    ScriptPropertyPtr first = std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "First");
    first->type = PinType::Float;
    first->defaultValue = Value(0.0);
    script.variables.push_back(first);
    ScriptPropertyPtr second = std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Second");
    second->type = PinType::Float;
    second->defaultValue = Value(0.0);
    script.variables.push_back(second);
    ScriptPropertyPtr mainObserved = std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "MainObserved");
    mainObserved->type = PinType::Float;
    mainObserved->defaultValue = Value(0.0);
    script.variables.push_back(mainObserved);
    ScriptPropertyPtr mainLocal = std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "MainLocal");
    mainLocal->type = PinType::Float;
    mainLocal->defaultValue = Value(4.0);
    script.main->variables.push_back(mainLocal);

    ScriptFunctionPtr increment = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "IncrementLocal");
    increment->functionDef->outputs.push_back({ "Result", Value(0.0), fixture.ids.GetNextId() });
    ScriptPropertyPtr counter = std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Counter");
    counter->type = PinType::Float;
    counter->defaultValue = Value(0.0);
    increment->variables.push_back(counter);
    script.functions.push_back(increment);

    NodePtr begin = BuildBeginNode(fixture.ids, increment);
    NodePtr getForAdd = BuildGetVariableNode(fixture.ids, counter, ScriptElementID::Invalid, increment->ID);
    NodePtr add = fixture.registry.FindCompiled("Math::Add")->MakeNode(fixture.ids);
    add->Inputs[1].LiteralValue = Value(1.0);
    NodePtr setCounter = BuildSetVariableNode(fixture.ids, counter, ScriptElementID::Invalid, increment->ID);
    NodePtr getForReturn = BuildGetVariableNode(fixture.ids, counter, ScriptElementID::Invalid, increment->ID);
    NodePtr returnNode = BuildReturnNode(fixture.ids, *increment);
    for (const NodePtr& node : { begin, getForAdd, add, setCounter, getForReturn, returnNode })
        AttachNode(increment->Graph, node);
    increment->Graph.AddLink(Link(fixture.ids.GetNextId(), begin->Outputs[0].ID, setCounter->Inputs[0].ID));
    increment->Graph.AddLink(Link(fixture.ids.GetNextId(), getForAdd->Outputs[0].ID, add->Inputs[0].ID));
    increment->Graph.AddLink(Link(fixture.ids.GetNextId(), add->Outputs[0].ID, setCounter->Inputs[1].ID));
    increment->Graph.AddLink(Link(fixture.ids.GetNextId(), setCounter->Outputs[0].ID, returnNode->Inputs[0].ID));
    increment->Graph.AddLink(Link(fixture.ids.GetNextId(), getForReturn->Outputs[0].ID, returnNode->Inputs[1].ID));

    NodePtr callFirst = increment->functionDef->MakeNode(fixture.ids, increment->ID);
    NodePtr getMainLocal = BuildGetVariableNode(fixture.ids, mainLocal, ScriptElementID::Invalid, script.main->ID);
    NodePtr storeMainLocal = BuildSetVariableNode(fixture.ids, mainObserved);
    NodePtr storeFirst = BuildSetVariableNode(fixture.ids, first);
    NodePtr callSecond = increment->functionDef->MakeNode(fixture.ids, increment->ID);
    NodePtr storeSecond = BuildSetVariableNode(fixture.ids, second);
    for (const NodePtr& node : { getMainLocal, storeMainLocal, callFirst, storeFirst, callSecond, storeSecond })
        AttachNode(script.main->Graph, node);
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), mainBegin->Outputs[0].ID, storeMainLocal->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), getMainLocal->Outputs[0].ID, storeMainLocal->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), storeMainLocal->Outputs[0].ID, callFirst->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), callFirst->Outputs[0].ID, storeFirst->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), callFirst->Outputs[1].ID, storeFirst->Inputs[1].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), storeFirst->Outputs[0].ID, callSecond->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), callSecond->Outputs[0].ID, storeSecond->Inputs[0].ID));
    script.main->Graph.AddLink(Link(fixture.ids.GetNextId(), callSecond->Outputs[1].ID, storeSecond->Inputs[1].ID));

    Require(ScriptRuntime::Run(fixture.vm, script) == InterpretResult::INTERPRET_OK,
            "A function using a local variable did not execute.");
    Require(isNumber(ReadGlobal(fixture.vm, "First")) && asNumber(ReadGlobal(fixture.vm, "First")) == 1.0,
            "The first invocation did not read and write its local variable.");
    Require(isNumber(ReadGlobal(fixture.vm, "Second")) && asNumber(ReadGlobal(fixture.vm, "Second")) == 1.0,
            "A local variable was not initialized freshly for the second invocation.");
    Require(isNumber(ReadGlobal(fixture.vm, "MainObserved")) && asNumber(ReadGlobal(fixture.vm, "MainObserved")) == 4.0,
            "Main did not initialize and expose its local variable.");
}

void FunctionLocalValidationEnforcesScopeAndAllowsPureAssignment()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");
    AttachNode(script.main->Graph, BuildBeginNode(fixture.ids, script.main));

    ScriptFunctionPtr pure = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "PureWithLocal");
    pure->functionDef->flags |= NodeDefinitionFlags::Pure;
    ScriptPropertyPtr local = std::make_shared<ScriptProperty>(fixture.ids.GetNextId(), "Scratch");
    local->type = PinType::Float;
    local->defaultValue = Value(0.0);
    pure->variables.push_back(local);
    NodePtr begin = BuildBeginNode(fixture.ids, pure);
    NodePtr setLocal = BuildSetVariableNode(fixture.ids, local, ScriptElementID::Invalid, pure->ID);
    AttachNode(pure->Graph, begin);
    AttachNode(pure->Graph, setLocal);
    pure->Graph.AddLink(Link(fixture.ids.GetNextId(), begin->Outputs[0].ID, setLocal->Inputs[0].ID));
    script.functions.push_back(pure);

    const ValidationReport valid = ScriptValidator::Validate(script);
    Require(!HasCode(valid, "impure-node"),
            "Assigning a function local incorrectly made a pure graph impure.");

    NodePtr invalidReference = BuildGetVariableNode(fixture.ids, local, ScriptElementID::Invalid, script.main->ID);
    AttachNode(script.main->Graph, invalidReference);
    const ValidationReport wrongScope = ScriptValidator::Validate(script);
    Require(HasCode(wrongScope, "variable-scope"),
            "Validation accepted a function local in a different graph.");

    pure->functionDef->inputs.push_back({ "Scratch", Value(0.0), fixture.ids.GetNextId() });
    const ValidationReport conflictingName = ScriptValidator::Validate(script);
    Require(HasCode(conflictingName, "local-input-conflict"),
            "Validation accepted an input and local variable with the same name.");
}
}

void AddRuntimeTests(Tests::Runner& runner)
{
    runner.Group("Runtime / standard library", [&]()
    {
        runner.Test("JSON, text, math, and collection nodes operate", JsonTextMathAndCollectionNodesOperate);
        runner.Test("extended file, process, and time nodes operate", ExtendedFileProcessAndTimeNodesOperate);
        runner.Test("file, path, and console nodes operate",
            FilePathAndConsoleNodesOperate);
        runner.Test("node definitions declare their capabilities", StandardLibraryDeclaresCapabilities);
        runner.Test("simple nodes hide redundant pin names",
            SimpleNodesHideRedundantPinNames);
        runner.Test("list native nodes operate on lists", ListNativeNodesOperateOnLists);
        runner.Test("range native nodes support both directions", RangeNativeNodesSupportBothDirections);
        runner.Test("expanded math and string nodes operate",
            ExpandedMathAndStringNodesOperate);
        runner.Test("expanded list and range nodes operate",
            ExpandedListAndRangeNodesOperate);
        runner.Test("map nodes operate and preserve insertion order",
            MapNodesOperateAndPreserveInsertionOrder);
    });
    runner.Group("Runtime / VM boundaries", [&]()
    {
        runner.Test("repeated interpretation releases the stack", RepeatedInterpretationReleasesStack);
        runner.Test("GC tracks concrete object sizes", GarbageCollectionTracksConcreteObjectSizes);
        runner.Test("compiler temporaries survive GC", CompilerTemporariesSurviveGarbageCollection);
        runner.Test("large list literals preserve their items", LargeListLiteralsPreserveItems);
        runner.Test("Flow For In keeps a constant stack footprint", ForInKeepsConstantStackFootprint);
        runner.Test("Flow For In iterates ranges and strings",
            ForInIteratesRangesAndStrings);
        runner.Test("Map For Each iterates keys and values",
            MapForEachIteratesKeysAndValues);
        runner.Test("Main receives program arguments as a string list",
            MainReceivesProgramArgumentsAsAStringList);
        runner.Test("functions and methods support multiple outputs",
            FunctionsAndMethodsSupportMultipleOutputs);
        runner.Test("function locals are fresh per invocation",
            FunctionLocalsAreFreshPerInvocation);
    });
    runner.Group("Runtime / validation and compilation", [&]()
    {
        runner.Test("instance errors do not change definition capabilities",
            InstanceErrorsDoNotChangeDefinitionCapabilities);
        runner.Test("a missing Begin node is rejected", MissingBeginIsRejected);
        runner.Test("dependency cycles are rejected", DependencyCyclesAreRejected);
        runner.Test("implicit self receivers respect graph context",
            ImplicitSelfReceiversRespectGraphContext);
        runner.Test("method Get functions work with Filter",
            MethodGetFunctionsWorkWithFilter);
        runner.Test("pure nodes are constant folded", PureNodesAreConstantFolded);
        runner.Test("complete expression nodes compile and execute",
            CompleteExpressionNodesCompileAndExecute);
        runner.Test("While and Repeat nodes compile and execute",
            WhileAndRepeatNodesCompileAndExecute);
        runner.Test("classes, ranges, and matching round-trip and execute",
            ClassesRangesAndMatchingRoundTripAndExecute);
        runner.Test("runtime list mutation does not change document defaults",
            RuntimeListMutationDoesNotChangeDocumentDefaults);
        runner.Test("Visual classes use toString when printed",
            VisualClassesUseToStringWhenPrinted);
        runner.Test("Flow Switch evaluates conditions in order",
            FlowSwitchEvaluatesConditionsInOrder);
    });
    runner.Group("Runtime / graph links", [&]()
    {
        runner.Test("new links replace occupied connections", NewLinksReplaceOccupiedConnections);
    });
    runner.Group("Type system", [&]()
    {
        runner.Test("descriptors are directional and composable",
            TypeDescriptorsAreDirectionalAndComposable);
        runner.Test("generic nodes infer across connections",
            GenericNodesInferAcrossConnections);
        runner.Test("declared types do not follow defaults",
            DeclaredTypesDoNotFollowDefaults);
        runner.Test("pure graphs reject impure nodes",
            PureGraphsRejectImpureNodes);
        runner.Test("function-local validation enforces scope and purity",
            FunctionLocalValidationEnforcesScopeAndAllowsPureAssignment);
    });
}
