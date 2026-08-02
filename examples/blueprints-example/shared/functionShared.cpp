
# pragma once

#include "functionShared.h"

#include "../graphs/node.h"
#include "../graphs/graph.h"
#include "../graphs/idgeneration.h"
#include "../graphs/graphCompiler.h"

#include "../utilities/utils.h"

#include "../script/script.h"

#include <Compiler.h>

#include <algorithm>


struct FunctionNode : public Node
{
    FunctionNode(int id, const char* name, const BasicFunctionDefPtr& pFunctionDef, ScriptElementID funcID)
        : Node(id, name, ImColor(255, 128, 128))
        , pFunctionDef(pFunctionDef)
    {
        refId = funcID;
        Category = NodeCategory::Function;
    }

    virtual void Compile(CompilerContext& compilerCtx, const Graph& graph, CompilationStage stage, int portIdx) const override
    {
        switch (stage)
        {
        case CompilationStage::BeginInputs:
        {
            if (!GraphUtils::IsNodeImplicit(this))
                CompileInputs(compilerCtx, graph);
        }
        break;
        case CompilationStage::PullOutput:
        {
            if (GraphUtils::IsNodeImplicit(this))
                CompileInputs(compilerCtx, graph);
        }
        break;
        }
    }

    void CompileInputs(CompilerContext& compilerCtx, const Graph& graph) const
    {
        if (pFunctionDef)
        {
            Compiler& compiler = compilerCtx.compiler;

            // Load named variable (native func)
            compiler.namedVariable(Token(TokenType::STRING, Name.c_str(), Name.length(), 10), false);

            const bool hasDynamicInputs =
                HasFlag(DefinitionFlags, NodeDefinitionFlags::DynamicInputs);
            int argCount = 0;

            if (hasDynamicInputs)
            {
                // Dynamic arguments are passed as a single list. Build it
                // incrementally so the number of inputs is not byte-limited.
                compiler.emitByte(OpByte(OpCode::OP_BUILD_LIST));
            }

            for (int i = 0; i < Inputs.size(); ++i)
            {
                if (Inputs[i].Type != PinType::Flow)
                {
                    GraphCompiler::CompileInput(compilerCtx, graph, Inputs[i], InputValues[i]);
                    if (hasDynamicInputs)
                        compiler.emitByte(OpByte(OpCode::OP_APPEND_LIST));
                    else
                        argCount++;
                }
            }

            if (hasDynamicInputs)
            {
                // We will only call the function with the list!
                argCount = 1;
            }

            compiler.emitBytes(OpByte(OpCode::OP_CALL), argCount);

            const size_t dataOutputStart = GraphUtils::IsNodeImplicit(this) ? 0 : 1;
            GraphCompiler::CompileCallResult(
                compilerCtx, graph, Outputs, dataOutputStart);
        }
    }

    virtual void Refresh(const Script& script, IDGenerator& IDGenerator) override
    {
        InstanceFlags = ClearFlag(InstanceFlags, NodeInstanceFlags::Error);

        RefreshDefinition(script);

        if (!pFunctionDef)
        {
            InstanceFlags |= NodeInstanceFlags::Error;
            Error = "Missing function with ID: " + std::to_string(refId);
            return;
        }

        // Basic info
        Name = pFunctionDef->name;
        Description = pFunctionDef->description;
        DefinitionFlags = pFunctionDef->flags;
        GenericTypeProperties = pFunctionDef->genericTypeProperties;

        const bool expressionOnly =
            HasFlag(DefinitionFlags, NodeDefinitionFlags::ReadOnly) ||
            HasFlag(DefinitionFlags, NodeDefinitionFlags::Pure);
        if (expressionOnly)
        {
            for (size_t index = Inputs.size(); index-- > 0;)
            {
                if (Inputs[index].Type != PinType::Flow)
                    continue;
                Inputs.erase(Inputs.begin() + index);
                if (index < InputValues.size())
                    InputValues.erase(InputValues.begin() + index);
            }
            stl::erase_if(Outputs,
                [](const Pin& output) { return output.Type == PinType::Flow; });
        }
        else
        {
            if (std::none_of(Inputs.begin(), Inputs.end(),
                    [](const Pin& input) { return input.Type == PinType::Flow; }))
            {
                Inputs.insert(Inputs.begin(),
                    Pin(IDGenerator.GetNextId(), "", PinType::Flow,
                        "Executes this function."));
                InputValues.insert(InputValues.begin(), Value());
            }
            if (std::none_of(Outputs.begin(), Outputs.end(),
                    [](const Pin& output) { return output.Type == PinType::Flow; }))
            {
                Outputs.insert(Outputs.begin(),
                    Pin(IDGenerator.GetNextId(), "", PinType::Flow,
                        "Continues after the function returns."));
            }
        }

        const bool scriptDefinition = refId.IsValid();
        const auto identityOf = [&](const BasicFunctionDef::Input& port)
        {
            return scriptDefinition ? PortIdentity::Script(port.id, port.persistentId) : PortIdentity::Fixed(port.key);
        };
        const auto matches = [&](const Pin& pin, const BasicFunctionDef::Input& port)
        {
            const PortIdentity identity = identityOf(port);
            return pin.Identity.kind != PortIdentityKind::None ? PortIdentitiesMatch(pin.Identity, identity) : pin.Name == port.name;
        };

        std::vector<std::pair<Pin, Value>> savedInputs;
        for (size_t index = 0; index < Inputs.size(); ++index)
            if (Inputs[index].Type != PinType::Flow)
                savedInputs.emplace_back(Inputs[index], InputValues[index]);
        for (size_t index = 0; index < UnresolvedInputs.size(); ++index)
            savedInputs.emplace_back(UnresolvedInputs[index], UnresolvedInputValues[index]);

        std::vector<InputPin> refreshedInputs;
        std::vector<Value> refreshedValues;
        if (!expressionOnly)
        {
            const auto flow = std::find_if(Inputs.begin(), Inputs.end(), [](const Pin& input) { return input.Type == PinType::Flow; });
            Pin pin = flow != Inputs.end() ? *flow : Pin(IDGenerator.GetNextId(), "", PinType::Flow, "Executes this function.");
            pin.Identity = PortIdentity::Fixed("execute");
            refreshedInputs.push_back(std::move(pin));
            refreshedValues.emplace_back();
        }

        for (const BasicFunctionDef::Input& input : pFunctionDef->inputs)
        {
            const auto existing = std::find_if(savedInputs.begin(), savedInputs.end(), [&](const auto& item) { return matches(item.first, input); });
            if (existing != savedInputs.end())
            {
                Pin pin = std::move(existing->first);
                Value value = existing->second;
                pin.Name = input.name;
                pin.Type = pin.DeclaredType = input.type;
                pin.Description = input.description;
                pin.Identity = identityOf(input);
                refreshedInputs.push_back(std::move(pin));
                refreshedValues.push_back(value);
                savedInputs.erase(existing);
            }
            else
            {
                Pin pin(IDGenerator.GetNextId(), input.name.c_str(), input.type, input.description);
                pin.Identity = identityOf(input);
                refreshedInputs.push_back(std::move(pin));
                refreshedValues.push_back(input.value);
            }
        }

        Inputs = std::move(refreshedInputs);
        InputValues = std::move(refreshedValues);
        UnresolvedInputs.clear();
        UnresolvedInputValues.clear();
        for (auto& [pin, value] : savedInputs)
        {
            UnresolvedInputs.push_back(std::move(pin));
            UnresolvedInputValues.push_back(value);
        }

        std::vector<Pin> savedOutputs;
        for (Pin& output : Outputs)
            if (output.Type != PinType::Flow)
                savedOutputs.push_back(output);
        savedOutputs.insert(savedOutputs.end(), UnresolvedOutputs.begin(), UnresolvedOutputs.end());

        std::vector<Pin> refreshedOutputs;
        if (!expressionOnly)
        {
            const auto flow = std::find_if(Outputs.begin(), Outputs.end(), [](const Pin& output) { return output.Type == PinType::Flow; });
            Pin pin = flow != Outputs.end() ? *flow : Pin(IDGenerator.GetNextId(), "", PinType::Flow, "Continues after the function returns.");
            pin.Identity = PortIdentity::Fixed("then");
            refreshedOutputs.push_back(std::move(pin));
        }

        for (const BasicFunctionDef::Input& output : pFunctionDef->outputs)
        {
            const auto existing = std::find_if(savedOutputs.begin(), savedOutputs.end(), [&](const Pin& pin) { return matches(pin, output); });
            if (existing != savedOutputs.end())
            {
                Pin pin = *existing;
                pin.Name = output.name;
                pin.Type = pin.DeclaredType = output.type;
                pin.Description = output.description;
                pin.Identity = identityOf(output);
                refreshedOutputs.push_back(std::move(pin));
                savedOutputs.erase(existing);
            }
            else
            {
                Pin pin(IDGenerator.GetNextId(), output.name.c_str(), output.type, output.description);
                pin.Identity = identityOf(output);
                refreshedOutputs.push_back(std::move(pin));
            }
        }

        Outputs = std::move(refreshedOutputs);
        UnresolvedOutputs = std::move(savedOutputs);
    }

    void RefreshDefinition(const Script& script)
    {
        const bool isNative = !refId.IsValid() && !refPersistentId.IsValid();

        if (!isNative)
        {
            ScriptFunctionPtr function = refPersistentId.IsValid()
                ? ScriptUtils::FindFunctionByPersistentId(script, refPersistentId)
                : ScriptUtils::FindFunctionById(script, refId);
            if (function)
            {
                refId = function->ID;
                refPersistentId = function->PersistentId;
                pFunctionDef = function->functionDef;
            }
            else
            {
                pFunctionDef = nullptr;
            }
        }
    }

    virtual void AddInput(IDGenerator& IDGenerator) override
    {
        if (pFunctionDef)
        {
            Inputs.emplace_back(IDGenerator.GetNextId(),
                GetInputName(Inputs.size()).c_str(),
                pFunctionDef->dynamicInputProps.type,
                pFunctionDef->dynamicInputProps.description);
            Inputs.back().Identity = PortIdentity::Dynamic(pFunctionDef->dynamicInputProps.familyKey, DynamicSlotId::New(), pFunctionDef->dynamicInputProps.memberKey);
            InputValues.emplace_back(pFunctionDef->dynamicInputProps.defaultValue);
        }
    };

    virtual void RemoveInput(ed::PinId pinId) override
    {
        const int inputIdx = GraphUtils::FindNodeInputIdx(this, pinId);
        if (inputIdx != -1)
        {
            Inputs.erase(Inputs.begin() + inputIdx);
            InputValues.erase(InputValues.begin() + inputIdx);

            // Rename inputs!
            for (int i = 1; i < Inputs.size(); ++i)
            {
                Inputs[i].Name = GetInputName(i);
            }
        }
    };

    // TODO: Should be defined in the function def
    virtual bool CanRemoveInput(ed::PinId pinId) const override
    {
        return pFunctionDef && Inputs.size() > pFunctionDef->dynamicInputProps.minInputs;
    };
    virtual bool CanAddInput() const override
    {
        return pFunctionDef && Inputs.size() < pFunctionDef->dynamicInputProps.maxInputs;
    };
    virtual TypeRef DynamicInputType() const override
    {
        return pFunctionDef
            ? pFunctionDef->dynamicInputProps.type : TypeRef(PinType::Any);
    }

    static std::string GetInputName(int inputIdx) { return std::string(1, char(65 + inputIdx)); }

    BasicFunctionDefPtr pFunctionDef;
};

NodePtr BuildFunctionNode(IDGenerator& IDGenerator, const BasicFunctionDefPtr& pFunctionDef,
                          ScriptElementID funcID)
{
    NodePtr node = std::make_shared<FunctionNode>(IDGenerator.GetNextId(),
        pFunctionDef ? pFunctionDef->name.c_str() : "", pFunctionDef, funcID);
    node->SerializationType = "function.call";
    node->DefinitionId = funcID.IsValid()
        ? "vlox.script.function.call"
        : pFunctionDef ? pFunctionDef->id : std::string();
    node->DefinitionRevision = pFunctionDef ? pFunctionDef->revision : 1;
    if (funcID.IsValid() && pFunctionDef) node->refPersistentId = pFunctionDef->scriptId;

    // The serialized pins are restored after construction for a dangling reference.
    if (!pFunctionDef)
        return node;

    node->Description = pFunctionDef->description;
    node->GenericTypeProperties = pFunctionDef->genericTypeProperties;

    const bool expressionOnly =
        HasFlag(pFunctionDef->flags, NodeDefinitionFlags::ReadOnly) ||
        HasFlag(pFunctionDef->flags, NodeDefinitionFlags::Pure);
    if (!expressionOnly)
    {
        node->Inputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow,
            "Executes this function.");
        node->Inputs.back().Identity = PortIdentity::Fixed("execute");
        node->InputValues.emplace_back(Value());

        node->Outputs.emplace_back(IDGenerator.GetNextId(), "", PinType::Flow,
            "Continues after the function returns.");
        node->Outputs.back().Identity = PortIdentity::Fixed("then");
    }

    if (!HasFlag(pFunctionDef->flags, NodeDefinitionFlags::DynamicInputs))
    {
        for (const BasicFunctionDef::Input& input : pFunctionDef->inputs)
        {
            node->Inputs.emplace_back(IDGenerator.GetNextId(), input.name.c_str(), input.type,
                input.description);
            node->Inputs.back().Identity = funcID.IsValid() ? PortIdentity::Script(input.id, input.persistentId) : PortIdentity::Fixed(input.key);
            node->InputValues.emplace_back(input.value);
        }
    }

    for (const BasicFunctionDef::Input& output : pFunctionDef->outputs)
    {
        node->Outputs.emplace_back(IDGenerator.GetNextId(), output.name.c_str(), output.type,
            output.description);
        node->Outputs.back().Identity = funcID.IsValid() ? PortIdentity::Script(output.id, output.persistentId) : PortIdentity::Fixed(output.key);
    }

    node->DefinitionFlags = pFunctionDef->flags;

    return node;
}

NodePtr BasicFunctionDef::MakeNode(IDGenerator& IDGenerator, ScriptElementID funcID)
{
    return BuildFunctionNode(IDGenerator, shared_from_this(), funcID);
}

BasicFunctionDef::Input* BasicFunctionDef::FindOutputByName(const std::string& name)
{
    for (Input& input : outputs)
    {
        if (input.name == name)
        {
            return &input;
        }
    }

    return nullptr;
}

BasicFunctionDef::Input* BasicFunctionDef::FindInputByName(const std::string& name)
{
    for (Input& input : inputs)
    {
        if (input.name == name)
        {
            return &input;
        }
    }

    return nullptr;
}

BasicFunctionDef::Input* BasicFunctionDef::FindOutputByID(const int inputId)
{
    for (Input& input : outputs)
    {
        if (input.id == inputId)
        {
            return &input;
        }
    }

    return nullptr;
}

BasicFunctionDef::Input* BasicFunctionDef::FindInputByID(const int inputId)
{
    for (Input& input : inputs)
    {
        if (input.id == inputId)
        {
            return &input;
        }
    }

    return nullptr;
}
