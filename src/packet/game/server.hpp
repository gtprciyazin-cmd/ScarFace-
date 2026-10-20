#pragma once
#include "../packet_helper.hpp"

namespace packet::game {
struct Disconnect : GamePacket<PacketId::Disconnect, PACKET_DISCONNECT> {
    bool read(const Payload& payload) override
    {
        return is_payload<GamePayload>(payload);
    }

    Payload write() override
    {
        GamePayload game_payload{};
        game_payload.packet.type = PACKET_TYPE;
        game_payload.packet.net_id = -1;
        return game_payload;
    }
};

struct OnSendToServer : VariantPacket<PacketId::OnSendToServer> {
    uint16_t port;
    int32_t token;
    int32_t user;
    std::string address;
    std::string door_id;
    std::string uuid_token;
    uint8_t login_mode;
    std::string username;

    bool read(const Payload& payload) override
    {
        const auto var{ get_payload_if<VariantPayload>(payload) };
        if (!var) {
            return false;
        }

        variant = var->variant;
        game_packet = var->game_packet;

        if (variant.size() < 5) {
            return false;
        }

        port = variant.get<int32_t>(1);
        token = variant.get<int32_t>(2);
        user = variant.get<int32_t>(3);

        const std::string raw_text{ variant.get<std::string>(4) };
        const std::string key{ raw_text.substr(0, raw_text.find_first_of('|')) };

        utils::TextParse text_parse{};
        text_parse.parse(raw_text);

        address = key;
        door_id = text_parse.get(key, 0);
        uuid_token = text_parse.get(key, 1);

        login_mode = variant.get<uint32_t>(5);
        username = variant.get<std::string>(6);
        return true;
    }

    Payload write() override
    {
        utils::TextParse text_parse{};
        text_parse.add(address, door_id, uuid_token);

        const PacketVariant variant{
            "OnSendToServer",
            static_cast<int32_t>(port),
            token,
            user,
            text_parse.get_raw(),
            static_cast<uint32_t>(login_mode),
            username
        };
        return VariantPayload{ game_packet, variant };
    }
};

struct OnSuperMainStartAcceptLogonHrdxs47254722215a : VariantPacket<PacketId::OnSuperMainStartAcceptLogonHrdxs47254722215a> {
    // Proton hash of the server's items.dat; sent as a signed or an unsigned
    // variant depending on the server, so both are accepted and kept on write.
    uint32_t item_hash;
    bool item_hash_signed{ true };
    std::string u;
    std::string uu;
    std::string uuu;
    uint32_t player_tribute_hash;

    bool read(const Payload& payload) override
    {
        const auto var{ get_payload_if<VariantPayload>(payload) };
        if (!var) {
            return false;
        }

        variant = var->variant;
        game_packet = var->game_packet;

        if (variant.size() < 6) {
            return false;
        }

        const auto& hash_var{ variant.get_variants()[1] };
        if (const auto* hash{ std::get_if<int32_t>(&hash_var) }) {
            item_hash = static_cast<uint32_t>(*hash);
            item_hash_signed = true;
        }
        else if (const auto* uhash{ std::get_if<uint32_t>(&hash_var) }) {
            item_hash = *uhash;
            item_hash_signed = false;
        }
        else {
            return false;
        }

        u = variant.get<std::string>(2);
        uu = variant.get<std::string>(3);
        uuu = variant.get<std::string>(4);
        player_tribute_hash = variant.get<uint32_t>(5);

        return true;
    }

    Payload write() override
    {
        PacketVariant variant{
            std::string{ "OnSuperMainStartAcceptLogonHrdxs47254722215a" },
            item_hash,
            u,
            uu,
            uuu,
            player_tribute_hash
        };
        if (item_hash_signed) {
            variant.set(1, static_cast<int32_t>(item_hash));
        }
        return VariantPayload{ game_packet, variant };
    }
};
}
