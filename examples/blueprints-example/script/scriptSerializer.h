#pragma once

#include "script.h"

#include <string>
#include <utility>

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
};
