#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

class Uuid
{
public:
    Uuid() = default;
    explicit Uuid(std::array<uint8_t, 16> bytes) : m_bytes(bytes) {}

    static Uuid NewV4();
    static bool TryParse(std::string_view text, Uuid& result);
    static Uuid Parse(std::string_view text);

    bool IsNil() const;
    const std::array<uint8_t, 16>& Bytes() const { return m_bytes; }
    std::string ToString() const;

    bool operator==(const Uuid& other) const { return m_bytes == other.m_bytes; }
    bool operator!=(const Uuid& other) const { return !(*this == other); }
    bool operator<(const Uuid& other) const { return m_bytes < other.m_bytes; }

private:
    std::array<uint8_t, 16> m_bytes{};
};

template<typename Tag>
class DurableId
{
public:
    DurableId() = default;
    explicit DurableId(Uuid value) : m_value(std::move(value)) {}

    static DurableId New() { return DurableId(Uuid::NewV4()); }
    static DurableId Parse(std::string_view text) { return DurableId(Uuid::Parse(text)); }

    bool IsValid() const { return !m_value.IsNil(); }
    const Uuid& Value() const { return m_value; }
    std::string ToString() const { return m_value.ToString(); }

    bool operator==(const DurableId& other) const { return m_value == other.m_value; }
    bool operator!=(const DurableId& other) const { return !(*this == other); }
    bool operator<(const DurableId& other) const { return m_value < other.m_value; }

private:
    Uuid m_value;
};

struct ModuleIdTag;
struct ScriptElementUuidTag;
struct ScriptPortIdTag;
struct GraphNodeIdTag;
struct DynamicSlotIdTag;
struct PersistentLinkIdTag;

using ModuleId = DurableId<ModuleIdTag>;
using ScriptElementUuid = DurableId<ScriptElementUuidTag>;
using ScriptPortId = DurableId<ScriptPortIdTag>;
using GraphNodeId = DurableId<GraphNodeIdTag>;
using DynamicSlotId = DurableId<DynamicSlotIdTag>;
using PersistentLinkId = DurableId<PersistentLinkIdTag>;

namespace std
{
template<typename Tag>
struct hash<DurableId<Tag>>
{
    size_t operator()(const DurableId<Tag>& value) const noexcept
    {
        size_t result = 1469598103934665603ull;
        for (uint8_t byte : value.Value().Bytes())
        {
            result ^= byte;
            result *= 1099511628211ull;
        }
        return result;
    }
};
}
