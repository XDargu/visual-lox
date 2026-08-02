#include "scriptSearchTests.h"

#include "testFramework.h"
#include "../script/scriptSearch.h"

#include <Object.h>

#include <algorithm>
#include <memory>

namespace
{
using Tests::Require;

struct SearchNode : Node
{
    SearchNode(int id, const char* name)
        : Node(id, name)
    {}

    void Compile(
        CompilerContext&, const Graph&, CompilationStage, int) const override
    {}
};

struct SearchFixture
{
    SearchFixture()
    {
        script.ID = 1;
        script.main = std::make_shared<ScriptFunction>(2, "Main");

        variable = std::make_shared<ScriptProperty>(3, "Player Score");
        variable->Description = "Current points";
        variable->type = PinType::Float;
        variable->defaultValue = Value(7.0);
        script.variables.push_back(variable);

        function = std::make_shared<ScriptFunction>(4, "Calculate Bonus");
        function->functionDef->description = "Computes a reward";
        function->functionDef->inputs.emplace_back(
            "Multiplier", Value(2.0), 5, PinType::Float,
            "Reward multiplier");
        localVariable = std::make_shared<ScriptProperty>(6, "Running Bonus");
        localVariable->Description = "Intermediate reward";
        localVariable->type = PinType::Float;
        localVariable->defaultValue = Value(1.0);
        function->variables.push_back(localVariable);
        script.functions.push_back(function);

        IDGenerator callIds;
        callIds.Reset(100);
        functionCall =
            function->functionDef->MakeNode(callIds, function->ID);
        NodeUtils::BuildNode(functionCall);
        script.main->Graph.AddNode(functionCall);

        node = std::make_shared<SearchNode>(20, "Format Result");
        node->Description = "Creates the final message";
        node->refId = variable->ID;
        node->Inputs.emplace_back(
            21, "Count", PinType::Float, "Number of points");
        node->Inputs.back().LiteralValue = Value(42.0);
        node->Inputs.emplace_back(
            23, "Predicate",
            TypeRef::Function({ PinType::Float }, { PinType::Bool }),
            "Function used to select values");
        node->Inputs.back().LiteralValue = Value(newFunction());
        node->Outputs.emplace_back(
            22, "Formatted Text", PinType::String, "Final output");
        NodeUtils::BuildNode(node);
        function->Graph.AddNode(node);

        scriptClass = std::make_shared<ScriptClass>(30, "Player");
        scriptClass->constructor =
            std::make_shared<ScriptFunction>(31, "Player");
        script.classes.push_back(scriptClass);

        constructNode = std::make_shared<SearchNode>(40, "Player");
        constructNode->refId = scriptClass->ID;
        NodeUtils::BuildNode(constructNode);
        script.main->Graph.AddNode(constructNode);
    }

    Script script;
    ScriptPropertyPtr variable;
    ScriptPropertyPtr localVariable;
    ScriptFunctionPtr function;
    ScriptClassPtr scriptClass;
    NodePtr node;
    NodePtr functionCall;
    NodePtr constructNode;
};

bool HasResult(
    const std::vector<ScriptSearchResult>& results,
    ScriptSearchResultKind kind, int elementId, int nodeId = -1)
{
    return std::any_of(
        results.begin(), results.end(),
        [&](const ScriptSearchResult& result)
        {
            return result.kind == kind &&
                   result.elementId == elementId &&
                   (nodeId < 0 || result.nodeId == nodeId);
        });
}

void TextSearchCoversDefinitionsPortsPinsAndValues()
{
    SearchFixture fixture;

    const auto definitionResults =
        ScriptSearch::Text(fixture.script, "PLAYER SCORE");
    Require(HasResult(
        definitionResults, ScriptSearchResultKind::Definition,
        fixture.variable->ID.id),
        "Text search did not match a definition case-insensitively.");

    const auto portResults =
        ScriptSearch::Text(fixture.script, "reward multiplier");
    Require(HasResult(
        portResults, ScriptSearchResultKind::FunctionPort, 5),
        "Text search did not match a function port description.");

    const auto callPinResults =
        ScriptSearch::Text(fixture.script, "Multiplier");
    Require(HasResult(
        callPinResults, ScriptSearchResultKind::GraphNode,
        fixture.function->ID.id,
        static_cast<int>(fixture.functionCall->ID.Get())),
        "Text search did not match an input pin on a function call.");

    const auto pinResults =
        ScriptSearch::Text(fixture.script, "formatted text");
    Require(HasResult(
        pinResults, ScriptSearchResultKind::GraphNode,
        fixture.variable->ID.id, 20),
        "Text search did not match an output pin name.");

    const auto valueResults =
        ScriptSearch::Text(fixture.script, "42.000000");
    Require(HasResult(
        valueResults, ScriptSearchResultKind::GraphNode,
        fixture.variable->ID.id, 20),
        "Text search did not match an input value.");

    const auto functionValueResults =
        ScriptSearch::Text(fixture.script, "<function>");
    Require(HasResult(
        functionValueResults, ScriptSearchResultKind::GraphNode,
        fixture.variable->ID.id, 20),
        "Text search did not safely inspect a function placeholder value.");
    Require(valueAsStr(fixture.node->Inputs[1].LiteralValue) == "<function>",
        "Nameless function placeholders are not safely printable.");
}

void ReferenceSearchIncludesDefinitionAndEveryUsage()
{
    SearchFixture fixture;
    const auto results = ScriptSearch::References(
        fixture.script, fixture.variable->ID.id, fixture.variable->ID.id);

    Require(HasResult(
        results, ScriptSearchResultKind::Definition,
        fixture.variable->ID.id),
        "Reference search omitted the variable definition.");
    Require(HasResult(
        results, ScriptSearchResultKind::GraphNode,
        fixture.variable->ID.id, 20),
        "Reference search omitted a graph usage.");
}

void SearchIncludesFunctionLocalDefinitions()
{
    SearchFixture fixture;
    const auto textResults = ScriptSearch::Text(fixture.script, "intermediate reward");
    const auto textMatch = std::find_if(textResults.begin(), textResults.end(),
        [&](const ScriptSearchResult& result)
        {
            return result.kind == ScriptSearchResultKind::Definition &&
                   result.elementId == fixture.localVariable->ID.id &&
                   result.functionId == fixture.function->ID.id;
        });
    Require(textMatch != textResults.end(),
            "Text search omitted a function-local variable or its owning graph.");

    const auto referenceResults = ScriptSearch::References(
        fixture.script, fixture.localVariable->ID.id, fixture.localVariable->ID.id);
    Require(HasResult(referenceResults, ScriptSearchResultKind::Definition,
                      fixture.localVariable->ID.id),
            "Reference search omitted a function-local definition.");
}

void ConstructorReferencesUseTheClassIdentity()
{
    SearchFixture fixture;
    const auto results = ScriptSearch::References(
        fixture.script, fixture.scriptClass->ID.id,
        fixture.scriptClass->constructor->ID.id);

    Require(HasResult(
        results, ScriptSearchResultKind::Definition,
        fixture.scriptClass->constructor->ID.id),
        "Constructor reference search omitted the constructor definition.");
    Require(HasResult(
        results, ScriptSearchResultKind::GraphNode,
        fixture.scriptClass->ID.id, 40),
        "Constructor reference search omitted a class construction node.");
}

void NativeAndCompiledReferencesUseDefinitionIdentity()
{
    SearchFixture fixture;

    NodePtr nativeReference = std::make_shared<SearchNode>(50, "Clock");
    nativeReference->SerializationType = "function.call";
    nativeReference->DefinitionId = "Clock";
    NodeUtils::BuildNode(nativeReference);
    fixture.script.main->Graph.AddNode(nativeReference);

    NodePtr secondNativeReference = std::make_shared<SearchNode>(51, "Clock");
    secondNativeReference->SerializationType = "function.call";
    secondNativeReference->DefinitionId = "Clock";
    NodeUtils::BuildNode(secondNativeReference);
    fixture.function->Graph.AddNode(secondNativeReference);

    NodePtr otherNative = std::make_shared<SearchNode>(52, "Random");
    otherNative->SerializationType = "function.call";
    otherNative->DefinitionId = "Random";
    NodeUtils::BuildNode(otherNative);
    fixture.script.main->Graph.AddNode(otherNative);

    NodePtr compiledReference = std::make_shared<SearchNode>(53, "+");
    compiledReference->SerializationType = "compiled";
    compiledReference->DefinitionId = "Math::Add";
    NodeUtils::BuildNode(compiledReference);
    fixture.function->Graph.AddNode(compiledReference);

    NodePtr secondCompiledReference = std::make_shared<SearchNode>(54, "+");
    secondCompiledReference->SerializationType = "compiled";
    secondCompiledReference->DefinitionId = "Math::Add";
    NodeUtils::BuildNode(secondCompiledReference);
    fixture.script.main->Graph.AddNode(secondCompiledReference);

    const auto nativeResults = ScriptSearch::References(fixture.script, *nativeReference);
    Require(HasResult(nativeResults, ScriptSearchResultKind::GraphNode, ScriptElementID::Invalid, 50),
        "Reference search omitted a native node instance.");
    Require(HasResult(nativeResults, ScriptSearchResultKind::GraphNode, ScriptElementID::Invalid, 51),
        "Reference search omitted a native node instance in another graph.");
    Require(!HasResult(nativeResults, ScriptSearchResultKind::GraphNode, ScriptElementID::Invalid, 52),
        "Reference search included a different native definition.");

    const auto compiledResults = ScriptSearch::References(fixture.script, *compiledReference);
    Require(HasResult(compiledResults, ScriptSearchResultKind::GraphNode, ScriptElementID::Invalid, 53),
        "Reference search omitted a compiled node instance.");
    Require(HasResult(compiledResults, ScriptSearchResultKind::GraphNode, ScriptElementID::Invalid, 54),
        "Reference search omitted a compiled node instance in another graph.");
}
}

void AddScriptSearchTests(Tests::Runner& runner)
{
    runner.Group("Script search", [&]()
    {
        runner.Test(
            "Text search covers definitions, ports, pins, and values",
            TextSearchCoversDefinitionsPortsPinsAndValues);
        runner.Test(
            "Reference search includes definition and usages",
            ReferenceSearchIncludesDefinitionAndEveryUsage);
        runner.Test(
            "Search includes function-local definitions",
            SearchIncludesFunctionLocalDefinitions);
        runner.Test(
            "Constructor references use class identity",
            ConstructorReferencesUseTheClassIdentity);
        runner.Test(
            "Native and compiled references use definition identity",
            NativeAndCompiledReferencesUseDefinitionIdentity);
    });
}
