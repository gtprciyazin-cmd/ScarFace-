#pragma once
#include <algorithm>
#include <string>
#include <vector>

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
    int32_t login_mode;
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

        port = static_cast<uint16_t>(variant.get_integer(1));
        token = static_cast<int32_t>(variant.get_integer(2));
        user = static_cast<int32_t>(variant.get_integer(3));

        const std::string raw_text{ variant.get<std::string>(4) };
        const std::string key{ raw_text.substr(0, raw_text.find_first_of('|')) };

        utils::TextParse text_parse{};
        text_parse.parse(raw_text);

        address = key;
        door_id = text_parse.get(key, 0);
        uuid_token = text_parse.get(key, 1);

        // Sent as SIGNED; the username only on some redirects.
        login_mode = static_cast<int32_t>(variant.get_integer(5));
        username = variant.size() > 6 ? variant.get<std::string>(6) : std::string{};
        return true;
    }

    Payload write() override
    {
        // Start from the server's arguments and replace only the fields above,
        // so the client gets every argument with its original type and count.
        // Rebuilding it from scratch turned the SIGNED login mode into an
        // UNSIGNED 0 and added an empty username the server never sent.
        PacketVariant out{ variant };
        if (out.size() < 5) {
            out = PacketVariant{ std::string{ "OnSendToServer" }, int32_t{ 0 }, int32_t{ 0 }, int32_t{ 0 }, std::string{} };
        }

        out.set_integer(1, port);
        out.set_integer(2, token);
        out.set_integer(3, user);

        // ip|door|uuid[|more]: replace the first three fields, keep the rest.
        std::vector<std::string> parts{};
        const std::string raw_text{ out.get<std::string>(4) };
        for (std::size_t start{ 0 };;) {
            const auto end{ raw_text.find('|', start) };
            parts.emplace_back(raw_text.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        parts.resize(std::max<std::size_t>(parts.size(), 3));
        parts[0] = address;
        parts[1] = door_id;
        parts[2] = uuid_token;

        std::string text{};
        for (std::size_t i{ 0 }; i < parts.size(); ++i) {
            if (i != 0) {
                text += '|';
            }
            text += parts[i];
        }
        out.set(4, text);

        out.set_integer(5, login_mode);
        if (out.size() > 6) {
            out.set(6, username);
        }

        return VariantPayload{ game_packet, out };
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

        if (variant.size() < 2) {
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

        const auto string_at = [this](const std::size_t index) {
            if (index < variant.size()) {
                if (const auto* value{ std::get_if<std::string>(&variant.get_variants()[index]) }) {
                    return *value;
                }
            }
            return std::string{};
        };

        u = string_at(2);
        uu = string_at(3);
        uuu = string_at(4);
        // [5] is the "proto=...|..." settings string; the tribute hash, when
        // present, is the integer after it.
        player_tribute_hash = static_cast<uint32_t>(variant.get_integer(6));

        return true;
    }

    Payload write() override
    {
        // Keep the server's arguments (types, the settings string, anything
        // newer) and replace only the fields above.
        PacketVariant out{ variant };
        if (out.size() < 2) {
            out = PacketVariant{ std::string{ "OnSuperMainStartAcceptLogonHrdxs47254722215a" }, int32_t{ 0 } };
        }

        if (item_hash_signed) {
            out.set(1, static_cast<int32_t>(item_hash));
        }
        else {
            out.set(1, item_hash);
        }

        const auto set_string = [&out](const std::size_t index, const std::string& value) {
            if (index < out.size() && std::holds_alternative<std::string>(out.get_variants()[index])) {
                out.set(index, value);
            }
        };
        set_string(2, u);
        set_string(3, uu);
        set_string(4, uuu);
        out.set_integer(6, player_tribute_hash);

        return VariantPayload{ game_packet, out };
    }
};
}
