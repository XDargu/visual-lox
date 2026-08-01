#include "node.h"

Pin* Node::FindOutputByName(const std::string& name)
{
    for (Pin& output : Outputs)
    {
        if (output.Name == name)
        {
            return &output;
        }
    }

    return nullptr;
}

Pin* Node::FindInputByName(const std::string& name)
{
    for (Pin& input : Inputs)
    {
        if (input.Name == name)
        {
            return &input;
        }
    }

    return nullptr;
}

void NodeUtils::BuildNode(const NodePtr& node)
{
    NormalizeDocumentation(node);

    for (Pin& input : node->Inputs)
    {
        input.Node = node;
        input.Kind = PinKind::Input;
    }

    for (Pin& output : node->Outputs)
    {
        output.Node = node;
        output.Kind = PinKind::Output;
    }

    for (Pin& input : node->UnresolvedInputs)
    {
        input.Node = node;
        input.Kind = PinKind::Input;
    }

    for (Pin& output : node->UnresolvedOutputs)
    {
        output.Node = node;
        output.Kind = PinKind::Output;
    }
}

void NodeUtils::NormalizeDocumentation(const NodePtr& node)
{
    const auto removeTerminalPeriod = [](std::string& description)
    {
        if (!description.empty() && description.back() == '.')
            description.pop_back();
    };

    removeTerminalPeriod(node->Description);
    for (Pin& input : node->Inputs)
        removeTerminalPeriod(input.Description);
    for (Pin& output : node->Outputs)
        removeTerminalPeriod(output.Description);
    for (Pin& input : node->UnresolvedInputs)
        removeTerminalPeriod(input.Description);
    for (Pin& output : node->UnresolvedOutputs)
        removeTerminalPeriod(output.Description);
}
