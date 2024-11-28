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

using ScriptElementID = int;

struct IScriptElement
{
    virtual ~IScriptElement() {};

    ScriptElementID ID;
    ScriptElementType Type;
};