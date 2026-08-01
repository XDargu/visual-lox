#include "serializationIdentityTests.h"

#include "../graphs/uuid.h"
#include "../graphs/nodeRegistry.h"
#include "../runtime/standardLibrary.h"
#include "../script/scriptSerializer.h"
#include "testFramework.h"

#include <type_traits>
#include <set>

namespace
{
using Tests::Require;

void UuidV5MatchesTheStandardVector()
{
    const Uuid dnsNamespace = Uuid::Parse("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
    const Uuid generated = Uuid::V5(dnsNamespace, "www.widgets.com");
    Require(generated.ToString() == "21f7f8de-8051-5b89-8680-0195ef798b6a", "UUIDv5 does not match the RFC 4122 test vector.");
}

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

    for (const NativeFunctionDef& native : registry.nativeDefinitions)
    {
        Require(native.functionDef && native.functionDef->id.rfind("vlox.std.native.", 0) == 0 && native.functionDef->revision > 0,
            "A native definition has no stable schema.");
        Require(definitionIds.insert(native.functionDef->id).second, "Native definition IDs are not unique.");
        validatePorts(*native.functionDef, native.functionDef->inputs);
        validatePorts(*native.functionDef, native.functionDef->outputs);
    }

    definitionIds.clear();
    for (const CompiledNodeDefPtr& compiled : registry.compiledDefinitions)
    {
        Require(compiled && compiled->functionDef && compiled->id.rfind("vlox.std.compiled.", 0) == 0 && compiled->revision > 0,
            "A compiled definition has no stable schema.");
        Require(definitionIds.insert(compiled->id).second, "Compiled definition IDs are not unique.");
        validatePorts(*compiled->functionDef, compiled->functionDef->inputs);
        validatePorts(*compiled->functionDef, compiled->functionDef->outputs);
    }
}

void DefinitionAliasesResolveToStableIds()
{
    NodeRegistry registry;
    RegisterStandardLibrary(registry);
    registry.RegisterNativeAlias("legacy.math.square", "vlox.std.native.math.square");
    registry.RegisterCompiledAlias("legacy.math.add", "vlox.std.compiled.math.add");
    Require(registry.FindNative("legacy.math.square") == registry.FindNative("vlox.std.native.math.square"), "Native definition alias did not resolve.");
    Require(registry.FindCompiled("legacy.math.add") == registry.FindCompiled("vlox.std.compiled.math.add"), "Compiled definition alias did not resolve.");
}
}

void AddSerializationIdentityTests(Tests::Runner& runner)
{
    runner.Group("Serialization / identity", [&]()
    {
        runner.Test("UUIDv5 matches the standard vector", UuidV5MatchesTheStandardVector);
        runner.Test("UUIDv4 has the required version and variant", UuidV4HasTheRequiredVersionAndVariant);
        runner.Test("durable identity types remain distinct", DurableIdentityTypesRemainDistinct);
        runner.Test("failures include structured diagnostics", SerializationFailuresHaveStructuredDiagnostics);
        runner.Test("registered definitions have stable schemas", RegisteredDefinitionsHaveStableSchemas);
        runner.Test("definition aliases resolve to stable IDs", DefinitionAliasesResolveToStableIds);
    });
}
