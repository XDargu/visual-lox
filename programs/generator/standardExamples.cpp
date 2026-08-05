#include "generator.h"

#include "../../examples/blueprints-example/native/nodes/begin.h"
#include "../../examples/blueprints-example/native/nodes/return.h"
#include "../../examples/blueprints-example/runtime/scriptRuntime.h"
#include "../../examples/blueprints-example/script/scriptSerializer.h"
#include "../../examples/blueprints-example/validation/scriptValidator.h"

#include <Object.h>
#include <Vm.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ExampleGenerator
{

NodePtr NumberOperation(Builder& builder, const char* name, double right)
{
    NodePtr node = builder.Compiled(name);
    node->Inputs[1].LiteralValue = Value(right);
    return node;
}

NodePtr MakeTypedList(Builder& builder, TypeRef elementType)
{
    NodePtr makeList = builder.Native("List::MakeList");
    makeList->TypeOverrides["T"] = elementType;
    return makeList;
}

struct FreshList
{
    NodePtr set;
    NodePtr clear;
};

FreshList InitializeFreshNumberList(Builder& builder, Graph& graph, const Pin& incomingFlow, const ScriptPropertyPtr& property)
{
    NodePtr makeList = MakeTypedList(builder, PinType::Float);
    NodePtr setList = builder.Set(property);
    NodePtr getList = builder.Get(property);
    NodePtr clear = builder.Native("List::Clear");
    builder.Add(graph, { makeList, setList, getList, clear });
    builder.Link(graph, incomingFlow, setList->Inputs[0]);
    builder.Link(graph, builder.Output(makeList, "List"), setList->Inputs[1]);
    builder.Link(graph, setList->Outputs[0], clear->Inputs[0]);
    builder.Link(graph, getList->Outputs[0], builder.Input(clear, "List"));
    return { setList, clear };
}

void LayoutGraph(Graph& graph)
{
    size_t executionCount = 0;
    for (const NodePtr& node : graph.GetNodes())
        executionCount += !GraphUtils::IsNodeImplicit(node) ? 1 : 0;

    const long dataStartY = static_cast<long>((executionCount + 7) / 8) * 240 + 320;
    size_t executionIndex = 0;
    size_t dataIndex = 0;
    for (const NodePtr& node : graph.GetNodes())
    {
        const bool execution = !GraphUtils::IsNodeImplicit(node);
        const size_t index = execution ? executionIndex++ : dataIndex++;
        const long x = static_cast<long>(index % 8) * (execution ? 280 : 240);
        const long y = (execution ? 0 : dataStartY) + static_cast<long>(index / 8) * (execution ? 240 : 176);
        node->State = "{\"location\":{\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + "}}";
    }
}

void LayoutScript(Script& script)
{
    LayoutGraph(script.main->Graph);
    for (const ScriptFunctionPtr& function : script.functions)
        LayoutGraph(function->Graph);
    for (const ScriptClassPtr& scriptClass : script.classes)
    {
        if (scriptClass->constructor)
            LayoutGraph(scriptClass->constructor->Graph);
        for (const ScriptFunctionPtr& method : scriptClass->methods)
            LayoutGraph(method->Graph);
    }
}

void ValidateCompileAndSave(VM& vm, const NodeRegistry& registry, Script& script, const std::filesystem::path& output)
{
    std::cerr << "Laying out " << output.filename().string() << std::endl;
    LayoutScript(script);
    std::cerr << "Validating " << output.filename().string() << std::endl;
    const ValidationReport validation = ScriptValidator::Validate(script);
    for (const ValidationDiagnostic& diagnostic : validation.diagnostics)
        std::clog << output.filename().string() << ": " << FormatDiagnostic(diagnostic) << '\n';
    if (validation.HasErrors())
        throw std::runtime_error("Generated graph failed validation: " + output.string());

    std::cerr << "Compiling " << output.filename().string() << std::endl;
    const ScriptCompileResult compiled = ScriptRuntime::Compile(vm, script);
    if (!compiled)
        throw std::runtime_error("Generated graph failed compilation: " + output.string());

    std::cerr << "Saving " << output.filename().string() << std::endl;
    const SerializationResult saved = ScriptSerializer::Save(script, output.string());
    if (!saved)
        throw std::runtime_error(saved.error);

    Script loaded;
    IDGenerator loadedIds;
    const SerializationResult roundTrip = ScriptSerializer::Load(output.string(), registry, loaded, loadedIds);
    if (!roundTrip)
        throw std::runtime_error("Could not reload generated script: " + roundTrip.error);
    if (!ScriptRuntime::Compile(vm, loaded))
        throw std::runtime_error("Reloaded graph failed compilation: " + output.string());
    std::cout << "Generated " << output.string() << '\n';
}

void SmokeTestGameOfLife(VM& vm, const NodeRegistry& registry, const std::filesystem::path& path)
{
    Script script;
    IDGenerator ids;
    const SerializationResult loaded = ScriptSerializer::Load(path.string(), registry, script, ids);
    if (!loaded)
        throw std::runtime_error("Could not load Game of Life smoke test: " + loaded.error);

    const ScriptCompileResult compiled = ScriptRuntime::Compile(vm, script);
    if (!compiled)
        throw std::runtime_error("Could not compile Game of Life smoke test.");

    bool textureCreated = false;
    VisualApplicationContext applicationContext({
        [&](const void*, int width, int height)
        {
            textureCreated = width == 40 && height == 40;
            return reinterpret_cast<ImTextureID>(1);
        },
        [](ImTextureID) {}
    });
    if (ScriptRuntime::Execute(vm, compiled.function) != InterpretResult::INTERPRET_OK || !applicationContext.HasUpdateFunction())
        throw std::runtime_error("Game of Life failed to initialize its visual application.");

    Value cells;
    if (!vm.globalTable().get(copyString("Cells", 5), &cells) || !isList(cells) || asList(cells)->items.size() != 40 * 40)
        throw std::runtime_error("Game of Life created an invalid initial grid.");
    if (ScriptRuntime::CallGlobal(vm, "Step Generation") != InterpretResult::INTERPRET_OK)
        throw std::runtime_error("Game of Life failed to calculate a generation.");
    if (!vm.globalTable().get(copyString("Cells", 5), &cells) || !isList(cells) || asList(cells)->items.size() != 40 * 40)
        throw std::runtime_error("Game of Life produced an invalid next-generation grid.");

    Value generation;
    if (!vm.globalTable().get(copyString("Generation", 10), &generation) || !isNumber(generation) || asNumber(generation) != 1.0)
        throw std::runtime_error("Game of Life did not advance its generation counter.");

    ImGuiContext* imguiContext = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(900.0f, 760.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    ImGui::NewFrame();
    applicationContext.BeginFrame();
    const InterpretResult frameResult = ScriptRuntime::Call(vm, applicationContext.GetUpdateFunction(), { Value(1.0 / 60.0) });
    applicationContext.EndFrame();
    ImGui::EndFrame();
    ImGui::DestroyContext(imguiContext);
    if (frameResult != InterpretResult::INTERPRET_OK || !textureCreated)
        throw std::runtime_error("Game of Life failed to draw an application frame.");
    std::cout << "Verified " << path.string() << " (cells=" << asList(cells)->items.size() << ", generation=1)\n";
}

Script MakeRockPaperScissors(const NodeRegistry& registry)
{
    Builder builder(registry);
    builder.script.main->functionDef->description = "Reads one move and plays a round of Rock Paper Scissors";

    ScriptFunctionPtr playRound = builder.Function("Play Round", "Chooses the computer move and prints the result");
    playRound->functionDef->inputs.push_back({ "Player Move", Value(0.0), builder.ids.GetNextId(), "Rock is 0, paper is 1, and scissors is 2" });
    ScriptPropertyPtr opponentMove = builder.LocalNumber(playRound, "Opponent Move", 0.0);

    Graph& roundGraph = playRound->Graph;
    NodePtr roundBegin = BuildBeginNode(builder.ids, playRound);
    NodePtr random = builder.Native("Random::Number");
    builder.Default(random, "Min", Value(0.0));
    builder.Default(random, "Max", Value(2.999999));
    NodePtr floor = builder.Native("Math::Floor");
    NodePtr setOpponent = builder.Set(opponentMove, playRound);
    builder.Add(roundGraph, { roundBegin, random, floor, setOpponent });
    builder.Link(roundGraph, roundBegin->Outputs[0], setOpponent->Inputs[0]);
    builder.Link(roundGraph, builder.Output(random, "Result"), builder.Input(floor, "Value"));
    builder.Link(roundGraph, builder.Output(floor, "Result"), setOpponent->Inputs[1]);

    NodePtr moveNames = MakeTypedList(builder, PinType::String);
    moveNames->AddInput(builder.ids);
    moveNames->AddInput(builder.ids);
    moveNames->AddInput(builder.ids);
    moveNames->Inputs[0].LiteralValue = StringValue("rock");
    moveNames->Inputs[1].LiteralValue = StringValue("paper");
    moveNames->Inputs[2].LiteralValue = StringValue("scissors");
    NodePtr getOpponentNameIndex = builder.Get(opponentMove, playRound);
    NodePtr getOpponentName = builder.Compiled("List::Get By Index");
    NodePtr announceText = builder.Compiled("String::Append");
    announceText->AddInput(builder.ids);
    announceText->Inputs[0].LiteralValue = StringValue("Computer chose ");
    announceText->Inputs[2].LiteralValue = StringValue(".");
    NodePtr announce = builder.Compiled("Debug::Print");
    builder.Add(roundGraph, { moveNames, getOpponentNameIndex, getOpponentName, announceText, announce });
    builder.Link(roundGraph, setOpponent->Outputs[0], announce->Inputs[0]);
    builder.Link(roundGraph, builder.Output(moveNames, "List"), builder.Input(getOpponentName, "List"));
    builder.Link(roundGraph, getOpponentNameIndex->Outputs[0], builder.Input(getOpponentName, "Index"));
    builder.Link(roundGraph, builder.Output(getOpponentName, "Value"), announceText->Inputs[1]);
    builder.Link(roundGraph, announceText->Outputs[0], builder.Input(announce, "Content"));

    NodePtr getOpponentForOutcome = builder.Get(opponentMove, playRound);
    NodePtr subtract = builder.Compiled("Math::Subtract");
    NodePtr addThree = NumberOperation(builder, "Math::Add", 3.0);
    NodePtr moduloThree = NumberOperation(builder, "Math::Modulo", 3.0);
    NodePtr outcome = builder.Compiled("Flow::Match");
    outcome->AddInput(builder.ids);
    outcome->Inputs[2].LiteralValue = Value(0.0);
    outcome->Inputs[3].LiteralValue = Value(1.0);
    NodePtr tie = builder.Compiled("Debug::Print");
    tie->Inputs[1].LiteralValue = StringValue("It is a tie.");
    NodePtr win = builder.Compiled("Debug::Print");
    win->Inputs[1].LiteralValue = StringValue("You win!");
    NodePtr lose = builder.Compiled("Debug::Print");
    lose->Inputs[1].LiteralValue = StringValue("The computer wins.");
    builder.Add(roundGraph, { getOpponentForOutcome, subtract, addThree, moduloThree, outcome, tie, win, lose });
    builder.Link(roundGraph, roundBegin->Outputs[1], subtract->Inputs[0]);
    builder.Link(roundGraph, getOpponentForOutcome->Outputs[0], subtract->Inputs[1]);
    builder.Link(roundGraph, subtract->Outputs[0], addThree->Inputs[0]);
    builder.Link(roundGraph, addThree->Outputs[0], moduloThree->Inputs[0]);
    builder.Link(roundGraph, announce->Outputs[0], outcome->Inputs[0]);
    builder.Link(roundGraph, moduloThree->Outputs[0], builder.Input(outcome, "Value"));
    builder.Link(roundGraph, outcome->Outputs[0], tie->Inputs[0]);
    builder.Link(roundGraph, outcome->Outputs[1], win->Inputs[0]);
    builder.Link(roundGraph, outcome->Outputs[2], lose->Inputs[0]);

    Graph& mainGraph = builder.script.main->Graph;
    NodePtr begin = BuildBeginNode(builder.ids, builder.script.main);
    NodePtr title = builder.Compiled("Debug::Print");
    title->Inputs[1].LiteralValue = StringValue("Rock Paper Scissors");
    NodePtr prompt = builder.Compiled("Debug::Print");
    prompt->Inputs[1].LiteralValue = StringValue("Choose rock, paper, or scissors:");
    NodePtr read = builder.Native("Console::Read Input");
    NodePtr lower = builder.Native("String::ToLower");
    NodePtr trim = builder.Native("String::Trim");
    NodePtr match = builder.Compiled("Flow::Match");
    match->AddInput(builder.ids);
    match->AddInput(builder.ids);
    match->Inputs[2].LiteralValue = StringValue("rock");
    match->Inputs[3].LiteralValue = StringValue("paper");
    match->Inputs[4].LiteralValue = StringValue("scissors");
    NodePtr playRock = builder.Call(playRound);
    builder.Default(playRock, "Player Move", Value(0.0));
    NodePtr playPaper = builder.Call(playRound);
    builder.Default(playPaper, "Player Move", Value(1.0));
    NodePtr playScissors = builder.Call(playRound);
    builder.Default(playScissors, "Player Move", Value(2.0));
    NodePtr invalid = builder.Compiled("Debug::Print");
    invalid->Inputs[1].LiteralValue = StringValue("That is not a valid move.");
    builder.Add(mainGraph, { begin, title, prompt, read, lower, trim, match, playRock, playPaper, playScissors, invalid });
    builder.Link(mainGraph, begin->Outputs[0], title->Inputs[0]);
    builder.Link(mainGraph, title->Outputs[0], prompt->Inputs[0]);
    builder.Link(mainGraph, prompt->Outputs[0], read->Inputs[0]);
    builder.Link(mainGraph, builder.Output(read, "Text"), builder.Input(lower, "Text"));
    builder.Link(mainGraph, builder.Output(lower, "Lowercase"), builder.Input(trim, "Text"));
    builder.Link(mainGraph, read->Outputs[0], match->Inputs[0]);
    builder.Link(mainGraph, builder.Output(trim, "Result"), builder.Input(match, "Value"));
    builder.Link(mainGraph, match->Outputs[0], playRock->Inputs[0]);
    builder.Link(mainGraph, match->Outputs[1], playPaper->Inputs[0]);
    builder.Link(mainGraph, match->Outputs[2], playScissors->Inputs[0]);
    builder.Link(mainGraph, match->Outputs[3], invalid->Inputs[0]);
    return std::move(builder.script);
}

Script MakeGameOfLife(const NodeRegistry& registry)
{
    Builder builder(registry);
    builder.script.main->functionDef->description = "Starts an interactive Conway's Game of Life application";
    ScriptPropertyPtr gridSize = builder.Number("Grid Size", 40.0);
    ScriptPropertyPtr cells = builder.NumberList("Cells");
    ScriptPropertyPtr nextCells = builder.NumberList("Next Cells");
    ScriptPropertyPtr running = builder.Boolean("Running", false);
    ScriptPropertyPtr generation = builder.Number("Generation", 0.0);
    ScriptPropertyPtr accumulator = builder.Number("Step Accumulator", 0.0);
    ScriptPropertyPtr interval = builder.Number("Step Interval", 0.12);
    ScriptPropertyPtr revision = builder.Number("Image Revision", 0.0);

    ScriptFunctionPtr cellAt = builder.Function("Cell At", "Returns a cell using wraparound grid coordinates", true);
    cellAt->functionDef->inputs.push_back({ "Row", Value(0.0), builder.ids.GetNextId(), "The row, which may be outside the grid" });
    cellAt->functionDef->inputs.push_back({ "Column", Value(0.0), builder.ids.GetNextId(), "The column, which may be outside the grid" });
    cellAt->functionDef->inputs.push_back({
        "Cells", Value(newList()), builder.ids.GetNextId(), TypeRef::List(PinType::Float), "The row-major cell values"
    });
    cellAt->functionDef->outputs.push_back({ "Cell", Value(0.0), builder.ids.GetNextId(), "One for a live cell and zero for a dead cell" });
    Graph& cellGraph = cellAt->Graph;
    NodePtr cellBegin = BuildBeginNode(builder.ids, cellAt);
    NodePtr getSizeForRowAdd = builder.Get(gridSize);
    NodePtr addRowSize = builder.Compiled("Math::Add");
    NodePtr getSizeForRowModulo = builder.Get(gridSize);
    NodePtr wrapRow = builder.Compiled("Math::Modulo");
    NodePtr getSizeForColumnAdd = builder.Get(gridSize);
    NodePtr addColumnSize = builder.Compiled("Math::Add");
    NodePtr getSizeForColumnModulo = builder.Get(gridSize);
    NodePtr wrapColumn = builder.Compiled("Math::Modulo");
    NodePtr getSizeForIndex = builder.Get(gridSize);
    NodePtr rowOffset = builder.Compiled("Math::Multiply");
    NodePtr index = builder.Compiled("Math::Add");
    NodePtr getCell = builder.Compiled("List::Get By Index");
    NodePtr cellReturn = BuildReturnNode(builder.ids, *cellAt);
    builder.Add(cellGraph, { cellBegin, getSizeForRowAdd, addRowSize, getSizeForRowModulo, wrapRow, getSizeForColumnAdd, addColumnSize,
        getSizeForColumnModulo, wrapColumn, getSizeForIndex, rowOffset, index, getCell, cellReturn });
    builder.Link(cellGraph, cellBegin->Outputs[0], cellReturn->Inputs[0]);
    builder.Link(cellGraph, cellBegin->Outputs[1], addRowSize->Inputs[0]);
    builder.Link(cellGraph, getSizeForRowAdd->Outputs[0], addRowSize->Inputs[1]);
    builder.Link(cellGraph, addRowSize->Outputs[0], wrapRow->Inputs[0]);
    builder.Link(cellGraph, getSizeForRowModulo->Outputs[0], wrapRow->Inputs[1]);
    builder.Link(cellGraph, cellBegin->Outputs[2], addColumnSize->Inputs[0]);
    builder.Link(cellGraph, getSizeForColumnAdd->Outputs[0], addColumnSize->Inputs[1]);
    builder.Link(cellGraph, addColumnSize->Outputs[0], wrapColumn->Inputs[0]);
    builder.Link(cellGraph, getSizeForColumnModulo->Outputs[0], wrapColumn->Inputs[1]);
    builder.Link(cellGraph, wrapRow->Outputs[0], rowOffset->Inputs[0]);
    builder.Link(cellGraph, getSizeForIndex->Outputs[0], rowOffset->Inputs[1]);
    builder.Link(cellGraph, rowOffset->Outputs[0], index->Inputs[0]);
    builder.Link(cellGraph, wrapColumn->Outputs[0], index->Inputs[1]);
    builder.Link(cellGraph, cellBegin->Outputs[3], builder.Input(getCell, "List"));
    builder.Link(cellGraph, index->Outputs[0], builder.Input(getCell, "Index"));
    builder.Link(cellGraph, builder.Output(getCell, "Value"), cellReturn->Inputs[1]);

    ScriptFunctionPtr reset = builder.Function("Reset Grid", "Fills the grid with a random population");
    Graph& resetGraph = reset->Graph;
    NodePtr resetBegin = BuildBeginNode(builder.ids, reset);
    builder.Add(resetGraph, resetBegin);
    const FreshList freshCells = InitializeFreshNumberList(builder, resetGraph, resetBegin->Outputs[0], cells);
    NodePtr resetRepeat = builder.Compiled("Flow::Repeat");
    NodePtr getResetWidth = builder.Get(gridSize);
    NodePtr getResetHeight = builder.Get(gridSize);
    NodePtr resetCount = builder.Compiled("Math::Multiply");
    NodePtr randomCell = builder.Native("Random::Number");
    builder.Default(randomCell, "Min", Value(0.0));
    builder.Default(randomCell, "Max", Value(1.0));
    NodePtr isAlive = NumberOperation(builder, "Math::Less Than", 0.28);
    NodePtr aliveBranch = builder.Compiled("Flow::Branch");
    NodePtr getCellsForReset = builder.Get(cells);
    NodePtr pushAlive = builder.Native("List::Push");
    builder.Default(pushAlive, "Value", Value(1.0));
    NodePtr pushDead = builder.Native("List::Push");
    builder.Default(pushDead, "Value", Value(0.0));
    NodePtr setGenerationZero = builder.Set(generation);
    builder.Default(setGenerationZero, "Generation", Value(0.0));
    NodePtr getRevisionForReset = builder.Get(revision);
    NodePtr incrementResetRevision = NumberOperation(builder, "Math::Add", 1.0);
    NodePtr setResetRevision = builder.Set(revision);
    builder.Add(resetGraph, { resetRepeat, getResetWidth, getResetHeight, resetCount, randomCell, isAlive, aliveBranch, getCellsForReset,
        pushAlive, pushDead, setGenerationZero, getRevisionForReset, incrementResetRevision, setResetRevision });
    builder.Link(resetGraph, freshCells.clear->Outputs[0], resetRepeat->Inputs[0]);
    builder.Link(resetGraph, getResetWidth->Outputs[0], resetCount->Inputs[0]);
    builder.Link(resetGraph, getResetHeight->Outputs[0], resetCount->Inputs[1]);
    builder.Link(resetGraph, resetCount->Outputs[0], builder.Input(resetRepeat, "Count"));
    builder.Link(resetGraph, builder.Output(randomCell, "Result"), isAlive->Inputs[0]);
    builder.Link(resetGraph, resetRepeat->Outputs[0], aliveBranch->Inputs[0]);
    builder.Link(resetGraph, isAlive->Outputs[0], aliveBranch->Inputs[1]);
    builder.Link(resetGraph, aliveBranch->Outputs[0], pushAlive->Inputs[0]);
    builder.Link(resetGraph, aliveBranch->Outputs[1], pushDead->Inputs[0]);
    builder.Link(resetGraph, getCellsForReset->Outputs[0], builder.Input(pushAlive, "List"));
    builder.Link(resetGraph, getCellsForReset->Outputs[0], builder.Input(pushDead, "List"));
    builder.Link(resetGraph, resetRepeat->Outputs[2], setGenerationZero->Inputs[0]);
    builder.Link(resetGraph, getRevisionForReset->Outputs[0], incrementResetRevision->Inputs[0]);
    builder.Link(resetGraph, incrementResetRevision->Outputs[0], setResetRevision->Inputs[1]);
    builder.Link(resetGraph, setGenerationZero->Outputs[0], setResetRevision->Inputs[0]);

    ScriptFunctionPtr step = builder.Function("Step Generation", "Calculates the next generation using Conway's rules");
    Graph& stepGraph = step->Graph;
    NodePtr stepBegin = BuildBeginNode(builder.ids, step);
    builder.Add(stepGraph, stepBegin);
    const FreshList freshNext = InitializeFreshNumberList(builder, stepGraph, stepBegin->Outputs[0], nextCells);
    NodePtr stepRepeat = builder.Compiled("Flow::Repeat");
    NodePtr getStepWidth = builder.Get(gridSize);
    NodePtr getStepHeight = builder.Get(gridSize);
    NodePtr stepCount = builder.Compiled("Math::Multiply");
    NodePtr getSizeForRow = builder.Get(gridSize);
    NodePtr divideIndex = builder.Compiled("Math::Divide");
    NodePtr row = builder.Native("Math::Floor");
    NodePtr getSizeForColumn = builder.Get(gridSize);
    NodePtr column = builder.Compiled("Math::Modulo");
    NodePtr getCellsForRead = builder.Get(cells);
    builder.Add(stepGraph, { stepRepeat, getStepWidth, getStepHeight, stepCount, getSizeForRow, divideIndex, row, getSizeForColumn, column, getCellsForRead });
    builder.Link(stepGraph, freshNext.clear->Outputs[0], stepRepeat->Inputs[0]);
    builder.Link(stepGraph, getStepWidth->Outputs[0], stepCount->Inputs[0]);
    builder.Link(stepGraph, getStepHeight->Outputs[0], stepCount->Inputs[1]);
    builder.Link(stepGraph, stepCount->Outputs[0], builder.Input(stepRepeat, "Count"));
    builder.Link(stepGraph, stepRepeat->Outputs[1], divideIndex->Inputs[0]);
    builder.Link(stepGraph, getSizeForRow->Outputs[0], divideIndex->Inputs[1]);
    builder.Link(stepGraph, divideIndex->Outputs[0], builder.Input(row, "Value"));
    builder.Link(stepGraph, stepRepeat->Outputs[1], column->Inputs[0]);
    builder.Link(stepGraph, getSizeForColumn->Outputs[0], column->Inputs[1]);

    std::vector<NodePtr> neighborCalls;
    for (int rowOffsetValue = -1; rowOffsetValue <= 1; ++rowOffsetValue)
    {
        for (int columnOffsetValue = -1; columnOffsetValue <= 1; ++columnOffsetValue)
        {
            if (rowOffsetValue == 0 && columnOffsetValue == 0)
                continue;
            NodePtr neighbor = builder.Call(cellAt);
            builder.Add(stepGraph, neighbor);
            if (rowOffsetValue == 0)
                builder.Link(stepGraph, builder.Output(row, "Result"), builder.Input(neighbor, "Row"));
            else
            {
                NodePtr offsetRow = NumberOperation(builder, "Math::Add", static_cast<double>(rowOffsetValue));
                builder.Add(stepGraph, offsetRow);
                builder.Link(stepGraph, builder.Output(row, "Result"), offsetRow->Inputs[0]);
                builder.Link(stepGraph, offsetRow->Outputs[0], builder.Input(neighbor, "Row"));
            }
            if (columnOffsetValue == 0)
                builder.Link(stepGraph, column->Outputs[0], builder.Input(neighbor, "Column"));
            else
            {
                NodePtr offsetColumn = NumberOperation(builder, "Math::Add", static_cast<double>(columnOffsetValue));
                builder.Add(stepGraph, offsetColumn);
                builder.Link(stepGraph, column->Outputs[0], offsetColumn->Inputs[0]);
                builder.Link(stepGraph, offsetColumn->Outputs[0], builder.Input(neighbor, "Column"));
            }
            builder.Link(stepGraph, getCellsForRead->Outputs[0], builder.Input(neighbor, "Cells"));
            neighborCalls.push_back(neighbor);
        }
    }

    NodePtr neighborTotal = builder.Compiled("Math::Add");
    for (size_t index = 2; index < neighborCalls.size(); ++index)
        neighborTotal->AddInput(builder.ids);
    NodePtr currentCell = builder.Call(cellAt);
    NodePtr exactlyThree = NumberOperation(builder, "Math::Equals", 3.0);
    NodePtr exactlyTwo = NumberOperation(builder, "Math::Equals", 2.0);
    NodePtr currentlyAlive = NumberOperation(builder, "Math::Equals", 1.0);
    NodePtr survives = builder.Compiled("Logic::And");
    NodePtr nextAlive = builder.Compiled("Logic::Or");
    NodePtr nextBranch = builder.Compiled("Flow::Branch");
    NodePtr getNextForPush = builder.Get(nextCells);
    NodePtr pushNextAlive = builder.Native("List::Push");
    builder.Default(pushNextAlive, "Value", Value(1.0));
    NodePtr pushNextDead = builder.Native("List::Push");
    builder.Default(pushNextDead, "Value", Value(0.0));
    builder.Add(stepGraph, { neighborTotal, currentCell, exactlyThree, exactlyTwo, currentlyAlive, survives, nextAlive, nextBranch, getNextForPush,
        pushNextAlive, pushNextDead });
    for (size_t index = 0; index < neighborCalls.size(); ++index)
        builder.Link(stepGraph, builder.Output(neighborCalls[index], "Cell"), neighborTotal->Inputs[index]);
    builder.Link(stepGraph, builder.Output(row, "Result"), builder.Input(currentCell, "Row"));
    builder.Link(stepGraph, column->Outputs[0], builder.Input(currentCell, "Column"));
    builder.Link(stepGraph, getCellsForRead->Outputs[0], builder.Input(currentCell, "Cells"));
    builder.Link(stepGraph, neighborTotal->Outputs[0], exactlyThree->Inputs[0]);
    builder.Link(stepGraph, neighborTotal->Outputs[0], exactlyTwo->Inputs[0]);
    builder.Link(stepGraph, builder.Output(currentCell, "Cell"), currentlyAlive->Inputs[0]);
    builder.Link(stepGraph, currentlyAlive->Outputs[0], survives->Inputs[0]);
    builder.Link(stepGraph, exactlyTwo->Outputs[0], survives->Inputs[1]);
    builder.Link(stepGraph, exactlyThree->Outputs[0], nextAlive->Inputs[0]);
    builder.Link(stepGraph, survives->Outputs[0], nextAlive->Inputs[1]);
    builder.Link(stepGraph, stepRepeat->Outputs[0], nextBranch->Inputs[0]);
    builder.Link(stepGraph, nextAlive->Outputs[0], nextBranch->Inputs[1]);
    builder.Link(stepGraph, nextBranch->Outputs[0], pushNextAlive->Inputs[0]);
    builder.Link(stepGraph, nextBranch->Outputs[1], pushNextDead->Inputs[0]);
    builder.Link(stepGraph, getNextForPush->Outputs[0], builder.Input(pushNextAlive, "List"));
    builder.Link(stepGraph, getNextForPush->Outputs[0], builder.Input(pushNextDead, "List"));

    NodePtr getNextForSwap = builder.Get(nextCells);
    NodePtr setCells = builder.Set(cells);
    NodePtr getGeneration = builder.Get(generation);
    NodePtr incrementGeneration = NumberOperation(builder, "Math::Add", 1.0);
    NodePtr setGeneration = builder.Set(generation);
    NodePtr getRevisionForStep = builder.Get(revision);
    NodePtr incrementStepRevision = NumberOperation(builder, "Math::Add", 1.0);
    NodePtr setStepRevision = builder.Set(revision);
    NodePtr setAccumulatorZero = builder.Set(accumulator);
    builder.Default(setAccumulatorZero, "Step Accumulator", Value(0.0));
    builder.Add(stepGraph, { getNextForSwap, setCells, getGeneration, incrementGeneration, setGeneration, getRevisionForStep, incrementStepRevision,
        setStepRevision, setAccumulatorZero });
    builder.Link(stepGraph, stepRepeat->Outputs[2], setCells->Inputs[0]);
    builder.Link(stepGraph, getNextForSwap->Outputs[0], setCells->Inputs[1]);
    builder.Link(stepGraph, setCells->Outputs[0], setGeneration->Inputs[0]);
    builder.Link(stepGraph, getGeneration->Outputs[0], incrementGeneration->Inputs[0]);
    builder.Link(stepGraph, incrementGeneration->Outputs[0], setGeneration->Inputs[1]);
    builder.Link(stepGraph, setGeneration->Outputs[0], setStepRevision->Inputs[0]);
    builder.Link(stepGraph, getRevisionForStep->Outputs[0], incrementStepRevision->Inputs[0]);
    builder.Link(stepGraph, incrementStepRevision->Outputs[0], setStepRevision->Inputs[1]);
    builder.Link(stepGraph, setStepRevision->Outputs[0], setAccumulatorZero->Inputs[0]);

    ScriptFunctionPtr maybeReset = builder.Function("Maybe Reset", "Resets the grid when the button was pressed");
    maybeReset->functionDef->inputs.push_back({ "Requested", Value(false), builder.ids.GetNextId() });
    Graph& maybeResetGraph = maybeReset->Graph;
    NodePtr maybeResetBegin = BuildBeginNode(builder.ids, maybeReset);
    NodePtr resetBranch = builder.Compiled("Flow::Branch");
    NodePtr resetCall = builder.Call(reset);
    builder.Add(maybeResetGraph, { maybeResetBegin, resetBranch, resetCall });
    builder.Link(maybeResetGraph, maybeResetBegin->Outputs[0], resetBranch->Inputs[0]);
    builder.Link(maybeResetGraph, maybeResetBegin->Outputs[1], resetBranch->Inputs[1]);
    builder.Link(maybeResetGraph, resetBranch->Outputs[0], resetCall->Inputs[0]);

    ScriptFunctionPtr updateClock = builder.Function("Update Clock", "Accumulates elapsed time only while the simulation is running");
    updateClock->functionDef->inputs.push_back({ "Is Running", Value(false), builder.ids.GetNextId() });
    updateClock->functionDef->inputs.push_back({ "Delta Time", Value(0.0), builder.ids.GetNextId() });
    Graph& clockGraph = updateClock->Graph;
    NodePtr clockBegin = BuildBeginNode(builder.ids, updateClock);
    NodePtr clockBranch = builder.Compiled("Flow::Branch");
    NodePtr getAccumulator = builder.Get(accumulator);
    NodePtr addDelta = builder.Compiled("Math::Add");
    NodePtr setAccumulator = builder.Set(accumulator);
    NodePtr clearAccumulator = builder.Set(accumulator);
    builder.Default(clearAccumulator, "Step Accumulator", Value(0.0));
    builder.Add(clockGraph, { clockBegin, clockBranch, getAccumulator, addDelta, setAccumulator, clearAccumulator });
    builder.Link(clockGraph, clockBegin->Outputs[0], clockBranch->Inputs[0]);
    builder.Link(clockGraph, clockBegin->Outputs[1], clockBranch->Inputs[1]);
    builder.Link(clockGraph, clockBranch->Outputs[0], setAccumulator->Inputs[0]);
    builder.Link(clockGraph, clockBranch->Outputs[1], clearAccumulator->Inputs[0]);
    builder.Link(clockGraph, getAccumulator->Outputs[0], addDelta->Inputs[0]);
    builder.Link(clockGraph, clockBegin->Outputs[2], addDelta->Inputs[1]);
    builder.Link(clockGraph, addDelta->Outputs[0], setAccumulator->Inputs[1]);

    ScriptFunctionPtr maybeStep = builder.Function("Maybe Step", "Advances the simulation when requested");
    maybeStep->functionDef->inputs.push_back({ "Requested", Value(false), builder.ids.GetNextId() });
    Graph& maybeStepGraph = maybeStep->Graph;
    NodePtr maybeStepBegin = BuildBeginNode(builder.ids, maybeStep);
    NodePtr stepBranch = builder.Compiled("Flow::Branch");
    NodePtr stepCall = builder.Call(step);
    builder.Add(maybeStepGraph, { maybeStepBegin, stepBranch, stepCall });
    builder.Link(maybeStepGraph, maybeStepBegin->Outputs[0], stepBranch->Inputs[0]);
    builder.Link(maybeStepGraph, maybeStepBegin->Outputs[1], stepBranch->Inputs[1]);
    builder.Link(maybeStepGraph, stepBranch->Outputs[0], stepCall->Inputs[0]);

    ScriptFunctionPtr update = builder.Function("Update Game", "Draws controls and advances the simulation");
    update->functionDef->inputs.push_back({ "Delta Time", Value(0.0), builder.ids.GetNextId(), "Seconds since the previous frame" });
    Graph& updateGraph = update->Graph;
    NodePtr updateBegin = BuildBeginNode(builder.ids, update);
    NodePtr title = builder.Native("UI::Text");
    builder.Default(title, "Text", StringValue("Conway's Game of Life"));
    NodePtr generationText = builder.Compiled("String::Append");
    builder.Default(generationText, "A", StringValue("Generation: "));
    NodePtr getGenerationForText = builder.Get(generation);
    NodePtr generationLabel = builder.Native("UI::Text");
    NodePtr speedSlider = builder.Native("UI::Slider Number");
    builder.Default(speedSlider, "Label", StringValue("Seconds per generation"));
    builder.Default(speedSlider, "Minimum", Value(0.03));
    builder.Default(speedSlider, "Maximum", Value(0.5));
    NodePtr getInterval = builder.Get(interval);
    NodePtr setInterval = builder.Set(interval);
    NodePtr toggleButton = builder.Native("UI::Button");
    builder.Default(toggleButton, "Label", StringValue("Run / Pause"));
    NodePtr sameLineOne = builder.Native("UI::Same Line");
    NodePtr stepButton = builder.Native("UI::Button");
    builder.Default(stepButton, "Label", StringValue("Step"));
    NodePtr sameLineTwo = builder.Native("UI::Same Line");
    NodePtr resetButton = builder.Native("UI::Button");
    builder.Default(resetButton, "Label", StringValue("Randomize"));
    builder.Add(updateGraph, { updateBegin, title, generationText, getGenerationForText, generationLabel, speedSlider, getInterval, setInterval,
        toggleButton, sameLineOne, stepButton, sameLineTwo, resetButton });
    builder.Link(updateGraph, updateBegin->Outputs[0], title->Inputs[0]);
    builder.Link(updateGraph, getGenerationForText->Outputs[0], generationText->Inputs[1]);
    builder.Link(updateGraph, generationText->Outputs[0], builder.Input(generationLabel, "Text"));
    builder.Link(updateGraph, title->Outputs[0], generationLabel->Inputs[0]);
    builder.Link(updateGraph, generationLabel->Outputs[0], speedSlider->Inputs[0]);
    builder.Link(updateGraph, getInterval->Outputs[0], builder.Input(speedSlider, "Value"));
    builder.Link(updateGraph, speedSlider->Outputs[0], setInterval->Inputs[0]);
    builder.Link(updateGraph, builder.Output(speedSlider, "Value"), setInterval->Inputs[1]);
    builder.Link(updateGraph, setInterval->Outputs[0], toggleButton->Inputs[0]);
    builder.Link(updateGraph, toggleButton->Outputs[0], sameLineOne->Inputs[0]);
    builder.Link(updateGraph, sameLineOne->Outputs[0], stepButton->Inputs[0]);
    builder.Link(updateGraph, stepButton->Outputs[0], sameLineTwo->Inputs[0]);
    builder.Link(updateGraph, sameLineTwo->Outputs[0], resetButton->Inputs[0]);

    NodePtr getRunningForToggle = builder.Get(running);
    NodePtr toggleRunning = builder.Compiled("Math::Not Equals");
    NodePtr setRunning = builder.Set(running);
    NodePtr maybeResetCall = builder.Call(maybeReset);
    NodePtr getRunningForClock = builder.Get(running);
    NodePtr updateClockCall = builder.Call(updateClock);
    builder.Add(updateGraph, { getRunningForToggle, toggleRunning, setRunning, maybeResetCall, getRunningForClock, updateClockCall });
    builder.Link(updateGraph, getRunningForToggle->Outputs[0], toggleRunning->Inputs[0]);
    builder.Link(updateGraph, builder.Output(toggleButton, "Pressed"), toggleRunning->Inputs[1]);
    builder.Link(updateGraph, resetButton->Outputs[0], setRunning->Inputs[0]);
    builder.Link(updateGraph, toggleRunning->Outputs[0], setRunning->Inputs[1]);
    builder.Link(updateGraph, setRunning->Outputs[0], maybeResetCall->Inputs[0]);
    builder.Link(updateGraph, builder.Output(resetButton, "Pressed"), builder.Input(maybeResetCall, "Requested"));
    builder.Link(updateGraph, maybeResetCall->Outputs[0], updateClockCall->Inputs[0]);
    builder.Link(updateGraph, getRunningForClock->Outputs[0], builder.Input(updateClockCall, "Is Running"));
    builder.Link(updateGraph, updateBegin->Outputs[1], builder.Input(updateClockCall, "Delta Time"));

    NodePtr getRunningForAutomatic = builder.Get(running);
    NodePtr getAccumulatorForCheck = builder.Get(accumulator);
    NodePtr getIntervalForCheck = builder.Get(interval);
    NodePtr intervalElapsed = builder.Compiled("Math::Greater Or Equal");
    NodePtr automaticStep = builder.Compiled("Logic::And");
    NodePtr requestedStep = builder.Compiled("Logic::Or");
    NodePtr notReset = builder.Compiled("Logic::Not");
    NodePtr allowedStep = builder.Compiled("Logic::And");
    NodePtr maybeStepCall = builder.Call(maybeStep);
    builder.Add(updateGraph, { getRunningForAutomatic, getAccumulatorForCheck, getIntervalForCheck, intervalElapsed, automaticStep, requestedStep,
        notReset, allowedStep, maybeStepCall });
    builder.Link(updateGraph, getAccumulatorForCheck->Outputs[0], intervalElapsed->Inputs[0]);
    builder.Link(updateGraph, getIntervalForCheck->Outputs[0], intervalElapsed->Inputs[1]);
    builder.Link(updateGraph, getRunningForAutomatic->Outputs[0], automaticStep->Inputs[0]);
    builder.Link(updateGraph, intervalElapsed->Outputs[0], automaticStep->Inputs[1]);
    builder.Link(updateGraph, builder.Output(stepButton, "Pressed"), requestedStep->Inputs[0]);
    builder.Link(updateGraph, automaticStep->Outputs[0], requestedStep->Inputs[1]);
    builder.Link(updateGraph, builder.Output(resetButton, "Pressed"), notReset->Inputs[0]);
    builder.Link(updateGraph, requestedStep->Outputs[0], allowedStep->Inputs[0]);
    builder.Link(updateGraph, notReset->Outputs[0], allowedStep->Inputs[1]);
    builder.Link(updateGraph, updateClockCall->Outputs[0], maybeStepCall->Inputs[0]);
    builder.Link(updateGraph, allowedStep->Outputs[0], builder.Input(maybeStepCall, "Requested"));

    NodePtr separator = builder.Native("UI::Separator");
    NodePtr image = builder.Native("UI::Image");
    builder.Default(image, "Id", StringValue("GameOfLife"));
    builder.Default(image, "Maximum", Value(2.0));
    NodePtr getCellsForImage = builder.Get(cells);
    NodePtr getWidthForImage = builder.Get(gridSize);
    NodePtr getHeightForImage = builder.Get(gridSize);
    NodePtr getRevisionForImage = builder.Get(revision);
    builder.Add(updateGraph, { separator, image, getCellsForImage, getWidthForImage, getHeightForImage, getRevisionForImage });
    builder.Link(updateGraph, maybeStepCall->Outputs[0], separator->Inputs[0]);
    builder.Link(updateGraph, separator->Outputs[0], image->Inputs[0]);
    builder.Link(updateGraph, getCellsForImage->Outputs[0], builder.Input(image, "Values"));
    builder.Link(updateGraph, getWidthForImage->Outputs[0], builder.Input(image, "Width"));
    builder.Link(updateGraph, getHeightForImage->Outputs[0], builder.Input(image, "Height"));
    builder.Link(updateGraph, getRevisionForImage->Outputs[0], builder.Input(image, "Revision"));

    Graph& mainGraph = builder.script.main->Graph;
    NodePtr mainBegin = BuildBeginNode(builder.ids, builder.script.main);
    NodePtr initialReset = builder.Call(reset);
    NodePtr start = builder.Native("UI::Start");
    NodePtr getUpdate = BuildGetFunctionNode(builder.ids, update->functionDef, update->ID);
    builder.Add(mainGraph, { mainBegin, initialReset, start, getUpdate });
    builder.Link(mainGraph, mainBegin->Outputs[0], initialReset->Inputs[0]);
    builder.Link(mainGraph, initialReset->Outputs[0], start->Inputs[0]);
    builder.Link(mainGraph, getUpdate->Outputs[0], builder.Input(start, "Update"));
    return std::move(builder.script);
}
}
