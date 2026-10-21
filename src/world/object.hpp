#pragma once
#include <cmath>
#include <utility>
#include <glm/glm.hpp>

#include "../utils/byte_stream.hpp"

namespace world {
// Tile a dropped object at this pixel position lies on. Drops land a few
// pixels around tile * 32 + 8, sometimes just over the edge of the tile above.
[[nodiscard]] inline std::pair<int, int> drop_tile(const float pos_x, const float pos_y)
{
    return { static_cast<int>(std::floor((pos_x + 8.0f) / 32.0f)), static_cast<int>(std::floor((pos_y + 8.0f) / 32.0f)) };
}

struct Object {
    std::uint16_t item_id;
    glm::vec2 pos;
    std::uint8_t amount;
    std::uint8_t flags;
    std::uint32_t object_id;

    Object()
        : item_id{ 0 }
        , pos{ 0.0f, 0.0f }
        , amount{ 0 }
        , flags{ 0 }
        , object_id{ 0 }
    {

    }

    [[nodiscard]] bool operator==(const Object& other) const
    {
        return item_id == other.item_id && pos == other.pos && amount == other.amount &&
            flags == other.flags && object_id == other.object_id;
    }

    void serialize(utils::ByteStream<>& bs, const std::uint16_t version)
    {
        bs.read(item_id);
        bs.read(pos.x);
        bs.read(pos.y);
        bs.read(amount);
        bs.read(flags);
        bs.read(object_id);
    }
};
}
