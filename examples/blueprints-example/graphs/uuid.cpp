#include "uuid.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
uint32_t RotateLeft(uint32_t value, int amount)
{
    return (value << amount) | (value >> (32 - amount));
}

std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t size)
{
    std::vector<uint8_t> message(data, data + size);
    const uint64_t bitLength = static_cast<uint64_t>(size) * 8;
    message.push_back(0x80);
    while (message.size() % 64 != 56)
        message.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8)
        message.push_back(static_cast<uint8_t>(bitLength >> shift));

    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xefcdab89;
    uint32_t h2 = 0x98badcfe;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xc3d2e1f0;

    for (size_t offset = 0; offset < message.size(); offset += 64)
    {
        uint32_t words[80]{};
        for (int index = 0; index < 16; ++index)
        {
            const size_t position = offset + static_cast<size_t>(index) * 4;
            words[index] = static_cast<uint32_t>(message[position]) << 24 |
                static_cast<uint32_t>(message[position + 1]) << 16 |
                static_cast<uint32_t>(message[position + 2]) << 8 |
                static_cast<uint32_t>(message[position + 3]);
        }
        for (int index = 16; index < 80; ++index)
            words[index] = RotateLeft(words[index - 3] ^ words[index - 8] ^ words[index - 14] ^ words[index - 16], 1);

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (int index = 0; index < 80; ++index)
        {
            uint32_t function;
            uint32_t constant;
            if (index < 20)
            {
                function = (b & c) | (~b & d);
                constant = 0x5a827999;
            }
            else if (index < 40)
            {
                function = b ^ c ^ d;
                constant = 0x6ed9eba1;
            }
            else if (index < 60)
            {
                function = (b & c) | (b & d) | (c & d);
                constant = 0x8f1bbcdc;
            }
            else
            {
                function = b ^ c ^ d;
                constant = 0xca62c1d6;
            }

            const uint32_t temporary = RotateLeft(a, 5) + function + e + constant + words[index];
            e = d;
            d = c;
            c = RotateLeft(b, 30);
            b = a;
            a = temporary;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<uint8_t, 20> result{};
    const uint32_t words[] = { h0, h1, h2, h3, h4 };
    for (size_t index = 0; index < 5; ++index)
    {
        result[index * 4] = static_cast<uint8_t>(words[index] >> 24);
        result[index * 4 + 1] = static_cast<uint8_t>(words[index] >> 16);
        result[index * 4 + 2] = static_cast<uint8_t>(words[index] >> 8);
        result[index * 4 + 3] = static_cast<uint8_t>(words[index]);
    }
    return result;
}

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

Uuid Uuid::V5(const Uuid& nameSpace, std::string_view name)
{
    std::vector<uint8_t> input(nameSpace.Bytes().begin(), nameSpace.Bytes().end());
    input.insert(input.end(), name.begin(), name.end());
    const std::array<uint8_t, 20> digest = Sha1(input.data(), input.size());
    std::array<uint8_t, 16> bytes{};
    std::copy_n(digest.begin(), bytes.size(), bytes.begin());
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0f) | 0x50);
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
