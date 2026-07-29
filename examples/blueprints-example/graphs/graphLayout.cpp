#include "graphLayout.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <tuple>

namespace
{
struct EdgeRecord
{
    size_t from = 0;
    size_t to = 0;
    int sourceOrder = 0;
    int targetOrder = 0;
    int weight = 1;
};

struct ComponentLayout
{
    std::vector<size_t> nodes;
    std::map<size_t, ImVec2> positions;
    ImVec2 minimum = ImVec2(0, 0);
    ImVec2 maximum = ImVec2(0, 0);
    ImVec2 oldMinimum = ImVec2(0, 0);
    bool hasRoot = false;
    int minimumId = 0;
};

bool IsFinite(const ImVec2& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

float NodeWidth(const GraphLayout::Node& node)
{
    return (std::max)(1.0f, node.size.x);
}

float NodeHeight(const GraphLayout::Node& node)
{
    return (std::max)(1.0f, node.size.y);
}

void Measure(ComponentLayout& component, const std::vector<GraphLayout::Node>& nodes)
{
    component.minimum = ImVec2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    component.maximum = ImVec2(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
    for (size_t nodeIndex : component.nodes)
    {
        const ImVec2 position = component.positions.at(nodeIndex);
        component.minimum.x = (std::min)(component.minimum.x, position.x);
        component.minimum.y = (std::min)(component.minimum.y, position.y);
        component.maximum.x = (std::max)(component.maximum.x, position.x + NodeWidth(nodes[nodeIndex]));
        component.maximum.y = (std::max)(component.maximum.y, position.y + NodeHeight(nodes[nodeIndex]));
    }
}

ComponentLayout LayoutComponent(const std::vector<size_t>& componentNodes, const std::vector<GraphLayout::Node>& nodes,
                                const std::vector<EdgeRecord>& edges, const GraphLayout::Options& options)
{
    ComponentLayout result;
    result.nodes = componentNodes;
    result.minimumId = std::numeric_limits<int>::max();
    result.oldMinimum = ImVec2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());

    std::set<size_t> componentSet(componentNodes.begin(), componentNodes.end());
    for (size_t nodeIndex : componentNodes)
    {
        result.hasRoot |= nodes[nodeIndex].root;
        result.minimumId = (std::min)(result.minimumId, nodes[nodeIndex].id);
        result.oldMinimum.x = (std::min)(result.oldMinimum.x, nodes[nodeIndex].position.x);
        result.oldMinimum.y = (std::min)(result.oldMinimum.y, nodes[nodeIndex].position.y);
    }

    std::vector<std::vector<size_t>> outgoing(nodes.size());
    for (const EdgeRecord& edge : edges)
        if (componentSet.count(edge.from) && componentSet.count(edge.to))
            outgoing[edge.from].push_back(edge.to);

    std::vector<int> discovery(nodes.size(), -1);
    std::vector<int> lowLink(nodes.size(), -1);
    std::vector<int> stronglyConnectedComponent(nodes.size(), -1);
    std::vector<size_t> stack;
    std::vector<bool> onStack(nodes.size(), false);
    int nextDiscovery = 0;
    int componentCount = 0;
    std::function<void(size_t)> findComponents = [&](size_t nodeIndex)
    {
        discovery[nodeIndex] = lowLink[nodeIndex] = nextDiscovery++;
        stack.push_back(nodeIndex);
        onStack[nodeIndex] = true;

        for (size_t successor : outgoing[nodeIndex])
        {
            if (discovery[successor] < 0)
            {
                findComponents(successor);
                lowLink[nodeIndex] = (std::min)(lowLink[nodeIndex], lowLink[successor]);
            }
            else if (onStack[successor])
                lowLink[nodeIndex] = (std::min)(lowLink[nodeIndex], discovery[successor]);
        }

        if (lowLink[nodeIndex] != discovery[nodeIndex])
            return;

        while (!stack.empty())
        {
            const size_t member = stack.back();
            stack.pop_back();
            onStack[member] = false;
            stronglyConnectedComponent[member] = componentCount;
            if (member == nodeIndex)
                break;
        }
        ++componentCount;
    };

    for (size_t nodeIndex : componentNodes)
        if (discovery[nodeIndex] < 0)
            findComponents(nodeIndex);

    std::vector<std::set<int>> componentSuccessors(static_cast<size_t>(componentCount));
    std::vector<int> indegree(static_cast<size_t>(componentCount), 0);
    for (const EdgeRecord& edge : edges)
    {
        if (!componentSet.count(edge.from) || !componentSet.count(edge.to))
            continue;
        const int from = stronglyConnectedComponent[edge.from];
        const int to = stronglyConnectedComponent[edge.to];
        if (from != to && componentSuccessors[static_cast<size_t>(from)].insert(to).second)
            ++indegree[static_cast<size_t>(to)];
    }

    std::priority_queue<int, std::vector<int>, std::greater<int>> ready;
    for (int component = 0; component < componentCount; ++component)
        if (indegree[static_cast<size_t>(component)] == 0)
            ready.push(component);

    std::vector<int> layerByComponent(static_cast<size_t>(componentCount), 0);
    while (!ready.empty())
    {
        const int component = ready.top();
        ready.pop();
        for (int successor : componentSuccessors[static_cast<size_t>(component)])
        {
            layerByComponent[static_cast<size_t>(successor)] =
                (std::max)(layerByComponent[static_cast<size_t>(successor)], layerByComponent[static_cast<size_t>(component)] + 1);
            if (--indegree[static_cast<size_t>(successor)] == 0)
                ready.push(successor);
        }
    }

    int maximumLayer = 0;
    std::vector<int> layerByNode(nodes.size(), 0);
    for (size_t nodeIndex : componentNodes)
    {
        const int layer = layerByComponent[static_cast<size_t>(stronglyConnectedComponent[nodeIndex])];
        layerByNode[nodeIndex] = layer;
        maximumLayer = (std::max)(maximumLayer, layer);
    }

    std::vector<std::vector<size_t>> layers(static_cast<size_t>(maximumLayer + 1));
    for (size_t nodeIndex : componentNodes)
        layers[static_cast<size_t>(layerByNode[nodeIndex])].push_back(nodeIndex);
    for (std::vector<size_t>& layer : layers)
    {
        std::stable_sort(layer.begin(), layer.end(), [&](size_t left, size_t right)
        {
            if (nodes[left].position.y != nodes[right].position.y)
                return nodes[left].position.y < nodes[right].position.y;
            if (nodes[left].root != nodes[right].root)
                return nodes[left].root;
            return nodes[left].id < nodes[right].id;
        });
    }

    const auto reduceCrossings = [&](int layerIndex, bool usePredecessors)
    {
        std::vector<size_t>& layer = layers[static_cast<size_t>(layerIndex)];
        std::map<size_t, size_t> order;
        for (const std::vector<size_t>& orderedLayer : layers)
            for (size_t position = 0; position < orderedLayer.size(); ++position)
                order[orderedLayer[position]] = position;

        struct Score
        {
            size_t node = 0;
            double barycenter = 0.0;
            size_t previousOrder = 0;
        };
        std::vector<Score> scores;
        scores.reserve(layer.size());
        for (size_t nodeIndex : layer)
        {
            double total = 0.0;
            int totalWeight = 0;
            for (const EdgeRecord& edge : edges)
            {
                const bool connected = usePredecessors ? edge.to == nodeIndex : edge.from == nodeIndex;
                if (!connected)
                    continue;
                const size_t neighbor = usePredecessors ? edge.from : edge.to;
                if (!componentSet.count(neighbor) || layerByNode[neighbor] == layerIndex)
                    continue;
                const double pinBias = static_cast<double>(usePredecessors ? edge.sourceOrder : edge.targetOrder) * 0.125;
                total += (static_cast<double>(order[neighbor]) + pinBias) * edge.weight;
                totalWeight += edge.weight;
            }
            const size_t previousOrder = order[nodeIndex];
            scores.push_back({ nodeIndex, totalWeight > 0 ? total / totalWeight : static_cast<double>(previousOrder), previousOrder });
        }
        std::stable_sort(scores.begin(), scores.end(), [&](const Score& left, const Score& right)
        {
            if (left.barycenter != right.barycenter)
                return left.barycenter < right.barycenter;
            if (left.previousOrder != right.previousOrder)
                return left.previousOrder < right.previousOrder;
            return nodes[left.node].id < nodes[right.node].id;
        });
        for (size_t position = 0; position < scores.size(); ++position)
            layer[position] = scores[position].node;
    };

    for (int pass = 0; pass < (std::max)(0, options.crossingReductionPasses); ++pass)
    {
        for (int layer = 1; layer <= maximumLayer; ++layer)
            reduceCrossings(layer, true);
        for (int layer = maximumLayer - 1; layer >= 0; --layer)
            reduceCrossings(layer, false);
    }

    std::vector<float> layerWidths(layers.size(), 0.0f);
    for (size_t layer = 0; layer < layers.size(); ++layer)
        for (size_t nodeIndex : layers[layer])
            layerWidths[layer] = (std::max)(layerWidths[layer], NodeWidth(nodes[nodeIndex]));

    std::vector<float> layerX(layers.size(), 0.0f);
    for (size_t layer = 1; layer < layers.size(); ++layer)
        layerX[layer] = layerX[layer - 1] + layerWidths[layer - 1] + options.columnGap;

    for (size_t layer = 0; layer < layers.size(); ++layer)
    {
        float totalHeight = 0.0f;
        for (size_t nodeIndex : layers[layer])
            totalHeight += NodeHeight(nodes[nodeIndex]);
        if (layers[layer].size() > 1)
            totalHeight += options.rowGap * static_cast<float>(layers[layer].size() - 1);

        float y = -totalHeight * 0.5f;
        for (size_t nodeIndex : layers[layer])
        {
            result.positions[nodeIndex] = ImVec2(layerX[layer], y);
            y += NodeHeight(nodes[nodeIndex]) + options.rowGap;
        }
    }

    Measure(result, nodes);
    return result;
}
}

namespace
{
std::vector<GraphLayout::Position> CalculateLayered(const std::vector<GraphLayout::Node>& inputNodes, const std::vector<GraphLayout::Edge>& inputEdges,
                                                    const GraphLayout::Options& options)
{
    using GraphLayout::Edge;
    using GraphLayout::Node;
    using GraphLayout::Position;

    std::vector<Node> nodes;
    nodes.reserve(inputNodes.size());
    for (const Node& node : inputNodes)
        if (node.id != 0 && IsFinite(node.position) && IsFinite(node.size))
            nodes.push_back(node);
    std::stable_sort(nodes.begin(), nodes.end(), [](const Node& left, const Node& right) { return left.id < right.id; });
    nodes.erase(std::unique(nodes.begin(), nodes.end(), [](const Node& left, const Node& right) { return left.id == right.id; }), nodes.end());
    if (nodes.empty())
        return {};

    std::map<int, size_t> nodeById;
    for (size_t index = 0; index < nodes.size(); ++index)
        nodeById[nodes[index].id] = index;

    std::vector<EdgeRecord> edges;
    std::vector<std::set<size_t>> undirected(nodes.size());
    for (const Edge& edge : inputEdges)
    {
        const auto from = nodeById.find(edge.from);
        const auto to = nodeById.find(edge.to);
        if (from == nodeById.end() || to == nodeById.end() || from->second == to->second)
            continue;
        edges.push_back({ from->second, to->second, edge.sourceOrder, edge.targetOrder, edge.flow ? 4 : 1 });
        undirected[from->second].insert(to->second);
        undirected[to->second].insert(from->second);
    }
    std::stable_sort(edges.begin(), edges.end(), [&](const EdgeRecord& left, const EdgeRecord& right)
    {
        return std::tie(nodes[left.from].id, nodes[left.to].id, left.sourceOrder, left.targetOrder, left.weight) <
               std::tie(nodes[right.from].id, nodes[right.to].id, right.sourceOrder, right.targetOrder, right.weight);
    });

    std::vector<std::vector<size_t>> componentNodes;
    std::vector<bool> visited(nodes.size(), false);
    for (size_t start = 0; start < nodes.size(); ++start)
    {
        if (visited[start])
            continue;
        componentNodes.emplace_back();
        std::queue<size_t> remaining;
        remaining.push(start);
        visited[start] = true;
        while (!remaining.empty())
        {
            const size_t nodeIndex = remaining.front();
            remaining.pop();
            componentNodes.back().push_back(nodeIndex);
            for (size_t neighbor : undirected[nodeIndex])
            {
                if (!visited[neighbor])
                {
                    visited[neighbor] = true;
                    remaining.push(neighbor);
                }
            }
        }
    }

    std::vector<ComponentLayout> components;
    components.reserve(componentNodes.size());
    for (const std::vector<size_t>& component : componentNodes)
        components.push_back(LayoutComponent(component, nodes, edges, options));
    std::stable_sort(components.begin(), components.end(), [](const ComponentLayout& left, const ComponentLayout& right)
    {
        if (left.hasRoot != right.hasRoot)
            return left.hasRoot;
        if (left.oldMinimum.y != right.oldMinimum.y)
            return left.oldMinimum.y < right.oldMinimum.y;
        if (left.oldMinimum.x != right.oldMinimum.x)
            return left.oldMinimum.x < right.oldMinimum.x;
        return left.minimumId < right.minimumId;
    });

    float nextComponentY = 0.0f;
    for (ComponentLayout& component : components)
    {
        const ImVec2 translation(-component.minimum.x, nextComponentY - component.minimum.y);
        for (auto& [nodeIndex, position] : component.positions)
        {
            position.x += translation.x;
            position.y += translation.y;
        }
        component.maximum.x += translation.x;
        component.maximum.y += translation.y;
        component.minimum.x += translation.x;
        component.minimum.y += translation.y;
        nextComponentY = component.maximum.y + options.componentGap;
    }

    ImVec2 oldMinimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    ImVec2 newMinimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    for (const Node& node : nodes)
    {
        oldMinimum.x = (std::min)(oldMinimum.x, node.position.x);
        oldMinimum.y = (std::min)(oldMinimum.y, node.position.y);
    }
    for (const ComponentLayout& component : components)
    {
        newMinimum.x = (std::min)(newMinimum.x, component.minimum.x);
        newMinimum.y = (std::min)(newMinimum.y, component.minimum.y);
    }
    const ImVec2 finalTranslation(oldMinimum.x - newMinimum.x, oldMinimum.y - newMinimum.y);

    std::vector<Position> result;
    result.reserve(nodes.size());
    for (const ComponentLayout& component : components)
        for (const auto& [nodeIndex, position] : component.positions)
            result.push_back({ nodes[nodeIndex].id, ImVec2(position.x + finalTranslation.x, position.y + finalTranslation.y) });
    std::stable_sort(result.begin(), result.end(), [](const Position& left, const Position& right) { return left.id < right.id; });
    return result;
}
}

namespace
{
struct DataAssignment
{
    int ownerId = 0;
    int distance = std::numeric_limits<int>::max();
    int inputOrder = std::numeric_limits<int>::max();
};

struct DataCluster
{
    size_t ownerIndex = 0;
    std::map<size_t, ImVec2> localPositions;
    ImVec2 minimum = ImVec2(0, 0);
    ImVec2 maximum = ImVec2(0, 0);
};

void ExpandBounds(ImVec2 position, ImVec2 size, ImVec2& minimum, ImVec2& maximum)
{
    minimum.x = (std::min)(minimum.x, position.x);
    minimum.y = (std::min)(minimum.y, position.y);
    maximum.x = (std::max)(maximum.x, position.x + (std::max)(1.0f, size.x));
    maximum.y = (std::max)(maximum.y, position.y + (std::max)(1.0f, size.y));
}

DataCluster BuildDataCluster(size_t ownerIndex, const std::vector<GraphLayout::Node>& nodes, const std::vector<GraphLayout::Edge>& edges,
                             const std::map<int, size_t>& nodeById, const std::vector<DataAssignment>& assignments, const GraphLayout::Options& options)
{
    DataCluster cluster;
    cluster.ownerIndex = ownerIndex;
    cluster.localPositions[ownerIndex] = ImVec2(0, 0);
    cluster.minimum = ImVec2(0, 0);
    cluster.maximum = ImVec2(NodeWidth(nodes[ownerIndex]), NodeHeight(nodes[ownerIndex]));

    int maximumDistance = 0;
    for (size_t index = 0; index < nodes.size(); ++index)
        if (!nodes[index].execution && assignments[index].ownerId == nodes[ownerIndex].id)
            maximumDistance = (std::max)(maximumDistance, assignments[index].distance);

    float nextColumnRight = -options.dataColumnGap;
    for (int distance = 1; distance <= maximumDistance; ++distance)
    {
        std::vector<size_t> column;
        for (size_t index = 0; index < nodes.size(); ++index)
            if (!nodes[index].execution && assignments[index].ownerId == nodes[ownerIndex].id && assignments[index].distance == distance)
                column.push_back(index);
        if (column.empty())
            continue;

        std::map<size_t, float> desiredY;
        for (size_t nodeIndex : column)
        {
            float total = 0.0f;
            int count = 0;
            for (const GraphLayout::Edge& edge : edges)
            {
                if (edge.flow || edge.from != nodes[nodeIndex].id)
                    continue;
                const size_t targetIndex = nodeById.at(edge.to);
                const auto targetPosition = cluster.localPositions.find(targetIndex);
                if (targetPosition == cluster.localPositions.end())
                    continue;
                total += edge.hasPinOffsets
                    ? targetPosition->second.y + edge.targetOffsetY - edge.sourceOffsetY
                    : targetPosition->second.y + (NodeHeight(nodes[targetIndex]) - NodeHeight(nodes[nodeIndex])) * 0.5f;
                ++count;
            }
            desiredY[nodeIndex] = count > 0 ? (std::max)(0.0f, total / static_cast<float>(count))
                                            : NodeHeight(nodes[ownerIndex]) + options.dataRowGap;
        }

        std::stable_sort(column.begin(), column.end(), [&](size_t left, size_t right)
        {
            if (desiredY[left] != desiredY[right])
                return desiredY[left] < desiredY[right];
            if (assignments[left].inputOrder != assignments[right].inputOrder)
                return assignments[left].inputOrder < assignments[right].inputOrder;
            if (nodes[left].position.y != nodes[right].position.y)
                return nodes[left].position.y < nodes[right].position.y;
            return nodes[left].id < nodes[right].id;
        });

        float columnWidth = 0.0f;
        for (size_t nodeIndex : column)
            columnWidth = (std::max)(columnWidth, NodeWidth(nodes[nodeIndex]));

        const float columnLeft = nextColumnRight - columnWidth;
        float previousBottom = -std::numeric_limits<float>::max();
        for (size_t nodeIndex : column)
        {
            const float y = (std::max)(desiredY[nodeIndex], previousBottom + options.dataRowGap);
            const ImVec2 position(columnLeft, y);
            cluster.localPositions[nodeIndex] = position;
            ExpandBounds(position, nodes[nodeIndex].size, cluster.minimum, cluster.maximum);
            previousBottom = y + NodeHeight(nodes[nodeIndex]);
        }
        nextColumnRight = columnLeft - options.dataColumnGap;
    }

    const float ownerCenterY = NodeHeight(nodes[ownerIndex]) * 0.5f;
    const float verticalRadius = (std::max)(ownerCenterY - cluster.minimum.y, cluster.maximum.y - ownerCenterY);
    cluster.minimum.y = ownerCenterY - verticalRadius;
    cluster.maximum.y = ownerCenterY + verticalRadius;

    return cluster;
}

std::map<int, ImVec2> IndexPositions(const std::vector<GraphLayout::Position>& positions)
{
    std::map<int, ImVec2> result;
    for (const GraphLayout::Position& position : positions)
        result[position.id] = position.value;
    return result;
}

void AlignLinearFlowComponents(std::vector<GraphLayout::Position>& positions, const std::vector<GraphLayout::Node>& nodes,
                               const std::vector<GraphLayout::Edge>& flowEdges, const std::map<int, DataCluster>& clusters)
{
    std::map<int, size_t> positionById;
    for (size_t index = 0; index < positions.size(); ++index)
        positionById[positions[index].id] = index;

    std::map<int, std::vector<const GraphLayout::Edge*>> outgoing;
    std::map<int, std::vector<int>> undirected;
    std::map<int, int> indegree;
    for (const GraphLayout::Node& node : nodes)
    {
        if (!node.execution)
            continue;
        outgoing[node.id];
        undirected[node.id];
        indegree[node.id] = 0;
    }
    for (const GraphLayout::Edge& edge : flowEdges)
    {
        outgoing[edge.from].push_back(&edge);
        undirected[edge.from].push_back(edge.to);
        undirected[edge.to].push_back(edge.from);
        ++indegree[edge.to];
    }

    std::set<int> visitedComponents;
    for (const auto& [nodeId, neighbors] : undirected)
    {
        (void)neighbors;
        if (visitedComponents.count(nodeId))
            continue;

        std::vector<int> component;
        std::queue<int> remaining;
        remaining.push(nodeId);
        visitedComponents.insert(nodeId);
        while (!remaining.empty())
        {
            const int current = remaining.front();
            remaining.pop();
            component.push_back(current);
            for (int neighbor : undirected[current])
            {
                if (visitedComponents.insert(neighbor).second)
                    remaining.push(neighbor);
            }
        }

        const bool branches = std::any_of(component.begin(), component.end(), [&](int id)
        {
            return outgoing[id].size() > 1 || indegree[id] > 1;
        });
        if (branches || component.size() < 2)
            continue;

        const auto start = std::find_if(component.begin(), component.end(), [&](int id) { return indegree[id] == 0; });
        if (start == component.end())
            continue;

        std::set<int> visitedPath;
        int current = *start;
        visitedPath.insert(current);
        while (outgoing[current].size() == 1)
        {
            const GraphLayout::Edge& edge = *outgoing[current].front();
            if (!visitedPath.insert(edge.to).second)
                break;
            const float desiredY = positions[positionById.at(current)].value.y +
                (edge.hasPinOffsets ? edge.sourceOffsetY - edge.targetOffsetY : 0.0f);
            const float translationY = desiredY - positions[positionById.at(edge.to)].value.y;
            const DataCluster& targetCluster = clusters.at(edge.to);
            for (const auto& [clusterNodeIndex, localPosition] : targetCluster.localPositions)
            {
                (void)localPosition;
                positions[positionById.at(nodes[clusterNodeIndex].id)].value.y += translationY;
            }
            current = edge.to;
        }
    }
}
}

std::vector<GraphLayout::Position> GraphLayout::Calculate(const std::vector<Node>& inputNodes, const std::vector<Edge>& inputEdges, const Options& options)
{
    std::vector<Node> nodes;
    nodes.reserve(inputNodes.size());
    for (const Node& node : inputNodes)
        if (node.id != 0 && IsFinite(node.position) && IsFinite(node.size))
            nodes.push_back(node);
    std::stable_sort(nodes.begin(), nodes.end(), [](const Node& left, const Node& right) { return left.id < right.id; });
    nodes.erase(std::unique(nodes.begin(), nodes.end(), [](const Node& left, const Node& right) { return left.id == right.id; }), nodes.end());
    if (nodes.empty())
        return {};

    std::map<int, size_t> nodeById;
    for (size_t index = 0; index < nodes.size(); ++index)
        nodeById[nodes[index].id] = index;

    std::vector<Edge> edges;
    edges.reserve(inputEdges.size());
    for (const Edge& edge : inputEdges)
        if (nodeById.count(edge.from) && nodeById.count(edge.to) && edge.from != edge.to)
            edges.push_back(edge);

    const bool hasExecutionNodes = std::any_of(nodes.begin(), nodes.end(), [](const Node& node) { return node.execution; });
    if (!hasExecutionNodes)
        return CalculateLayered(nodes, edges, options);

    std::vector<std::vector<const Edge*>> dataEdgesFrom(nodes.size());
    for (const Edge& edge : edges)
        if (!edge.flow)
            dataEdgesFrom[nodeById.at(edge.from)].push_back(&edge);

    std::vector<DataAssignment> assignments(nodes.size());
    for (size_t start = 0; start < nodes.size(); ++start)
    {
        if (nodes[start].execution)
            continue;

        std::queue<std::pair<size_t, int>> remaining;
        std::vector<int> bestDistance(nodes.size(), std::numeric_limits<int>::max());
        remaining.push({ start, 0 });
        bestDistance[start] = 0;
        while (!remaining.empty())
        {
            const auto [current, distance] = remaining.front();
            remaining.pop();
            for (const Edge* edge : dataEdgesFrom[current])
            {
                const size_t target = nodeById.at(edge->to);
                const int candidateDistance = distance + 1;
                if (nodes[target].execution)
                {
                    const auto candidate = std::make_tuple(candidateDistance, nodes[target].id, edge->targetOrder);
                    const auto currentBest = std::make_tuple(assignments[start].distance, assignments[start].ownerId, assignments[start].inputOrder);
                    if (assignments[start].ownerId == 0 || candidate < currentBest)
                        assignments[start] = { nodes[target].id, candidateDistance, edge->targetOrder };
                }
                else if (candidateDistance < bestDistance[target])
                {
                    bestDistance[target] = candidateDistance;
                    remaining.push({ target, candidateDistance });
                }
            }
        }
    }

    std::map<int, DataCluster> clusters;
    std::vector<Node> compoundNodes;
    for (size_t index = 0; index < nodes.size(); ++index)
    {
        if (!nodes[index].execution)
            continue;
        DataCluster cluster = BuildDataCluster(index, nodes, edges, nodeById, assignments, options);
        compoundNodes.push_back({
            nodes[index].id,
            ImVec2(nodes[index].position.x + cluster.minimum.x, nodes[index].position.y + cluster.minimum.y),
            ImVec2(cluster.maximum.x - cluster.minimum.x, cluster.maximum.y - cluster.minimum.y),
            nodes[index].root,
            true,
        });
        clusters.emplace(nodes[index].id, std::move(cluster));
    }

    std::vector<Edge> flowEdges;
    for (const Edge& edge : edges)
    {
        const size_t from = nodeById.at(edge.from);
        const size_t to = nodeById.at(edge.to);
        if (edge.flow && nodes[from].execution && nodes[to].execution)
            flowEdges.push_back(edge);
    }

    const std::map<int, ImVec2> compoundPositions = IndexPositions(CalculateLayered(compoundNodes, flowEdges, options));
    std::vector<Position> result;
    result.reserve(nodes.size());
    for (const auto& [ownerId, cluster] : clusters)
    {
        const ImVec2 blockPosition = compoundPositions.at(ownerId);
        const ImVec2 ownerPosition(blockPosition.x - cluster.minimum.x, blockPosition.y - cluster.minimum.y);
        for (const auto& [nodeIndex, localPosition] : cluster.localPositions)
            result.push_back({ nodes[nodeIndex].id, ImVec2(ownerPosition.x + localPosition.x, ownerPosition.y + localPosition.y) });
    }
    AlignLinearFlowComponents(result, nodes, flowEdges, clusters);

    std::vector<Node> orphanNodes;
    std::set<int> orphanIds;
    for (size_t index = 0; index < nodes.size(); ++index)
    {
        if (!nodes[index].execution && assignments[index].ownerId == 0)
        {
            orphanNodes.push_back(nodes[index]);
            orphanIds.insert(nodes[index].id);
        }
    }
    if (!orphanNodes.empty())
    {
        std::vector<Edge> orphanEdges;
        for (const Edge& edge : edges)
            if (orphanIds.count(edge.from) && orphanIds.count(edge.to))
                orphanEdges.push_back(edge);

        Options orphanOptions = options;
        orphanOptions.columnGap = options.dataColumnGap;
        orphanOptions.rowGap = options.dataRowGap;
        std::vector<Position> orphanPositions = CalculateLayered(orphanNodes, orphanEdges, orphanOptions);

        float flowMinimumX = std::numeric_limits<float>::max();
        float flowMaximumY = std::numeric_limits<float>::lowest();
        for (const Position& position : result)
        {
            const Node& node = nodes[nodeById.at(position.id)];
            flowMinimumX = (std::min)(flowMinimumX, position.value.x);
            flowMaximumY = (std::max)(flowMaximumY, position.value.y + NodeHeight(node));
        }
        float orphanMinimumX = std::numeric_limits<float>::max();
        float orphanMinimumY = std::numeric_limits<float>::max();
        for (const Position& position : orphanPositions)
        {
            orphanMinimumX = (std::min)(orphanMinimumX, position.value.x);
            orphanMinimumY = (std::min)(orphanMinimumY, position.value.y);
        }
        const ImVec2 translation(flowMinimumX - orphanMinimumX, flowMaximumY + options.componentGap - orphanMinimumY);
        for (Position& position : orphanPositions)
        {
            position.value.x += translation.x;
            position.value.y += translation.y;
            result.push_back(position);
        }
    }

    ImVec2 oldMinimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    ImVec2 newMinimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    for (const Node& node : nodes)
    {
        oldMinimum.x = (std::min)(oldMinimum.x, node.position.x);
        oldMinimum.y = (std::min)(oldMinimum.y, node.position.y);
    }
    for (const Position& position : result)
    {
        newMinimum.x = (std::min)(newMinimum.x, position.value.x);
        newMinimum.y = (std::min)(newMinimum.y, position.value.y);
    }
    const ImVec2 finalTranslation(oldMinimum.x - newMinimum.x, oldMinimum.y - newMinimum.y);
    for (Position& position : result)
    {
        position.value.x += finalTranslation.x;
        position.value.y += finalTranslation.y;
    }
    std::stable_sort(result.begin(), result.end(), [](const Position& left, const Position& right) { return left.id < right.id; });
    return result;
}
