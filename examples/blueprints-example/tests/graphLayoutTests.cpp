#include "graphLayoutTests.h"

#include "testFramework.h"
#include "../graphs/graphLayout.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace
{
using Tests::Require;

std::map<int, ImVec2> Positions(const std::vector<GraphLayout::Position>& positions)
{
    std::map<int, ImVec2> result;
    for (const GraphLayout::Position& position : positions)
        result[position.id] = position.value;
    return result;
}

bool Overlaps(const GraphLayout::Node& left, const ImVec2& leftPosition, const GraphLayout::Node& right, const ImVec2& rightPosition)
{
    return leftPosition.x < rightPosition.x + right.size.x && leftPosition.x + left.size.x > rightPosition.x &&
           leftPosition.y < rightPosition.y + right.size.y && leftPosition.y + left.size.y > rightPosition.y;
}

void RequireNoOverlap(const std::vector<GraphLayout::Node>& nodes, const std::map<int, ImVec2>& positions)
{
    for (size_t left = 0; left < nodes.size(); ++left)
        for (size_t right = left + 1; right < nodes.size(); ++right)
            Require(!Overlaps(nodes[left], positions.at(nodes[left].id), nodes[right], positions.at(nodes[right].id)), "The layout contains overlapping nodes.");
}

void ChainMovesLeftToRight()
{
    const std::vector<GraphLayout::Node> nodes = {
        { 1, ImVec2(20, 30), ImVec2(160, 80), true, true },
        { 2, ImVec2(40, 50), ImVec2(220, 100), false, true },
        { 3, ImVec2(60, 70), ImVec2(140, 120), false, true },
    };
    const std::vector<GraphLayout::Edge> edges = {
        { 1, 2, 0, 0, true },
        { 2, 3, 0, 0, true },
    };
    const auto positions = Positions(GraphLayout::Calculate(nodes, edges));
    Require(positions.at(1).x < positions.at(2).x && positions.at(2).x < positions.at(3).x, "A chain was not arranged left to right.");
    Require(std::abs(positions.at(1).x - 20.0f) < 0.001f, "The layout did not preserve the graph's left edge.");
    RequireNoOverlap(nodes, positions);
}

void BranchesFollowOutputOrder()
{
    const std::vector<GraphLayout::Node> nodes = {
        { 1, ImVec2(0, 0), ImVec2(160, 90), true, true },
        { 2, ImVec2(0, 300), ImVec2(180, 90), false, true },
        { 3, ImVec2(0, 100), ImVec2(180, 90), false, true },
    };
    const std::vector<GraphLayout::Edge> edges = {
        { 1, 2, 0, 0, true },
        { 1, 3, 1, 0, true },
    };
    const auto positions = Positions(GraphLayout::Calculate(nodes, edges));
    Require(positions.at(2).y < positions.at(3).y, "Branch order did not follow output-pin order.");
    RequireNoOverlap(nodes, positions);
}

void CyclesAndDisconnectedNodesRemainFinite()
{
    const std::vector<GraphLayout::Node> nodes = {
        { 1, ImVec2(-50, -30), ImVec2(150, 90), true, true },
        { 2, ImVec2(0, 0), ImVec2(150, 90), false, true },
        { 3, ImVec2(0, 0), ImVec2(150, 90), false, true },
        { 4, ImVec2(400, 100), ImVec2(120, 70), false },
    };
    const std::vector<GraphLayout::Edge> edges = {
        { 1, 2, 0, 0, true },
        { 2, 3, 0, 0, true },
        { 3, 2, 0, 0, true },
    };
    const auto positions = Positions(GraphLayout::Calculate(nodes, edges));
    Require(positions.size() == nodes.size(), "The layout lost a node in a cycle or disconnected component.");
    for (const auto& [id, position] : positions)
    {
        (void)id;
        Require(std::isfinite(position.x) && std::isfinite(position.y), "The layout produced a non-finite position.");
    }
    RequireNoOverlap(nodes, positions);
}

void DataDependenciesClusterAroundTheirExecutionConsumer()
{
    const std::vector<GraphLayout::Node> nodes = {
        { 1, ImVec2(0, 100), ImVec2(160, 90), true, true },
        { 2, ImVec2(600, 100), ImVec2(180, 100), false, true },
        { 3, ImVec2(1000, 100), ImVec2(170, 90), false, true },
        { 10, ImVec2(100, 400), ImVec2(150, 70), false, false },
        { 11, ImVec2(300, 350), ImVec2(180, 90), false, false },
        { 12, ImVec2(100, 500), ImVec2(140, 70), false, false },
    };
    const std::vector<GraphLayout::Edge> edges = {
        { 1, 2, 0, 0, true },
        { 2, 3, 0, 0, true },
        { 10, 11, 0, 0, false },
        { 12, 11, 0, 1, false },
        { 11, 2, 0, 1, false },
    };
    const auto positions = Positions(GraphLayout::Calculate(nodes, edges));
    Require(positions.at(1).x < positions.at(2).x && positions.at(2).x < positions.at(3).x, "Data dependencies distorted the execution order.");
    Require(positions.at(10).x < positions.at(11).x && positions.at(11).x < positions.at(2).x, "Pulled data nodes were not placed upstream of their consumer.");
    const float directGap = positions.at(2).x - (positions.at(11).x + nodes[4].size.x);
    Require(std::abs(directGap - GraphLayout::Options().dataColumnGap) < 0.001f, "A direct data dependency was not kept close to its consumer.");
    Require(positions.at(11).y + nodes[4].size.y < positions.at(2).y, "Pulled data nodes obstruct the execution-flow row.");
    const float beginCenter = positions.at(1).y + nodes[0].size.y * 0.5f;
    const float consumerCenter = positions.at(2).y + nodes[1].size.y * 0.5f;
    const float returnCenter = positions.at(3).y + nodes[2].size.y * 0.5f;
    Require(std::abs(beginCenter - consumerCenter) < 0.001f && std::abs(consumerCenter - returnCenter) < 0.001f,
            "Data clusters pulled execution nodes away from the flow backbone.");
    RequireNoOverlap(nodes, positions);
}

void LayoutIsDeterministic()
{
    std::vector<GraphLayout::Node> nodes = {
        { 4, ImVec2(800, 400), ImVec2(140, 80), false, true },
        { 2, ImVec2(200, 300), ImVec2(180, 100), false, true },
        { 1, ImVec2(100, 200), ImVec2(160, 90), true, true },
        { 3, ImVec2(600, 100), ImVec2(190, 120), false, true },
    };
    const std::vector<GraphLayout::Edge> edges = {
        { 1, 2, 0, 0, true },
        { 1, 3, 1, 0, true },
        { 2, 4, 0, 0, false },
        { 3, 4, 0, 1, false },
    };
    const std::vector<GraphLayout::Position> first = GraphLayout::Calculate(nodes, edges);
    const std::map<int, ImVec2> firstById = Positions(first);
    for (GraphLayout::Node& node : nodes)
        node.position = firstById.at(node.id);
    const std::vector<GraphLayout::Position> second = GraphLayout::Calculate(nodes, edges);
    Require(first.size() == second.size(), "Repeated layouts returned different node counts.");
    for (size_t index = 0; index < first.size(); ++index)
        Require(first[index].id == second[index].id && first[index].value.x == second[index].value.x && first[index].value.y == second[index].value.y,
                "Repeated layouts returned different positions.");
}
}

void AddGraphLayoutTests(Tests::Runner& runner)
{
    runner.Group("Graph layout", [&]()
    {
        runner.Test("chains move left to right", ChainMovesLeftToRight);
        runner.Test("branches follow output order", BranchesFollowOutputOrder);
        runner.Test("cycles and disconnected nodes remain finite", CyclesAndDisconnectedNodesRemainFinite);
        runner.Test("data dependencies cluster around their execution consumer", DataDependenciesClusterAroundTheirExecutionConsumer);
        runner.Test("layout is deterministic", LayoutIsDeterministic);
    });
}
