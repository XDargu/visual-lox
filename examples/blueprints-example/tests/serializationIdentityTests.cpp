#include "serializationIdentityTests.h"

#include "../graphs/uuid.h"
#include "../graphs/idgeneration.h"
#include "../graphs/nodeRegistry.h"
#include "../native/nodes/object.h"
#include "../runtime/standardLibrary.h"
#include "../script/scriptSerializer.h"
#include "testFramework.h"

#include <type_traits>
#include <set>

namespace
{
using Tests::Require;

void UuidV4HasTheRequiredVersionAndVariant()
{
    const Uuid generated = Uuid::NewV4();
    const std::string text = generated.ToString();
    Require(!generated.IsNil(), "UUIDv4 generation returned the nil UUID.");
    Require(text.size() == 36 && text[14] == '4', "UUIDv4 generation set the wrong version.");
    Require(text[19] == '8' || text[19] == '9' || text[19] == 'a' || text[19] == 'b', "UUIDv4 generation set the wrong variant.");
    Require(Uuid::Parse(text) == generated, "UUID formatting and parsing did not round-trip.");
}

void DurableIdentityTypesRemainDistinct()
{
    const Uuid value = Uuid::Parse("9fcf33a0-3566-4834-b16f-e2849703c258");
    const ModuleId module(value);
    const ScriptPortId port(value);
    Require(module.ToString() == port.ToString(), "Typed durable IDs did not retain their UUID value.");
    static_assert(!std::is_same_v<ModuleId, ScriptPortId>);
}

void SerializationFailuresHaveStructuredDiagnostics()
{
    const SerializationResult result = SerializationResult::Fail("Broken document.", "document.corrupt", "$.script");
    Require(!result && result.diagnostics.size() == 1, "Serialization failure did not include one structured diagnostic.");
    Require(result.diagnostics[0].severity == SerializationDiagnosticSeverity::Error && result.diagnostics[0].code == "document.corrupt" &&
        result.diagnostics[0].path == "$.script" && result.diagnostics[0].message == result.error,
        "Serialization failure diagnostic fields were not preserved.");
}

void RegisteredDefinitionsHaveStableSchemas()
{
    NodeRegistry registry;
    RegisterStandardLibrary(registry);
    std::set<std::string> definitionIds;
    const auto validatePorts = [](const BasicFunctionDef& definition, const std::vector<BasicFunctionDef::Input>& ports)
    {
        std::set<std::string> keys;
        for (const BasicFunctionDef::Input& port : ports)
            Require(!port.key.empty() && keys.insert(port.key).second, "A registered definition has an empty or duplicate stable port key.");
    };
    const auto validateNodePorts = [](const NodePtr& node)
    {
        Require(node != nullptr, "A registered definition did not create a node.");
        for (const Pin& input : node->Inputs)
            Require(input.Identity.kind != PortIdentityKind::None, "A registered definition created an input without a semantic identity.");
        for (const Pin& output : node->Outputs)
            Require(output.Identity.kind != PortIdentityKind::None, "A registered definition created an output without a semantic identity.");
    };
    IDGenerator ids;

    for (const NativeFunctionDef& native : registry.nativeDefinitions)
    {
        Require(native.functionDef && native.functionDef->id.rfind("vlox.std.native.", 0) == 0 && native.functionDef->revision > 0 && !native.functionDef->compatibilityFingerprint.empty(),
            "A native definition has no stable schema.");
        Require(definitionIds.insert(native.functionDef->id).second, "Native definition IDs are not unique.");
        validatePorts(*native.functionDef, native.functionDef->inputs);
        validatePorts(*native.functionDef, native.functionDef->outputs);
        validateNodePorts(native.functionDef->MakeNode(ids, ScriptElementID::Invalid));
    }

    definitionIds.clear();
    for (const CompiledNodeDefPtr& compiled : registry.compiledDefinitions)
    {
        Require(compiled && compiled->functionDef && compiled->id.rfind("vlox.std.compiled.", 0) == 0 && compiled->revision > 0 && !compiled->functionDef->compatibilityFingerprint.empty(),
            "A compiled definition has no stable schema.");
        Require(definitionIds.insert(compiled->id).second, "Compiled definition IDs are not unique.");
        validatePorts(*compiled->functionDef, compiled->functionDef->inputs);
        validatePorts(*compiled->functionDef, compiled->functionDef->outputs);
        validateNodePorts(compiled->MakeNode(ids));
    }
}

void LiteralValuesAreOwnedByInputs()
{
    NodeRegistry registry;
    RegisterStandardLibrary(registry);
    IDGenerator ids;
    NodePtr node = registry.FindCompiled("vlox.std.compiled.math.add")->MakeNode(ids);
    Require(node && !node->Inputs.empty(), "The registered node has no inputs.");
    node->Inputs[0].LiteralValue = Value(42.0);
    const size_t initialCount = node->Inputs.size();
    node->AddInput(ids);
    Require(node->Inputs.size() == initialCount + 1 && isNumber(node->Inputs[0].LiteralValue) && asNumber(node->Inputs[0].LiteralValue) == 42.0,
        "Adding a dynamic input changed another input's owned literal value.");
    node->RemoveInput(node->Inputs.back().ID);
    Require(node->Inputs.size() == initialCount && isNumber(node->Inputs[0].LiteralValue) && asNumber(node->Inputs[0].LiteralValue) == 42.0,
        "Removing a dynamic input changed another input's owned literal value.");
}

void PureMethodCallsIncludeDeclaredInputs()
{
    IDGenerator ids;
    ScriptFunctionPtr method = std::make_shared<ScriptFunction>(ids.GetNextId(), "CanRun");
    method->functionDef->flags |= NodeDefinitionFlags::Pure;
    method->functionDef->inputs.emplace_back("Iteration", Value(0.0), ids.GetNextId(), PinType::Float);

    const NodePtr call = BuildMethodCallNode(ids, method);
    Require(call && call->Inputs.size() == 2, "A pure method call omitted its declared input.");
    Require(call->Inputs[0].Identity == PortIdentity::Fixed("instance"), "A pure method call omitted its instance input.");
    Require(call->Inputs[1].Identity == PortIdentity::Script(method->functionDef->inputs[0].persistentId),
        "A pure method call assigned the wrong semantic identity to its declared input.");
}

void DefinitionFingerprintsIgnorePresentation()
{
    NodeRegistry registry;
    RegisterStandardLibrary(registry);
    BasicFunctionDef original = *registry.FindNative("vlox.std.native.math.square")->functionDef;
    BasicFunctionDef presentationChange = original;
    presentationChange.name = "Renamed Square";
    presentationChange.description = "Changed documentation";
    presentationChange.inputs[0].name = "Renamed Value";
    presentationChange.inputs[0].description = "Changed input documentation";
    Require(ComputeDefinitionCompatibilityFingerprint(original) == ComputeDefinitionCompatibilityFingerprint(presentationChange),
        "Presentation-only changes altered the compatibility fingerprint.");

    BasicFunctionDef schemaChange = original;
    schemaChange.inputs[0].key = "different_key";
    Require(ComputeDefinitionCompatibilityFingerprint(original) != ComputeDefinitionCompatibilityFingerprint(schemaChange),
        "A stable port-key change did not alter the compatibility fingerprint.");
}
}

void AddSerializationIdentityTests(Tests::Runner& runner)
{
    runner.Group("Serialization / identity", [&]()
    {
        runner.Test("UUIDv4 has the required version and variant", UuidV4HasTheRequiredVersionAndVariant);
        runner.Test("durable identity types remain distinct", DurableIdentityTypesRemainDistinct);
        runner.Test("failures include structured diagnostics", SerializationFailuresHaveStructuredDiagnostics);
        runner.Test("registered definitions have stable schemas", RegisteredDefinitionsHaveStableSchemas);
        runner.Test("literal values are owned by inputs", LiteralValuesAreOwnedByInputs);
        runner.Test("pure method calls include declared inputs", PureMethodCallsIncludeDeclaredInputs);
        runner.Test("definition fingerprints ignore presentation", DefinitionFingerprintsIgnorePresentation);
    });
}
