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
        node->InputValues.emplace_back(Value(42.0));
        node->Inputs.emplace_back(
            23, "Predicate",
            TypeRef::Function({ PinType::Float }, { PinType::Bool }),
            "Function used to select values");
        node->InputValues.emplace_back(Value(newFunction()));
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
    Require(valueAsStr(fixture.node->InputValues[1]) == "<function>",
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
            "Constructor references use class identity",
            ConstructorReferencesUseTheClassIdentity);
    });
}
