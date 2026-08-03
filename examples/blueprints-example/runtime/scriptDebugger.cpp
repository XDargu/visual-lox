#include "scriptDebugger.h"

#include <Value.h>

#include <algorithm>
#include <utility>

namespace
{
std::string PortIdentityKey(const PortIdentity& port)
{
    switch (port.kind)
    {
    case PortIdentityKind::Fixed: return "fixed:" + port.key;
    case PortIdentityKind::Script: return "script:" + port.scriptPortId.ToString();
    case PortIdentityKind::Dynamic: return "dynamic:" + port.family + ":" + port.dynamicSlot.ToString() + ":" + port.member;
    case PortIdentityKind::None: return "none";
    }
    return "none";
}
}

uint32_t ScriptDebugInfo::AddProbe(ScriptDebugProbe probe)
{
    const uint32_t id = static_cast<uint32_t>(m_probes.size());
    m_probes.push_back(std::move(probe));
    return id;
}

const ScriptDebugProbe* ScriptDebugInfo::FindProbe(uint32_t probeId) const
{
    return probeId < m_probes.size() ? &m_probes[probeId] : nullptr;
}

std::string ScriptDebugInfo::NodeKey(ModuleId moduleId, ScriptElementUuid functionId, GraphNodeId nodeId)
{
    return "node:" + moduleId.ToString() + ":" + functionId.ToString() + ":" + nodeId.ToString();
}

std::string ScriptDebugInfo::PortKey(ModuleId moduleId, ScriptElementUuid functionId, GraphNodeId nodeId, const PortIdentity& port, PinKind kind)
{
    return "port:" + moduleId.ToString() + ":" + functionId.ToString() + ":" + nodeId.ToString() + ":" + (kind == PinKind::Input ? "in:" : "out:") + PortIdentityKey(port);
}

std::string ScriptDebugInfo::VariableKey(ModuleId moduleId, ScriptElementUuid functionId, ScriptElementUuid variableId)
{
    (void)functionId;
    return "variable:" + moduleId.ToString() + ":" + variableId.ToString();
}

void ScriptDebugger::SetDebugInfo(std::shared_ptr<const ScriptDebugInfo> debugInfo)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_debugInfo = std::move(debugInfo);
    m_values.clear();
    m_lastFlowNodes.clear();
    m_flowEdges.clear();
    m_pause = {};
    m_resumeMode = ScriptDebugResumeMode::Continue;
    m_paused = false;
}

std::shared_ptr<const ScriptDebugInfo> ScriptDebugger::GetDebugInfo() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_debugInfo;
}

bool ScriptDebugger::HasBreakpoint(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_breakpoints.count(key) != 0;
}

void ScriptDebugger::SetBreakpoint(const std::string& key, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (enabled)
        m_breakpoints.insert(key);
    else
        m_breakpoints.erase(key);

    SetWantsBreakpoints(!m_breakpoints.empty());
    SetWantsValues(!m_breakpoints.empty() || !m_watches.empty());
}

bool ScriptDebugger::IsWatching(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    return m_watches.count(key) != 0;
}

void ScriptDebugger::SetWatching(const std::string& key, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (enabled)
        m_watches.insert(key);
    else
        m_watches.erase(key);

    SetWantsValues(!m_breakpoints.empty() || !m_watches.empty());
}

void ScriptDebugger::ClearRuntimeState()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_values.clear();
    m_lastFlowNodes.clear();
    m_flowEdges.clear();
    m_pause = {};
    m_resumeMode = ScriptDebugResumeMode::Continue;
    m_paused = false;
}

void ScriptDebugger::LeavePause()
{
    Resume(ScriptDebugResumeMode::Continue);
}

void ScriptDebugger::Resume(ScriptDebugResumeMode mode)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_resumeMode = mode;
    m_stepCallDepth = m_pause.callStack.size();
    m_pause = {};
    m_paused = false;
}

bool ScriptDebugger::IsPaused() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_paused;
}

ScriptDebugPause ScriptDebugger::GetPause() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pause;
}

std::vector<ScriptDebugValue> ScriptDebugger::GetWatchedValues() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<ScriptDebugValue> values;
    values.reserve(m_values.size());

    for (const auto& [key, value] : m_values)
    {
        if (value.probe && m_watches.count(key) != 0)
        {
            values.push_back({ *value.probe, valueAsStr(value.value) });
        }
    }

    return values;
}

bool ScriptDebugger::HasValue(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    return m_values.count(key) != 0;
}

bool ScriptDebugger::TryGetValue(const std::string& key, std::string& value) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto found = m_values.find(key);
    if (found == m_values.end())
        return false;

    value = valueAsStr(found->second.value);

    return true;
}

bool ScriptDebugger::IsFlowEdgeActive(ScriptElementUuid functionId, GraphNodeId fromNodeId, GraphNodeId toNodeId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    return std::any_of(m_flowEdges.begin(), m_flowEdges.end(), [&](const ScriptDebugFlowEdge& edge)
    {
        return edge.functionId == functionId && edge.fromNodeId == fromNodeId && edge.toNodeId == toNodeId;
    });
}

bool ScriptDebugger::TryGetLastFlowNode(ScriptElementUuid functionId, GraphNodeId& nodeId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto found = m_lastFlowNodes.find(functionId.ToString());
    if (found == m_lastFlowNodes.end())
        return false;

    nodeId = found->second;

    return true;
}

std::vector<std::string> ScriptDebugger::GetBreakpoints() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    return { m_breakpoints.begin(), m_breakpoints.end() };
}

bool ScriptDebugger::OnBreakpoint(uint32_t probeId, const VM& vm)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_paused)
        return false;

    const ScriptDebugProbe* probe = m_debugInfo ? m_debugInfo->FindProbe(probeId) : nullptr;
    if (!probe)
        return false;

    if (probe->flowNode && probe->functionId.IsValid())
    {
        const std::string functionKey = probe->functionId.ToString();
        const auto previous = m_lastFlowNodes.find(functionKey);

        if (previous != m_lastFlowNodes.end() && previous->second != probe->nodeId)
        {
            const bool duplicate = std::any_of(m_flowEdges.begin(), m_flowEdges.end(), [&](const ScriptDebugFlowEdge& edge)
            {
                return edge.functionId == probe->functionId && edge.fromNodeId == previous->second && edge.toNodeId == probe->nodeId;
            });

            if (!duplicate)
            {
                m_flowEdges.push_back({ probe->functionId, previous->second, probe->nodeId });
            }
        }
        m_lastFlowNodes[functionKey] = probe->nodeId;
    }

    const size_t callDepth = vm.getFrameCount();
    const bool explicitBreakpoint = m_breakpoints.count(probe->key) != 0;
    const bool stepPause = m_resumeMode == ScriptDebugResumeMode::StepInto ||
        (m_resumeMode == ScriptDebugResumeMode::StepOver && callDepth <= m_stepCallDepth) ||
        (m_resumeMode == ScriptDebugResumeMode::StepOut && callDepth < m_stepCallDepth);

    if (!explicitBreakpoint && !stepPause)
        return false;

    m_pause.probe = *probe;
    m_pause.callStack = vm.getDebugCallStack();
    m_pause.sequence = ++m_pauseSequence;
    m_resumeMode = ScriptDebugResumeMode::Continue;
    m_paused = true;

    return true;
}

void ScriptDebugger::OnValue(uint32_t probeId, const Value& value)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const ScriptDebugProbe* probe = m_debugInfo ? m_debugInfo->FindProbe(probeId) : nullptr;
    if (!probe)
        return;

    m_values[probe->key] = { probe, value };
}

void ScriptDebugger::MarkRoots(VM& vm)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [key, stored] : m_values)
        vm.markValue(stored.value);
}
