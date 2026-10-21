#include "packet_decoder.hpp"

#include <algorithm>
#include <cstring>
#include <string>

#include <magic_enum/magic_enum.hpp>

#include "../utils/formatter/packet_variant_formatter.hpp"
#include "../utils/formatter/text_parse_formatter.hpp"

namespace packet {
namespace {
// True when the bytes after the outer message type form an unambiguous game
// packet: a known packet type, the extended flag, and extra data that fills
// the rest exactly (the server appends one NUL).
[[nodiscard]] bool looks_like_game_packet(const std::span<const std::byte> data)
{
    constexpr std::size_t header_end{ sizeof(NetMessageType) + sizeof(GameUpdatePacket) };
    if (data.size() <= header_end) {
        return false;
    }

    GameUpdatePacket header{};
    std::memcpy(&header, data.data() + sizeof(NetMessageType), sizeof(header));
    if (!magic_enum::enum_contains(header.type) || header.flags.extended == 0 || header.data_size == 0) {
        return false;
    }

    const std::size_t rest{ data.size() - header_end };
    return header.data_size == rest || header.data_size + 1 == rest;
}
}

[[nodiscard]] std::optional<std::shared_ptr<IPacket>> PacketDecoder::decode(
    std::span<const std::byte> data,
    const core::Config::LogConfig& log_config,
    std::string_view direction
) {
    spdlog::debug("[{}] Decoding packet of size {} bytes", direction, data.size());

    utils::ByteStream stream{ data.data(), data.size() };

    NetMessageType msg_type{};
    if (!stream.read(msg_type)) {
        return std::nullopt;
    }

    // With protocol 227 some messages (likely the inventory) arrive under an
    // outer type the proxy does not know while carrying an ordinary game
    // packet. Read those as game packets; the original bytes are still the
    // ones forwarded.
    if ((msg_type == NET_MESSAGE_UNKNOWN || msg_type > NET_MESSAGE_GAME_PACKET) && looks_like_game_packet(data)) {
        spdlog::debug(
            "Net message type {} ({} bytes) carries a game packet, reading it as one",
            static_cast<std::uint32_t>(msg_type),
            data.size()
        );
        msg_type = NET_MESSAGE_GAME_PACKET;
    }

    switch (msg_type) {
    case NET_MESSAGE_SERVER_HELLO: {
        TextPayload text_payload{ 
            NET_MESSAGE_SERVER_HELLO, 
            utils::TextParse{}, 
            data
        };
        Payload payload = text_payload;

        spdlog::debug("Received server hello packet");
        return PacketRegistry::instance().create(payload);
    }
    case NET_MESSAGE_GENERIC_TEXT:
    case NET_MESSAGE_GAME_MESSAGE: {
        // The text runs to the end of the packet, usually NUL-terminated.
        std::string message{
            reinterpret_cast<const char*>(data.data() + sizeof(NetMessageType)),
            data.size() - sizeof(NetMessageType)
        };
        if (const auto nul{ message.find('\0') }; nul != std::string::npos) {
            message.resize(nul);
        }

        utils::TextParse parser{ message };
        if (log_config.print_message) {
            spdlog::debug(
                "{} ({} bytes):\n{}",
                magic_enum::enum_name(msg_type),
                message.size(),
                parser
            );
        }

        TextPayload text_payload{ msg_type, std::move(parser), data };
        auto packet = PacketRegistry::instance().create(text_payload);
        if (!packet) {
            return std::nullopt;
        }

        return packet;
    }
    case NET_MESSAGE_GAME_PACKET: {
        GameUpdatePacket game_pkt{};
        if (!stream.read(game_pkt)) {
            return std::nullopt;
        }

        const size_t extra_size = static_cast<size_t>(
             game_pkt.data_size > 0
                 ? game_pkt.data_size
                 : stream.get_size() - stream.get_read_offset()
        );

        // Create a view of the extra data
        std::span<const std::byte> extra_view{
            data.data() + stream.get_read_offset(),
            stream.get_size() - stream.get_read_offset()
        };

        spdlog::debug(
            "{} with type of {}",
            magic_enum::enum_name(msg_type),
            magic_enum::enum_name(game_pkt.type)
        );

        if (log_config.print_game_update_packet) {
            // View of the GameUpdatePacket bytes
            std::span<const std::byte> game_pkt_view{ 
                data.data() + sizeof(NetMessageType), 
                sizeof(GameUpdatePacket) 
            };
            spdlog::debug(
                "Game packet: {}",
                spdlog::to_hex(game_pkt_view.begin(), game_pkt_view.end())
            );
        }

        stream.skip(extra_size);

        if (game_pkt.type == PACKET_CALL_FUNCTION) {
            PacketVariant variant{};
            if (!variant.deserialize(extra_view)) {
                return std::nullopt;
            }

            if (log_config.print_variant) {
                spdlog::debug("{}", variant);
            }

            VariantPayload var_payload{ 
                game_pkt, 
                std::move(variant), 
                data 
            };
            Payload payload = var_payload;

            auto packet = PacketRegistry::instance().create(payload);
            if (!packet) {
                return std::nullopt;
            }

            return packet;
        }

        if (log_config.print_extra && extra_size > 1) {
            spdlog::debug("Extra data: {}", spdlog::to_hex(extra_view.begin(), extra_view.end()));
        }

        GamePayload game_payload{ 
            game_pkt, 
            extra_view, 
            data 
        };
        auto packet = PacketRegistry::instance().create(game_payload);
        if (!packet) {
            return std::nullopt;
        }

        return packet;
    }
    default: {
        // Forwarded untouched, but logged so new message types can be identified.
        const auto preview{ data.first(std::min<std::size_t>(data.size(), 64)) };
        spdlog::debug(
            "Unhandled net message type {} ({} bytes): {}",
            static_cast<std::uint32_t>(msg_type),
            data.size(),
            spdlog::to_hex(preview.begin(), preview.end())
        );

        // Error, track and client-log messages are NUL-terminated text.
        if (msg_type == NET_MESSAGE_ERROR || msg_type == NET_MESSAGE_TRACK || msg_type == NET_MESSAGE_CLIENT_LOG_REQUEST) {
            std::string text{ reinterpret_cast<const char*>(data.data() + sizeof(NetMessageType)), data.size() - sizeof(NetMessageType) };
            if (const auto nul{ text.find('\0') }; nul != std::string::npos) {
                text.resize(nul);
            }
            spdlog::debug("{}:\n{}", magic_enum::enum_name(msg_type), text);
        }

        return std::nullopt;
    }
    }
}
}
