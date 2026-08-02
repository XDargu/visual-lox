#pragma once

#include "../../graphs/graph.h"
#include "../../graphs/idgeneration.h"
#include "../../graphs/node.h"

#include <string>

struct VariadicInputNode : public Node
{
    using Node::Node;

    void ConfigureDynamicInputs(const DynamicInputProps& properties) override
    {
        dynamicInputProps = properties;
        dynamicInputsConfigured = true;
    }

    void AddInput(IDGenerator& ids) override
    {
        if (!CanAddInput())
            return;

        const int inputNumber = DataInputCount();
        Inputs.emplace_back(ids.GetNextId(), DynamicInputName(inputNumber).c_str(), dynamicInputProps.type, dynamicInputProps.description);
        Inputs.back().Identity = PortIdentity::Dynamic(dynamicInputProps.familyKey, DynamicSlotId::New(), dynamicInputProps.memberKey);
        Inputs.back().LiteralValue = dynamicInputProps.defaultValue;
    }

    void RemoveInput(ed::PinId pinId) override
    {
        if (!CanRemoveInput(pinId))
            return;

        const int inputIndex = GraphUtils::FindNodeInputIdx(this, pinId);
        Inputs.erase(Inputs.begin() + inputIndex);

        int dataInputIndex = 0;
        for (Pin& input : Inputs)
        {
            if (input.Type == PinType::Flow)
                continue;

            input.Name = DynamicInputName(dataInputIndex);
            ++dataInputIndex;
        }
    }

    bool CanRemoveInput(ed::PinId pinId) const override
    {
        const int inputIndex = GraphUtils::FindNodeInputIdx(this, pinId);
        return dynamicInputsConfigured && inputIndex >= 0 && Inputs[inputIndex].Type != PinType::Flow && DataInputCount() > dynamicInputProps.minInputs;
    }

    bool CanAddInput() const override
    {
        return dynamicInputsConfigured && DataInputCount() < dynamicInputProps.maxInputs;
    }

    TypeRef DynamicInputType() const override
    {
        return dynamicInputsConfigured ? dynamicInputProps.type : TypeRef(PinType::Any);
    }

    bool IsValidDynamicInputCount(size_t inputCount) const override
    {
        return dynamicInputsConfigured && inputCount >= static_cast<size_t>(dynamicInputProps.minInputs) && inputCount <= static_cast<size_t>(dynamicInputProps.maxInputs);
    }

protected:
    int DataInputCount() const
    {
        int count = 0;
        for (const Pin& input : Inputs)
            if (input.Type != PinType::Flow)
                ++count;
        return count;
    }

    virtual std::string DynamicInputName(int inputIndex) const
    {
        return std::string(1, static_cast<char>('A' + inputIndex));
    }

private:
    DynamicInputProps dynamicInputProps;
    bool dynamicInputsConfigured = false;
};
