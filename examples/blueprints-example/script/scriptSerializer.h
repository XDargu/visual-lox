#pragma once

#include "script.h"

#include <string>
#include <utility>
#include <vector>

class NodeRegistry;
struct IDGenerator;

struct SerializationResult
{
    bool success = false;
    std::string error;

    explicit operator bool() const { return success; }

    static SerializationResult Ok() { return { true, {} }; }
    static SerializationResult Fail(std::string message) { return { false, std::move(message) }; }
};

// Version 1 of the .vlox format. Loading is transactional: outputScript and
// idGenerator are only replaced after the complete document has been checked.
class ScriptSerializer
{
public:
    static constexpr int FormatVersion = 1;

    static SerializationResult Save(const Script& script, const std::string& path);
    static SerializationResult Load(const std::string& path, const NodeRegistry& registry,
                                    Script& outputScript, IDGenerator& idGenerator);
    static SerializationResult SerializeToString(const Script& script, std::string& output);
    static SerializationResult DeserializeFromString(const std::string& data,
                                                      const NodeRegistry& registry,
                                                      Script& outputScript,
                                                      IDGenerator& idGenerator);

    // Fragment import helpers used by copy/paste. All persisted IDs owned by
    // the fragment are regenerated; references within that fragment are patched.
    static SerializationResult CloneNodes(const Script& source, int sourceFunctionId,
                                          const std::vector<int>& nodeIds,
                                          const NodeRegistry& registry, Script& destination,
                                          int destinationFunctionId, IDGenerator& ids,
                                          std::vector<int>& pastedNodeIds);
    static SerializationResult CloneFunction(const Script& source, int functionId,
                                             const NodeRegistry& registry, Script& destination,
                                             IDGenerator& ids, int& pastedFunctionId);
    static SerializationResult CloneVariable(const Script& source, int variableId,
                                             Script& destination, IDGenerator& ids,
                                             int& pastedVariableId);
    static SerializationResult CloneFunctionPort(const Script& source, int sourceFunctionId,
                                                 int portId, bool output, Script& destination,
                                                 int destinationFunctionId, IDGenerator& ids,
                                                 int& pastedPortId);
};
