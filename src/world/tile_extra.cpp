#include "tile_extra.hpp"

#include <algorithm>

namespace world::tile_extra {
namespace {
// Caps a list length read from the wire to the number of 4-byte entries the
// remaining data can hold, so a corrupt length cannot allocate gigabytes.
[[nodiscard]] std::uint32_t clamp_u32_count(const utils::ByteStream<>& bs, const std::uint32_t count)
{
    return std::min<std::uint32_t>(count, static_cast<std::uint32_t>(bs.remaining() / sizeof(std::uint32_t)));
}

Door serialize_door(utils::ByteStream<>& bs)
{
    Door door{};
    bs.read(door.label);
    bs.read(door.unk);
    return door;
}

Sign serialize_sign(utils::ByteStream<>& bs)
{
    Sign sign{};
    bs.read(sign.label);
    bs.read(sign.unk);
    return sign;
}

Lock serialize_lock(utils::ByteStream<>& bs, const std::uint16_t foreground)
{
    Lock lock{};
    bs.read(lock.unk);
    bs.read(lock.owner_id);

    std::uint32_t access_size{};
    bs.read(access_size);
    access_size = clamp_u32_count(bs, access_size);

    lock.accesses.resize(access_size);

    for (std::uint32_t i = 0; i < access_size; i++) {
        bs.read(lock.accesses[i]);
    }

    bs.skip(8);
    if (foreground == 5814) {
        bs.skip(16);
    }

    return lock;
}

Seed serialize_seed(utils::ByteStream<>& bs) {
    Seed seed;
    bs.read(seed.growth_time);
    bs.read(seed.fruit_count);
    return seed;
}

Dice serialize_dice(utils::ByteStream<>& bs) {
    Dice dice;
    bs.read(dice.number);
    return dice;
}

Provider serialize_provider(utils::ByteStream<>& bs, std::uint16_t foreground) {
    Provider provider{};
    bs.read(provider.unk);

    // Well of Love and Winterfest Calendar 2017 carry 4 more bytes.
    if (foreground == 10656 || foreground == 5318) {
        bs.read(provider.unk2);
    }

    return provider;
}

Achievement serialize_achievement(utils::ByteStream<>& bs) {
    Achievement achievement;
    bs.read(achievement.unk);
    bs.read(achievement.unk2);
    return achievement;
}

HeartMonitor serialize_heart_monitor(utils::ByteStream<>& bs) {
    HeartMonitor heart_monitor;
    bs.read(heart_monitor.unk);
    bs.read(heart_monitor.label);
    return heart_monitor;
}

BunnyEgg serialize_bunny_egg(utils::ByteStream<>& bs) {
    BunnyEgg bunny_egg;
    bs.read(bunny_egg.unk);
    return bunny_egg;
}

Xenonite serialize_xenonite(utils::ByteStream<>& bs) {
    Xenonite xenonite;
    bs.read(xenonite.unk);
    bs.read(xenonite.unk2);
    return xenonite;
}

Crystal serialize_crystal(utils::ByteStream<>& bs) {
    Crystal crystal;
    bs.read(crystal.unk);
    return crystal;
}

Burglar serialize_burglar(utils::ByteStream<>& bs) {
    Burglar burglar;
    bs.read(burglar.unk);
    bs.read(burglar.unk2);
    bs.read(burglar.unk3);
    return burglar;
}

DisplayBlock serialize_display_block(utils::ByteStream<>& bs) {
    DisplayBlock display_block;
    bs.read(display_block.item_id);
    return display_block;
}

Vending serialize_vending(utils::ByteStream<>& bs) {
    Vending vending;
    bs.read(vending.item_id);
    bs.read(vending.price);
    return vending;
}

Solar serialize_solar(utils::ByteStream<>& bs) {
    Solar solar;
    bs.read(solar.unk);
    bs.read(solar.unk2);
    solar.unk2 = clamp_u32_count(bs, solar.unk2);
    solar.unk3.resize(solar.unk2);
    for (std::uint32_t i = 0; i < solar.unk2; i++) {
        bs.read(solar.unk3[i]);
    }
    return solar;
}

Deco serialize_deco(utils::ByteStream<>& bs) {
    Deco deco;
    bs.read(deco.unk);
    bs.read(deco.unk2);
    bs.read(deco.unk3);
    return deco;
}

SewingMachine serialize_sewing_machine(utils::ByteStream<>& bs) {
    SewingMachine sewing_machine{};
    std::uint32_t count{};
    bs.read(count);
    sewing_machine.items.resize(clamp_u32_count(bs, count));
    for (auto& item : sewing_machine.items) {
        bs.read(item);
    }
    return sewing_machine;
}

CountryFlag serialize_country_flag(utils::ByteStream<>& bs) {
    CountryFlag country_flag;
    bs.read(country_flag.flag);
    return country_flag;
}

BattleCage serialize_battle_cage(utils::ByteStream<>& bs) {
    BattleCage battle_cage;
    bs.read(battle_cage.label);
    bs.read(battle_cage.pet1);
    bs.read(battle_cage.pet2);
    bs.read(battle_cage.pet3);
    return battle_cage;
}

WeatherSpecial serialize_weather_special(utils::ByteStream<>& bs) {
    WeatherSpecial weather_special;
    bs.read(weather_special.color);
    return weather_special;
}

VipEntrance serialize_vip_entrance(utils::ByteStream<>& bs) {
    VipEntrance vip_entrance;
    bs.read(vip_entrance.unk);
    bs.read(vip_entrance.owner_id);
    std::uint32_t access_size{};
    bs.read(access_size);
    access_size = clamp_u32_count(bs, access_size);
    vip_entrance.accesses.resize(access_size);
    for (std::uint32_t i = 0; i < access_size; i++) {
        bs.read(vip_entrance.accesses[i]);
    }
    return vip_entrance;
}

GeigerCharger serialize_geiger_charger(utils::ByteStream<>& bs) {
    GeigerCharger geiger_charger;
    bs.read(geiger_charger.unk);
    return geiger_charger;
}

void skip_string(utils::ByteStream<>& bs)
{
    std::uint16_t length{};
    bs.read(length);
    bs.skip(length);
}

// Skips a u32 count followed by count entries of entry_size bytes.
template <typename CountType = std::uint32_t>
void skip_list(utils::ByteStream<>& bs, const std::size_t entry_size)
{
    CountType count{};
    bs.read(count);
    bs.skip(static_cast<std::size_t>(count) * entry_size);
}
}

// Layouts follow the live parsers of CLOEI/Mori (Apr 2026) and
// badewen/Growtopia-Things, which agree on every type handled here.
bool TileExtra::serialize(utils::ByteStream<>& bs, std::uint16_t version, std::uint16_t foreground, std::uint16_t background) {
    std::uint8_t type_val{};
    if (!bs.read(type_val)) {
        return false;
    }

    type_ = static_cast<Type>(type_val);

    switch (type_val) {
    case 1: data_ = serialize_door(bs); break;
    case 2: data_ = serialize_sign(bs); break;
    case 3: data_ = serialize_lock(bs, foreground); break;
    case 4: data_ = serialize_seed(bs); break;
    case 6:  // Mailbox
    case 7:  // Bulletin board
    case 12: // Donation box
        skip_string(bs);
        skip_string(bs);
        skip_string(bs);
        bs.skip(1);
        break;
    case 8: data_ = serialize_dice(bs); break;
    case 9: data_ = serialize_provider(bs, foreground); break;
    case 10: data_ = serialize_achievement(bs); break;
    case 11: data_ = serialize_heart_monitor(bs); break;
    case 14: // Mannequin
        skip_string(bs);
        bs.skip(23);
        break;
    case 15: data_ = serialize_bunny_egg(bs); break;
    case 16: bs.skip(1); break; // Game grave
    case 17: data_ = GameGen{}; break; // Game generator, no payload
    case 18: data_ = serialize_xenonite(bs); break;
    case 19: bs.skip(18); break; // Phone booth
    case 20: data_ = serialize_crystal(bs); break;
    case 21: data_ = serialize_burglar(bs); break;
    case 22: // Spotlight
    case 34: // Lobster trap
    case 45: // Challenge timer
    case 51: // DNA extractor
    case 52: // Howler
    case 58: // Adventure begins
    case 59: // Tomb robber
    case 74: // Safe vault
        break;
    case 23: data_ = serialize_display_block(bs); break;
    case 24: data_ = serialize_vending(bs); break;
    case 25: data_ = serialize_solar(bs); break; // Fish tank port
    case 26: bs.skip(5); break; // Solar collector
    case 27: bs.skip(4); break; // Forge
    case 28: data_ = serialize_deco(bs); break; // Giving tree
    case 30: bs.skip(5); break; // Steam organ
    case 31: // Silk worm
        bs.skip(1);
        skip_string(bs);
        bs.skip(29);
        break;
    case 32: data_ = serialize_sewing_machine(bs); break;
    case 33:
        // Only the plain Country Flag stores its flag name.
        if (foreground == 3394) {
            data_ = serialize_country_flag(bs);
        }
        break;
    case 35: // Painting easel
        bs.skip(4);
        skip_string(bs);
        break;
    case 36: data_ = serialize_battle_cage(bs); break;
    case 37: { // Pet trainer
        skip_string(bs);
        std::uint32_t count{};
        bs.read(count);
        bs.skip(4);
        bs.skip(static_cast<std::size_t>(count) * 4);
        break;
    }
    case 38: // Steam engine
    case 39: // Lock-bot
    case 41: // Spirit storage unit
    case 50: // Fossil prep station
    case 73: // Temporary platform
    case 79: // Pineapple guzzler
        bs.skip(4);
        break;
    case 40: data_ = serialize_weather_special(bs); break;
    case 42: bs.skip(21); break; // Data bedrock
    case 43: bs.skip(16); break; // Shelf
    case 44: data_ = serialize_vip_entrance(bs); break;
    case 47: // Fish wall mount
        skip_string(bs);
        bs.skip(5);
        break;
    case 48: { // Portrait
        skip_string(bs);
        bs.skip(18);
        std::uint16_t hat{};
        bs.read(hat);
        bs.skip(6);
        if (hat == 12958) { // Infinity Crown adds a string
            skip_string(bs);
        }
        break;
    }
    case 49: bs.skip(9); break; // Weather machine
    case 53: bs.skip(8); break; // Chemsynth tank
    case 54: { // Storage block: u16 byte length of its 13-byte records
        std::uint16_t length{};
        bs.read(length);
        bs.skip(length);
        break;
    }
    case 55: // Cooking oven: temperature, ingredient list, 3 more values
        bs.skip(4);
        skip_list(bs, 4);
        bs.skip(12);
        break;
    case 56: // Audio rack
        skip_string(bs);
        bs.skip(4);
        break;
    case 57: data_ = serialize_geiger_charger(bs); break;
    case 60: bs.skip(5); break; // Balloon-o-matic
    case 61: bs.skip(35); break; // Training port
    case 62: bs.skip(14); break; // Item sucker
    case 63: // CyBot: commands of 15 bytes, then 2 timers
        skip_list(bs, 15);
        bs.skip(8);
        break;
    case 65: bs.skip(17); break; // Guild item
    case 66: bs.skip(1); break; // Growscan
    case 67: // Containment field power node
        bs.skip(4);
        skip_list(bs, 4);
        break;
    case 68: // Spirit board
        bs.skip(4);
        skip_string(bs);
        skip_string(bs);
        skip_list(bs, 4);
        break;
    case 69: bs.skip(16); break; // Tesseract manipulator
    case 71: bs.skip(44); break; // Techno-organic engine (not independently confirmed)
    case 72: bs.skip(12); break; // Stormy cloud
    case 75: { // Angelic counting cloud
        std::uint32_t state{};
        bs.read(state);
        bs.skip(2);
        if (state == 2) {
            bs.skip(1);
        }
        break;
    }
    case 77: // Infinity weather machine
        bs.skip(4);
        skip_list(bs, 4);
        break;
    case 80: bs.skip(8); break; // Kraken galactic block
    case 81: // Friends entrance
        bs.skip(6);
        skip_list<std::uint16_t>(bs, 4);
        break;
    default:
        data_ = std::monostate{};
        return false;
    }

    return !bs.failed();
}
}
