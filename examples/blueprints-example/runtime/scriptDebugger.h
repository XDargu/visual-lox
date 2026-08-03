#pragma once

#include "../graphs/node.h"
#include "../script/function.h"

#include <Vm.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>

enum class ScriptDebugProbeKind
{
    Node,
    PortInput,
    PortOutput,
    Variable,
};

enum class ScriptDebugResumeMode
{
    Continue,
    StepInto,
    StepOver,
    StepOut,
};

struct ScriptDebugProbe
{
    ScriptDebugProbeKind kind = ScriptDebugProbeKind::Node;
    std::string key;
    std::string label;
    ModuleId moduleId;
    ScriptElementUuid functionId;
    GraphNodeId nodeId;
    ScriptElementUuid variableId;
    PortIdentity port;
    int functionRuntimeId = ScriptElementID::Invalid;
    int nodeRuntimeId = -1;
    int pinRuntimeId = -1;
    bool flowNode = false;
};

struct FunctionDebugInfo
{
    std::string displayName;
    ScriptElementUuid persistentId;
    ScriptFunctionPtr pScriptFunction;
};

class ScriptDebugInfo
{
public:
    uint32_t AddProbe(ScriptDebugProbe probe);
    const ScriptDebugProbe* FindProbe(uint32_t probeId) const;
    const std::vector<ScriptDebugProbe>& Probes() const { return m_probes; }

    static std::string NodeKey(ModuleId moduleId, ScriptElementUuid functionId, GraphNodeId nodeId);
    static std::string PortKey(ModuleId moduleId, ScriptElementUuid functionId, GraphNodeId nodeId, const PortIdentity& port, PinKind kind);
    static std::string VariableKey(ModuleId moduleId, ScriptElementUuid functionId, ScriptElementUuid variableId);

    void AddFunction(const ObjFunction* pFunction, const std::string& displayName, const ScriptElementUuid& persistentId, const ScriptFunctionPtr& pScriptFunction);
    const FunctionDebugInfo* FindFunction(const ObjFunction* pFunction) const;

private:
    std::vector<ScriptDebugProbe> m_probes;
    std::unordered_map<const ObjFunction*, FunctionDebugInfo> m_functions;
};

struct ScriptDebugValue
{
    ScriptDebugProbe probe;
    std::string value;
};

struct ScriptDebugPause
{
    ScriptDebugProbe probe;
    std::vector<VmDebugCallFrame> callStack;
    uint64_t sequence = 0;
};

struct ScriptDebugFlowEdge
{
    ScriptElementUuid functionId;
    GraphNodeId fromNodeId;
    GraphNodeId toNodeId;
};

class ScriptDebugger final : public VmDebugHandler
{
public:
    void SetDebugInfo(std::shared_ptr<const ScriptDebugInfo> debugInfo);
    std::shared_ptr<const ScriptDebugInfo> GetDebugInfo() const;

    bool HasBreakpoint(const std::string& key) const;
    void SetBreakpoint(const std::string& key, bool enabled);
    bool IsWatching(const std::string& key) const;
    void SetWatching(const std::string& key, bool enabled);
    void ClearRuntimeState();
    void LeavePause();
    void Resume(ScriptDebugResumeMode mode);

    bool IsPaused() const;
    ScriptDebugPause GetPause() const;
    std::vector<ScriptDebugValue> GetWatchedValues() const;
    std::vector<std::string> GetBreakpoints() const;
    bool HasValue(const std::string& key) const;
    bool TryGetValue(const std::string& key, std::string& value) const;
    bool IsFlowEdgeActive(ScriptElementUuid functionId, GraphNodeId fromNodeId, GraphNodeId toNodeId) const;
    bool TryGetLastFlowNode(ScriptElementUuid functionId, GraphNodeId& nodeId) const;

    bool OnBreakpoint(uint32_t probeId, const VM& vm) override;
    void OnValue(uint32_t probeId, const Value& value) override;
    void MarkRoots(VM& vm);

private:
    struct StoredValue
    {
        const ScriptDebugProbe* probe = nullptr;
        Value value;
    };

    mutable std::mutex m_mutex;
    std::shared_ptr<const ScriptDebugInfo> m_debugInfo;
    std::set<std::string> m_breakpoints;
    std::set<std::string> m_watches;
    std::map<std::string, StoredValue> m_values;
    std::map<std::string, GraphNodeId> m_lastFlowNodes;
    std::vector<ScriptDebugFlowEdge> m_flowEdges;
    ScriptDebugPause m_pause;
    ScriptDebugResumeMode m_resumeMode = ScriptDebugResumeMode::Continue;
    size_t m_stepCallDepth = 0;
    uint64_t m_pauseSequence = 0;
    bool m_paused = false;
};
