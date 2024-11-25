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
