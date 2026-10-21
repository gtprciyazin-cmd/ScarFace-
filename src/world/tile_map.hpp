#pragma once
#include <algorithm>
#include <optional>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include "tile.hpp"
#include "../utils/byte_stream.hpp"

class WorldTileMap final {
public:
    WorldTileMap()
        : size_{ 0, 0 }
    {

    }

    // Returns false when a tile could not be understood; tiles_ then holds
    // only the tiles before it.
    [[nodiscard]] bool serialize(utils::ByteStream<>& bs, const std::uint16_t version)
    {
        bs.read(size_.x);
        bs.read(size_.y);

        std::uint32_t count{};
        bs.read(count);
        bs.skip(5);

        // Every tile takes at least 8 bytes. A count the data cannot hold means
        // the format changed under us, so never let it allocate gigabytes.
        count = std::min<std::uint32_t>(count, static_cast<std::uint32_t>(bs.remaining() / 8));
        tiles_.resize(count);
        offsets_.clear();
        offsets_.reserve(count + 1);

        for (std::size_t i{ 0 }; i < tiles_.size(); ++i) {
            offsets_.push_back(static_cast<std::uint32_t>(bs.get_read_offset()));
            if (!tiles_[i].serialize(bs, version)) {
                spdlog::warn(
                    "Stopped reading world tiles at tile {} of {} (foreground {}, extra type {}, offset {}): unknown or truncated data",
                    i,
                    count,
                    tiles_[i].foreground,
                    static_cast<int>(tiles_[i].extra.get_type()),
                    bs.get_read_offset()
                );
                tiles_.resize(i);
                offsets_.resize(i + 1); // The bad tile's start ends the last good one.
                complete_ = false;
                return false;
            }
        }
        offsets_.push_back(static_cast<std::uint32_t>(bs.get_read_offset()));

        complete_ = !bs.failed() && tiles_.size() == static_cast<std::size_t>(size_.x) * static_cast<std::size_t>(size_.y);
        return !bs.failed();
    }

    // False when parsing stopped early: tiles past get_tiles().size() are unknown.
    [[nodiscard]] bool is_complete() const { return complete_; }
    [[nodiscard]] const glm::ivec2& get_size() const { return size_; }
    [[nodiscard]] glm::ivec2& get_size() { return size_; }
    [[nodiscard]] const std::vector<world::Tile>& get_tiles() const { return tiles_; }
    [[nodiscard]] std::vector<world::Tile>& get_tiles() { return tiles_; }

    // Where tile i starts in the map data it was read from (relative to the
    // packet's extra data); tile i ends where tile i + 1 starts. Lets a
    // handler patch tile bytes in place before the packet is forwarded.
    [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> get_tile_bytes(const std::size_t i) const
    {
        if (i + 1 >= offsets_.size()) {
            return std::nullopt;
        }
        return std::pair{ offsets_[i], offsets_[i + 1] };
    }

private:
    glm::ivec2 size_;
    std::vector<world::Tile> tiles_;
    std::vector<std::uint32_t> offsets_;
    bool complete_{ false };
};
