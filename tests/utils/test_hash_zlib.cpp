#include <gtest/gtest.h>
#include "utils/hash.hpp"
#include "utils/zlib.hpp"

// Reference values from the unsigned Proton SDK HashString.
TEST(HashTest, ProtonMatchesReferenceValues)
{
    EXPECT_EQ(utils::hash::proton("hello", 5), 2975787161u);

    std::string bytes{};
    for (int round{ 0 }; round < 4; ++round) {
        for (int i{ 0 }; i < 256; ++i) {
            bytes.push_back(static_cast<char>(i));
        }
    }
    // High bytes and a long input exercise the sign/overflow bug of the old
    // int32 implementation.
    EXPECT_EQ(utils::hash::proton(bytes.data(), bytes.size()), 46182411u);
}

// Regression: decompress_zlib pre-filled expected_size bytes and appended the
// data after them, so every items.dat sent by the server was rejected.
TEST(ZlibTest, RoundTripWithExpectedSize)
{
    std::vector<std::byte> original(100000);
    for (std::size_t i{ 0 }; i < original.size(); ++i) {
        original[i] = static_cast<std::byte>((i * 31) % 251);
    }

    std::vector<std::byte> compressed{};
    ASSERT_TRUE(utils::compress_zlib(original, compressed));

    std::vector<std::byte> out{};
    ASSERT_TRUE(utils::decompress_zlib(compressed, out, static_cast<std::uint32_t>(original.size())));
    EXPECT_EQ(out, original);

    // A wrong expected size is still rejected.
    EXPECT_FALSE(utils::decompress_zlib(compressed, out, static_cast<std::uint32_t>(original.size() - 1)));
}
