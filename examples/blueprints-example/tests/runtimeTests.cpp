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

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
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
        Require(node && node->DefinitionId == name,
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
}

void SimpleNodesHideRedundantPinNames()
{
    RuntimeFixture fixture;
    for (const char* name : {
            "Math::Negate", "Math::Not Equals", "Math::Greater Or Equal",
            "Math::Less Or Equal", "Logic::Not", "Logic::And", "Logic::Or",
            "Value::Is Nil" })
    {
        const CompiledNodeDefPtr definition = fixture.registry.FindCompiled(name);
        Require(definition != nullptr,
                "Expected the simple node definition to be registered.");
        const NodePtr node = definition->MakeNode(fixture.ids);
        Require(!node->ShowInputPinNames && !node->ShowOutputPinNames,
                "Self-evident simple nodes should hide their pin names.");
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
    constructorSet->InputValues[2] = Value(7.0);
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
    NodePtr throughReturn = BuildReturnNode(fixture.ids, *readThroughSelf);
    AttachNode(readThroughSelf->Graph, throughBegin);
    AttachNode(readThroughSelf->Graph, callRead);
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

    NodePtr missingInMain = BuildGetPropertyNode(fixture.ids, value);
    AttachNode(script.main->Graph, missingInMain);
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
    const auto hasMissingInstance = [&](const NodePtr& node)
    {
        const auto diagnostics =
            invalid.ForNode(node->ID == missingInMain->ID
                    ? script.main->ID : otherMethod->ID,
                node->ID);
        return std::any_of(diagnostics.begin(), diagnostics.end(),
            [](const ValidationDiagnostic* diagnostic)
            {
                return diagnostic->code == "missing-instance";
            });
    };
    Require(hasMissingInstance(missingInMain),
            "A member node in Main should require an explicit instance.");
    Require(hasMissingInstance(missingInOtherClass),
            "A member node in another class should require an explicit instance.");
    Require(!HasCode(valid, "missing-instance"),
            "Same-class receivers must not produce missing-instance errors.");
}

void PureNodesAreConstantFolded()
{
    RuntimeFixture fixture;
    Script script;
    script.ID = fixture.ids.GetNextId();
    script.main = std::make_shared<ScriptFunction>(fixture.ids.GetNextId(), "Main");
    NodePtr begin = BuildBeginNode(fixture.ids, script.main);
    NodePtr add = fixture.registry.FindCompiled("Math::Add")->MakeNode(fixture.ids);
    add->InputValues[0] = Value(2.0);
    add->InputValues[1] = Value(3.0);
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
            asNumber(compiled.foldedValues[index]) == 5.0,
            "Constant folding should preserve the Add result.");
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
    forIn->InputValues[1] = Value(list);
    listLength->InputValues[0] = Value(list);

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
    rangeLoop->InputValues[1] = Value(newRange(2.0, 4.0));
    stringLoop->InputValues[1] = StringValue("Lox");

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
    multipleReturn->InputValues[1] = Value(42.0);
    multipleReturn->InputValues[2] = Value(takeString("packed", 6));
    multipleReturn->InputValues[3] = Value(true);
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
    methodReturn->InputValues[1] = Value(7.0);
    methodReturn->InputValues[2] = Value(takeString("method", 6));
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
    construct->InputValues[1] = Value(7.0);
    NodePtr callGetValue = BuildMethodCallNode(ids, getValue);
    NodePtr storeClassResult = BuildSetVariableNode(ids, classResult);
    NodePtr storeMatchResult = BuildSetVariableNode(ids, matchResult);
    storeMatchResult->InputValues[1] = Value(true);
    NodePtr flowMatch = registry.FindCompiled("Flow::Match")->MakeNode(ids);
    flowMatch->AddInput(ids);
    flowMatch->InputValues[1] = Value(3.0);
    flowMatch->InputValues[2] = Value(1.0);
    NodePtr flowRange = registry.FindCompiled("Range::Make")->MakeNode(ids);
    flowRange->InputValues[0] = Value(2.0);
    flowRange->InputValues[1] = Value(4.0);
    NodePtr firstCase = BuildSetVariableNode(ids, flowMatchResult);
    firstCase->InputValues[1] = Value(10.0);
    NodePtr secondCase = BuildSetVariableNode(ids, flowMatchResult);
    secondCase->InputValues[1] = Value(20.0);
    NodePtr defaultCase = BuildSetVariableNode(ids, flowMatchResult);
    defaultCase->InputValues[1] = Value(99.0);
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
    Require(static_cast<bool>(ScriptSerializer::SerializeToString(source, document)),
            "Classes and object nodes should serialize.");
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
    Require(static_cast<bool>(
                ScriptSerializer::SerializeToString(script, before)),
            "The source document should serialize before execution.");

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
    methodReturn->InputValues[1] = StringValue("custom display");
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
    append->InputValues[0] = StringValue("prefix: ");
    NodePtr storeConcatenated =
        BuildSetVariableNode(fixture.ids, concatenated);
    NodePtr appendList =
        fixture.registry.FindCompiled("String::Append")->MakeNode(fixture.ids);
    appendList->InputValues[0] = StringValue("list: ");
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
    falseCondition->InputValues[0] = Value(true);
    NodePtr trueCondition =
        fixture.registry.FindCompiled("Logic::Not")->MakeNode(fixture.ids);
    trueCondition->InputValues[0] = Value(false);

    NodePtr firstCase = BuildSetVariableNode(fixture.ids, selected);
    firstCase->InputValues[1] = Value(10.0);
    NodePtr secondCase = BuildSetVariableNode(fixture.ids, selected);
    secondCase->InputValues[1] = Value(20.0);
    NodePtr thirdCase = BuildSetVariableNode(fixture.ids, selected);
    thirdCase->InputValues[1] = Value(30.0);
    NodePtr unexpectedDefault = BuildSetVariableNode(fixture.ids, selected);
    unexpectedDefault->InputValues[1] = Value(99.0);

    NodePtr defaultSwitch =
        fixture.registry.FindCompiled("Flow::Switch")->MakeNode(fixture.ids);
    defaultSwitch->AddInput(fixture.ids);
    NodePtr unexpectedFirst = BuildSetVariableNode(fixture.ids, defaultSelected);
    unexpectedFirst->InputValues[1] = Value(1.0);
    NodePtr unexpectedSecond = BuildSetVariableNode(fixture.ids, defaultSelected);
    unexpectedSecond->InputValues[1] = Value(2.0);
    NodePtr defaultCase = BuildSetVariableNode(fixture.ids, defaultSelected);
    defaultCase->InputValues[1] = Value(40.0);

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
            return node->DefinitionId == "Flow::Switch";
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
        expression->InputValues[0] = first;
        if (hasSecond)
            expression->InputValues[1] = second;

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
    addExpression("Logic::And", "ExprAnd", Value(true), Value(true), true,
                  Value(false));
    NodePtr shortAnd = addExpression(
        "Logic::And", "ExprAndShort", Value(false), Value(true), true, Value(true));
    addExpression("Logic::Or", "ExprOr", Value(false), Value(true), true,
                  Value(false));
    NodePtr shortOr = addExpression(
        "Logic::Or", "ExprOrShort", Value(true), Value(false), true, Value(false));
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
             "ExprOr", "ExprOrShort" })
        Require(isBoolean(ReadGlobal(fixture.vm, name)) &&
                asBoolean(ReadGlobal(fixture.vm, name)),
                "Expected expression result to be true.");
    Require(isBoolean(ReadGlobal(fixture.vm, "ExprAndShort")) &&
            !asBoolean(ReadGlobal(fixture.vm, "ExprAndShort")),
            "Logic::And should preserve a false left operand.");
    Require(isString(ReadGlobal(fixture.vm, "ExprCoalesce")) &&
            asString(ReadGlobal(fixture.vm, "ExprCoalesce"))->chars == "fallback",
            "Value::Coalesce should use its fallback for nil.");
    Require(isString(ReadGlobal(fixture.vm, "ExprCoalesceKeep")) &&
            asString(ReadGlobal(fixture.vm, "ExprCoalesceKeep"))->chars == "left",
            "Value::Coalesce should preserve a non-nil left operand.");
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
    condition->InputValues[1] = Value(3.0);
    NodePtr increment =
        fixture.registry.FindCompiled("Math::Add")->MakeNode(fixture.ids);
    NodePtr bodyCounter = BuildGetVariableNode(fixture.ids, counter);
    increment->InputValues[1] = Value(1.0);
    NodePtr setCounter = BuildSetVariableNode(fixture.ids, counter);
    NodePtr setWhileDone = BuildSetVariableNode(fixture.ids, whileDone);
    setWhileDone->InputValues[1] = Value(true);

    NodePtr repeat =
        fixture.registry.FindCompiled("Flow::Repeat")->MakeNode(fixture.ids);
    repeat->InputValues[1] = Value(3.0);
    NodePtr sum =
        fixture.registry.FindCompiled("Math::Add")->MakeNode(fixture.ids);
    NodePtr currentSum = BuildGetVariableNode(fixture.ids, repeatSum);
    NodePtr setSum = BuildSetVariableNode(fixture.ids, repeatSum);
    NodePtr setRepeatDone = BuildSetVariableNode(fixture.ids, repeatDone);
    setRepeatDone->InputValues[1] = Value(true);

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

    std::filesystem::remove_all(directory, error);
    Require(!error, "Expected to clean up the standard-library test directory.");
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
            isNumber(numericEquals->InputValues[1]) &&
            asNumber(numericEquals->InputValues[1]) == 0.0,
            "An inferred numeric Equals operand should receive a numeric zero default.");
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
}

void AddRuntimeTests(Tests::Runner& runner)
{
    runner.Group("Runtime / standard library", [&]()
    {
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
    });
    runner.Group("Runtime / VM boundaries", [&]()
    {
        runner.Test("repeated interpretation releases the stack", RepeatedInterpretationReleasesStack);
        runner.Test("large list literals preserve their items", LargeListLiteralsPreserveItems);
        runner.Test("Flow For In keeps a constant stack footprint", ForInKeepsConstantStackFootprint);
        runner.Test("Flow For In iterates ranges and strings",
            ForInIteratesRangesAndStrings);
        runner.Test("Main receives program arguments as a string list",
            MainReceivesProgramArgumentsAsAStringList);
        runner.Test("functions and methods support multiple outputs",
            FunctionsAndMethodsSupportMultipleOutputs);
    });
    runner.Group("Runtime / validation and compilation", [&]()
    {
        runner.Test("instance errors do not change definition capabilities",
            InstanceErrorsDoNotChangeDefinitionCapabilities);
        runner.Test("a missing Begin node is rejected", MissingBeginIsRejected);
        runner.Test("dependency cycles are rejected", DependencyCyclesAreRejected);
        runner.Test("implicit self receivers respect graph context",
            ImplicitSelfReceiversRespectGraphContext);
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
    });
}
