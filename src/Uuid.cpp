#include <libecs-cpp/Uuid.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
    std::mt19937_64 generatorCreate()
    {
        std::array<std::uint32_t, 12> words{};

        try
        {
            std::random_device device;
            for(std::size_t i = 0; i < 8; i++)
            {
                words[i] = static_cast<std::uint32_t>(device());
            }
        }
        catch(...)
        {
            // No platform random source: fall back to the address of a local
            // variable, mixed with the index, so the words still differ.
            int local = 0;
            auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&local));
            for(std::size_t i = 0; i < 8; i++)
            {
                std::uint64_t mixed = (address + i) * 0x9E3779B97F4A7C15ULL;
                words[i] = static_cast<std::uint32_t>(mixed >> 32) ^ static_cast<std::uint32_t>(mixed);
            }
        }

        auto ticks = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        auto thread = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        words[8] = static_cast<std::uint32_t>(ticks);
        words[9] = static_cast<std::uint32_t>(ticks >> 32);
        words[10] = static_cast<std::uint32_t>(thread);
        words[11] = static_cast<std::uint32_t>(thread >> 32);

        std::seed_seq seq(words.begin(), words.end());
        return std::mt19937_64(seq);
    }

    std::mt19937_64 &generatorGet()
    {
        thread_local std::mt19937_64 generator = generatorCreate();
        return generator;
    }

    constexpr std::array<std::array<char, 2>, 256> makeHexPairs()
    {
        constexpr char digits[] = "0123456789abcdef";
        std::array<std::array<char, 2>, 256> pairs{};
        for(std::size_t i = 0; i < 256; i++)
        {
            pairs[i][0] = digits[i >> 4];
            pairs[i][1] = digits[i & 0xF];
        }
        return pairs;
    }

    constexpr std::array<std::array<char, 2>, 256> hexPairs = makeHexPairs();

    constexpr std::array<std::size_t, 8> highOffsets = {0, 2, 4, 6, 9, 11, 14, 16};
    constexpr std::array<std::size_t, 8> lowOffsets = {19, 21, 24, 26, 28, 30, 32, 34};

    void writeHalf(char *out, std::uint64_t half, const std::array<std::size_t, 8> &offsets)
    {
        for(std::size_t i = 0; i < 8; i++)
        {
            auto byte = static_cast<std::size_t>((half >> (56 - 8 * i)) & 0xFF);
            out[offsets[i]] = hexPairs[byte][0];
            out[offsets[i] + 1] = hexPairs[byte][1];
        }
    }

    std::string quote(const std::string &text)
    {
        constexpr std::size_t limit = 64;
        constexpr char digits[] = "0123456789abcdef";

        std::string quoted = "\"";
        std::size_t count = text.size() < limit ? text.size() : limit;
        for(std::size_t i = 0; i < count; i++)
        {
            auto c = static_cast<unsigned char>(text[i]);
            if(c < 0x20 || c > 0x7e || c == '"' || c == '\\')
            {
                quoted += "\\x";
                quoted += digits[c >> 4];
                quoted += digits[c & 0xF];
            }
            else
            {
                quoted += static_cast<char>(c);
            }
        }
        quoted += "\"";

        if(text.size() > limit)
        {
            quoted += " (first 64 characters)";
        }
        return quoted;
    }

    std::string describe(unsigned char c)
    {
        if(c >= 0x20 && c <= 0x7e)
        {
            return std::string("'") + static_cast<char>(c) + "'";
        }

        constexpr char digits[] = "0123456789abcdef";
        std::string text = "byte 0x";
        text += digits[c >> 4];
        text += digits[c & 0xF];
        return text;
    }

    [[noreturn]] void reject(const std::string &text, const std::string &problem)
    {
        throw std::runtime_error("ecs::Uuid(" + quote(text) + "): " + problem);
    }

    int hexValue(unsigned char c)
    {
        if(c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if(c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        if(c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return -1;
    }
}

ecs::Uuid::Uuid()
{
    auto &generator = generatorGet();
    this->high = generator();
    this->low = generator();

    // Version 4 in the high half, the standard variant in the low half.
    this->high = (this->high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    this->low = (this->low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
}

ecs::Uuid::Uuid(const std::string &id)
{
    if(id.empty())
    {
        reject(id, "text is empty");
    }
    if(id.size() != 36)
    {
        reject(id, "text is " + std::to_string(id.size()) + " characters long, expected 36");
    }

    std::uint64_t parsedHigh = 0;
    std::uint64_t parsedLow = 0;
    std::size_t digits = 0;

    for(std::size_t position = 1; position <= 36; position++)
    {
        auto c = static_cast<unsigned char>(id[position - 1]);

        if(position == 9 || position == 14 || position == 19 || position == 24)
        {
            if(c != '-')
            {
                reject(id, "expected '-' at position " + std::to_string(position) + ", found " + describe(c));
            }
            continue;
        }

        int value = hexValue(c);
        if(value < 0)
        {
            reject(id, "invalid character " + describe(c) + " at position " + std::to_string(position)
                       + ", expected a hexadecimal digit");
        }

        if(digits < 16)
        {
            parsedHigh = (parsedHigh << 4) | static_cast<std::uint64_t>(value);
        }
        else
        {
            parsedLow = (parsedLow << 4) | static_cast<std::uint64_t>(value);
        }
        digits++;
    }

    this->high = parsedHigh;
    this->low = parsedLow;
}

std::string ecs::Uuid::Get() const
{
    std::string text(36, '-');
    writeHalf(text.data(), this->high, highOffsets);
    writeHalf(text.data(), this->low, lowOffsets);
    return text;
}
