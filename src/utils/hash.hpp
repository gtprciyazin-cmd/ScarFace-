#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

namespace utils::hash {
[[nodiscard]] constexpr std::int32_t fnv1a_32(std::string_view str, std::int32_t hash = 0x811c9dc5)
{
    for (auto& c : str) {
        hash ^= static_cast<std::int32_t>(c);
        hash *= 0x01000193;
    }

    return hash;
}

// Proton SDK HashString; the item hash in OnSuperMainStartAcceptLogon is this
// over the whole uncompressed items.dat. It must be computed in uint32: with
// int32 the >> sign-extends (and the sum overflows), so the result never
// matched the server's hash.
[[nodiscard]] constexpr std::uint32_t proton(const char* data, std::size_t length = 0)
{
    std::uint32_t hash{ 0x55555555 };
    if (!data) {
        return hash;
    }

    if (length == 0) {
        length = std::strlen(data);
    }

    while (length--) {
        hash = (hash >> 27) + (hash << 5) + static_cast<std::uint8_t>(*data++);
    }

    return hash;
}

// Returns 0 when the file cannot be read.
[[nodiscard]] inline std::uint32_t proton_file(const std::string& file_path)
{
    std::ifstream in{ file_path, std::ios::binary };
    if (!in) {
        return 0;
    }

    const std::streampos begin{ in.tellg() };
    in.seekg(0, std::ios::end);
    const std::streampos end{ in.tellg() };
    in.seekg(0, std::ios::beg);
    if (begin < 0 || end < begin) {
        return 0;
    }

    const std::size_t length{ static_cast<std::size_t>(end - begin) };
    if (length == 0) {
        return 0;
    }

    spdlog::debug("Calculating proton hash for file '{}' ({} bytes)", file_path, length);

    std::vector<char> buffer(length);
    in.read(buffer.data(), static_cast<std::streamsize>(length));

    return proton(buffer.data(), length);
}
}

[[nodiscard]] constexpr std::int32_t operator "" _fnv1a_32(const char* str, const std::size_t len)
{
    return utils::hash::fnv1a_32(std::string_view{ str, len });
}

[[nodiscard]] constexpr std::uint32_t operator "" _proton(const char* str, const std::size_t len)
{
    return utils::hash::proton(str, len);
}
