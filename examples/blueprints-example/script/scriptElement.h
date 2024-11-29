# pragma once

#include <string>
#include <vector>
#include <memory>

enum class ScriptElementType
{
    Function,
    Variable,
    Class,
};

struct ScriptElementID
{
    ScriptElementID() = default;
    ScriptElementID(int id) : id(id) {}
    operator int() const { return id; }

    bool IsValid() const { return id >= 0; }
    static constexpr int Invalid = -1;

    int id = ScriptElementID::Invalid;
};

struct IScriptElement
{
    virtual ~IScriptElement() {};

    ScriptElementID ID;
    ScriptElementType Type;
};