#pragma once

#include <imgui.h>

#include <vector>

namespace GraphLayout
{
struct Node
{
    int id = 0;
    ImVec2 position = ImVec2(0, 0);
    ImVec2 size = ImVec2(0, 0);
    bool root = false;
    bool execution = false;
};

struct Edge
{
    int from = 0;
    int to = 0;
    int sourceOrder = 0;
    int targetOrder = 0;
    bool flow = false;
};

struct Options
{
    float columnGap = 120.0f;
    float rowGap = 60.0f;
    float componentGap = 160.0f;
    float dataColumnGap = 70.0f;
    float dataRowGap = 35.0f;
    int crossingReductionPasses = 4;
};

struct Position
{
    int id = 0;
    ImVec2 value = ImVec2(0, 0);
};

std::vector<Position> Calculate(const std::vector<Node>& nodes, const std::vector<Edge>& edges, const Options& options = {});
}
