#pragma once
#include "../packet_helper.hpp"

namespace packet::game {
struct OnNameChanged : VariantPacket<PacketId::OnNameChanged> {
    int32_t net_id;
    std::string name;

    OnNameChanged()
        : net_id{ - 1}
    {

    }

    bool read(const Payload& payload) override
    {
        const auto var{ get_payload_if<VariantPayload>(payload) };
        if (!var) {
            return false;
        }

        variant = var->variant;
        game_packet = var->game_packet;

        if (variant.size() < 2) {
            return false;
        }

        // The renamed avatar is the call's target, carried in the header.
        net_id = static_cast<int32_t>(game_packet.net_id);
        name = variant.get<std::string>(1);
        return true;
    }

    Payload write() override
    {
        game_packet.net_id = static_cast<uint32_t>(net_id);

        // Keep any arguments the server added after the name.
        PacketVariant out{ variant };
        if (out.size() < 2) {
            out = PacketVariant{ "OnNameChanged", name };
        }
        out.set(1, name);
        return VariantPayload{ game_packet, out };
    }
};

// Sets an avatar's flag and badges: "us", or "us|showGuild|maxLevel" etc.
// Renaming a legend ("... of Legend``") makes the client reset these, so the
// proxy sends them again after its own renames.
struct OnCountryState : VariantPacket<PacketId::OnCountryState> {
    int32_t net_id{ -1 };
    std::string country;

    bool read(const Payload& payload) override
    {
        const auto var{ get_payload_if<VariantPayload>(payload) };
        if (!var) {
            return false;
        }

        variant = var->variant;
        game_packet = var->game_packet;

        if (variant.size() < 2) {
            return false;
        }

        net_id = static_cast<int32_t>(game_packet.net_id);
        country = variant.get<std::string>(1);
        return true;
    }

    Payload write() override
    {
        game_packet.net_id = static_cast<uint32_t>(net_id);

        PacketVariant out{ variant };
        if (out.size() < 2) {
            out = PacketVariant{ "OnCountryState", country };
        }
        out.set(1, country);
        return VariantPayload{ game_packet, out };
    }
};

struct OnChangeSkin : VariantPacket<PacketId::OnChangeSkin> {
    int32_t net_id;
    uint32_t skin_code;

    OnChangeSkin()
        : net_id{ -1 }
        , skin_code{ 0 }
    {

    }

    bool read(const Payload& payload) override
    {
        const auto var{ get_payload_if<VariantPayload>(payload) };
        if (!var) {
            return false;
        }

        variant = var->variant;
        game_packet = var->game_packet;

        if (variant.size() < 2) {
            return false;
        }

        skin_code = variant.get<uint32_t>(1);
        return true;
    }

    Payload write() override
    {
        game_packet.net_id = net_id;
        const PacketVariant variant{
            "OnChangeSkin",
            skin_code
        };
        return VariantPayload{ game_packet, variant };
    }
};

// Player movement. Sent by the client for the local player and by the server
// for every other player in the world. Positions are in pixels (32 per tile).
struct State : GamePacket<PacketId::State, PACKET_STATE> {
    int32_t net_id{ -1 };
    float pos_x{ 0.0f };
    float pos_y{ 0.0f };

    bool read(const Payload& payload) override
    {
        const auto game{ get_payload_if<GamePayload>(payload) };
        if (!game) {
            return false;
        }

        game_packet = game->packet;
        net_id = static_cast<int32_t>(game_packet.net_id);
        pos_x = game_packet.pos_x;
        pos_y = game_packet.pos_y;
        return true;
    }

    Payload write() override
    {
        GamePayload game_payload{};
        game_payload.packet = game_packet;
        game_payload.packet.type = PACKET_TYPE;
        game_payload.packet.net_id = static_cast<std::uint32_t>(net_id);
        game_payload.packet.pos_x = pos_x;
        game_payload.packet.pos_y = pos_y;
        return game_payload;
    }
};
}
