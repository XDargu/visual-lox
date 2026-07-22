#pragma once

#include "../script/scriptElement.h"

#include <imgui_node_editor.h>

#include <string>
#include <vector>

namespace ed = ax::NodeEditor;

enum class DiagnosticSeverity
{
    Warning,
    Error,
};

struct ValidationDiagnostic
{
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;
    std::string message;
    std::string graphName;
    ScriptElementID functionId;
    ed::NodeId nodeId = 0;
    ed::PinId pinId = 0;
    ed::LinkId linkId = 0;
};

struct ValidationReport
{
    std::vector<ValidationDiagnostic> diagnostics;

    bool HasErrors() const;
    size_t ErrorCount() const;
    size_t WarningCount() const;
    std::vector<const ValidationDiagnostic*> ForNode(ScriptElementID functionId,
                                                      ed::NodeId nodeId) const;
};

struct Script;

class ScriptValidator
{
public:
    static ValidationReport Validate(const Script& script);
};

std::string FormatDiagnostic(const ValidationDiagnostic& diagnostic);
