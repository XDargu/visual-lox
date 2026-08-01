#pragma once

#include "script.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

class NodeRegistry;
struct IDGenerator;

enum class SerializationDiagnosticSeverity
{
    Information,
    Warning,
    Error,
};

struct SerializationDiagnostic
{
    SerializationDiagnosticSeverity severity = SerializationDiagnosticSeverity::Information;
    std::string code;
    std::string path;
    std::string identity;
    std::string message;
};

struct SerializationResult
{
    bool success = false;
    std::string error;
    std::vector<SerializationDiagnostic> diagnostics;

    explicit operator bool() const { return success; }

    static SerializationResult Ok(std::vector<SerializationDiagnostic> diagnostics = {})
    {
        return { true, {}, std::move(diagnostics) };
    }

    static SerializationResult Fail(std::string message, std::string code = "serialization.error", std::string path = {})
    {
        SerializationDiagnostic diagnostic{ SerializationDiagnosticSeverity::Error, std::move(code), std::move(path), {}, message };
        return { false, std::move(message), { std::move(diagnostic) } };
    }
};

// Version 2 adds script-level classes, properties, methods and constructors.
// Version 3 stores declared types independently from runtime default values.
// Version 5 stores generic type properties and explicit bindings on node instances.
// Version 6 adds persisted comment boxes.
// Version 7 adds Map<K, V> declarations and ordered map values.
// Loading remains backward-compatible with version 1 and is transactional: outputScript and
// idGenerator are only replaced after the complete document has been checked.
class ScriptSerializer
{
public:
    static constexpr int FormatVersion = 7;

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
                                          std::vector<int>& pastedNodeIds,
                                          std::optional<std::pair<double, double>> pastePosition = std::nullopt);
    static SerializationResult CloneFunction(const Script& source, int functionId,
                                             const NodeRegistry& registry, Script& destination,
                                             IDGenerator& ids, int& pastedFunctionId);
    static SerializationResult CloneVariable(const Script& source, int variableId,
                                             Script& destination, IDGenerator& ids,
                                             int& pastedVariableId);
    static SerializationResult CloneFunctionVariable(const Script& source, int sourceFunctionId,
                                                     int variableId, Script& destination,
                                                     int destinationFunctionId, IDGenerator& ids,
                                                     int& pastedVariableId);
    static SerializationResult CloneFunctionPort(const Script& source, int sourceFunctionId,
                                                 int portId, bool output, Script& destination,
                                                 int destinationFunctionId, IDGenerator& ids,
                                                 int& pastedPortId);
};
