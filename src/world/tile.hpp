#pragma once
#include <string>

#include <fmt/format.h>
#include "tile_extra.hpp"

#include "../item/item_database.hpp"

namespace world {
enum class TileFlag : std::uint16_t {
    None = 0,
    Extra = 1 << 0,
    Locked = 1 << 1,
    Seed = 1 << 4,
    Flipped = 1 << 5,
    Open = 1 << 6,
    Public = 1 << 7,
    Silenced = 1 << 9,
    Water = 1 << 10,
    Glue = 1 << 11,
    Fire = 1 << 12,
    Red = 1 << 13,
    Green = 1 << 14,
    Blue = 1 << 15
};

[[nodiscard]] inline TileFlag operator|(TileFlag lhs, TileFlag rhs) {
    return static_cast<TileFlag>(static_cast<std::uint16_t>(lhs) | static_cast<std::uint16_t>(rhs));
}

[[nodiscard]] inline TileFlag operator&(TileFlag lhs, TileFlag rhs) {
    return static_cast<TileFlag>(static_cast<std::uint16_t>(lhs) & static_cast<std::uint16_t>(rhs));
}

[[nodiscard]] inline TileFlag operator~(TileFlag flag) {
    return static_cast<TileFlag>(~static_cast<std::uint16_t>(flag));
}

[[nodiscard]] inline TileFlag& operator|=(TileFlag& lhs, TileFlag rhs) {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] inline bool has_flag(TileFlag flags, TileFlag flag) {
    return (flags & flag) == flag;
}

struct Tile {
    std::uint16_t foreground;
    std::uint16_t background;
    std::uint16_t parent_tile;
    TileFlag flag;
    std::uint16_t lock_parent_tile;
    tile_extra::TileExtra extra;

    Tile()
        : foreground{ 0 }
        , background{ 0 }
        , parent_tile{ 0 }
        , flag{ TileFlag::None }
        , lock_parent_tile{ 0 }
    {

    }

    // Returns false when the tile data is unknown or truncated: everything
    // after it is out of sync and must not be parsed.
    [[nodiscard]] bool serialize(utils::ByteStream<>& bs, const std::uint16_t version)
    {
        // Start clean: fields that this tile does not carry (lock parent, extra)
        // must not keep values from the tile that was here in the last world.
        *this = Tile{};

        bs.read(foreground);
        bs.read(background);
        bs.read(parent_tile);

        std::uint16_t flag_val{};
        bs.read(flag_val);
        flag = static_cast<TileFlag>(flag_val);

        if (has_flag(flag, TileFlag::Locked)) {
            bs.read(lock_parent_tile);
        }

        // Extra data is present only when the flag says so; current parsers
        // (Mori, gtworld-r, badewen) never guess it from the item type.
        if (has_flag(flag, TileFlag::Extra) && !extra.serialize(bs, version, foreground, background)) {
            return false;
        }

        // Some tiles keep more state as a length-prefixed CBOR blob after the
        // extra data, whether or not the extra flag is set.
        if (has_cbor_data(foreground)) {
            std::uint32_t length{};
            bs.read(length);
            bs.skip(length);
        }

        return !bs.failed();
    }

    [[nodiscard]] static bool has_cbor_data(const std::uint16_t fg)
    {
        switch (fg) {
        case 3548:  // Battle Pet Cage
        case 12598: // Offering Table
        case 14662: // Operating Table
        case 14666: // Auto Surgeon Station
        case 14962: // Shirt Rack
        case 15376: // Party Projector
        case 15546: // Auction Block
            return true;
        default:
            break;
        }

        // Bountiful roots
        if (fg >= 8624 && fg <= 8714 && (fg - 8624) % 6 == 0) {
            return true;
        }

        // Newer items of the same kinds, recognised by their extra file.
        if (const auto* item{ item::ItemDatabase::instance().get_item(fg) }) {
            const std::string_view extra_file{ item->extra_file };
            return extra_file.ends_with("AutoSurgeonStation.xml")
                || extra_file.ends_with("ClothesRack.xml")
                || extra_file.ends_with("OperatingTable.xml");
        }

        return false;
    }

    [[nodiscard]] std::string flag_to_string() const
    {
        if (flag == TileFlag::None) {
            return "None";
        }

        std::string flag_string;
        
        if (has_flag(flag, TileFlag::Extra)) flag_string.append("Extra|");
        if (has_flag(flag, TileFlag::Locked)) flag_string.append("Locked|");
        if (has_flag(flag, TileFlag::Seed)) flag_string.append("Seed|");
        if (has_flag(flag, TileFlag::Flipped)) flag_string.append("Flipped|");
        if (has_flag(flag, TileFlag::Open)) flag_string.append("Open|");
        if (has_flag(flag, TileFlag::Public)) flag_string.append("Public|");
        if (has_flag(flag, TileFlag::Silenced)) flag_string.append("Silenced|");
        if (has_flag(flag, TileFlag::Water)) flag_string.append("Water|");
        if (has_flag(flag, TileFlag::Glue)) flag_string.append("Glue|");
        if (has_flag(flag, TileFlag::Fire)) flag_string.append("Fire|");
        if (has_flag(flag, TileFlag::Red)) flag_string.append("Red|");
        if (has_flag(flag, TileFlag::Green)) flag_string.append("Green|");
        if (has_flag(flag, TileFlag::Blue)) flag_string.append("Blue|");

        if (!flag_string.empty()) {
            flag_string.pop_back();
        }

        return flag_string;
    }
};
}
