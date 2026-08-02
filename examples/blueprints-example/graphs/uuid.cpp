#include "uuid.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>

namespace
{
int HexValue(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}
}

Uuid Uuid::NewV4()
{
    static std::mutex mutex;
    static std::mt19937_64 random(std::random_device{}());
    std::lock_guard<std::mutex> lock(mutex);

    std::array<uint8_t, 16> bytes{};
    for (size_t offset = 0; offset < bytes.size(); offset += 8)
    {
        const uint64_t value = random();
        for (size_t index = 0; index < 8; ++index)
            bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
    }
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3f) | 0x80);
    return Uuid(bytes);
}

bool Uuid::TryParse(std::string_view text, Uuid& result)
{
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
        return false;

    std::array<uint8_t, 16> bytes{};
    size_t byteIndex = 0;
    for (size_t index = 0; index < text.size();)
    {
        if (text[index] == '-')
        {
            ++index;
            continue;
        }
        if (index + 1 >= text.size() || byteIndex >= bytes.size())
            return false;
        const int high = HexValue(text[index]);
        const int low = HexValue(text[index + 1]);
        if (high < 0 || low < 0)
            return false;
        bytes[byteIndex++] = static_cast<uint8_t>(high << 4 | low);
        index += 2;
    }
    if (byteIndex != bytes.size())
        return false;
    result = Uuid(bytes);
    return true;
}

Uuid Uuid::Parse(std::string_view text)
{
    Uuid result;
    if (!TryParse(text, result))
        throw std::invalid_argument("Invalid UUID '" + std::string(text) + "'.");
    return result;
}

bool Uuid::IsNil() const
{
    return std::all_of(m_bytes.begin(), m_bytes.end(), [](uint8_t value) { return value == 0; });
}

std::string Uuid::ToString() const
{
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (size_t index = 0; index < m_bytes.size(); ++index)
    {
        if (index == 4 || index == 6 || index == 8 || index == 10)
            output << '-';
        output << std::setw(2) << static_cast<unsigned>(m_bytes[index]);
    }
    return output.str();
}
