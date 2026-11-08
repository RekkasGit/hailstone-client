#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "assets/sound_files.h"
#include "game_files.h"

using namespace hailstone::assets;

namespace {

std::string text(const std::optional<std::vector<std::byte>>& b) {
    return b ? std::string(reinterpret_cast<const char*>(b->data()), b->size()) : std::string();
}

void put32(std::vector<std::byte>& d, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) d[at + static_cast<std::size_t>(i)] = static_cast<std::byte>((v >> (8 * i)) & 0xffu);
}
void putf(std::vector<std::byte>& d, std::size_t at, float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(d, at, v);
}

} // namespace

TEST_CASE("_sounds.eff: 84-byte records, fields by offset") {
    std::vector<std::byte> d(84 * 2 + 10); // two records and a partial one
    putf(d, 16, -782.5f);
    putf(d, 20, 890.25f);
    putf(d, 24, -147.0f);
    putf(d, 28, 850.0f);
    put32(d, 32, 1000);
    put32(d, 36, 2000);
    put32(d, 40, 300);
    put32(d, 48, 0);
    put32(d, 52, 165);
    d[56] = std::byte{0};
    d[57] = std::byte{1};
    put32(d, 60, 0);
    put32(d, 64, static_cast<std::uint32_t>(-5));
    put32(d, 68, 2000);
    put32(d, 72, 3000);
    put32(d, 76, 50);
    put32(d, 80, 60);
    putf(d, 84 + 28, -2.0f);
    auto r = parse_sounds_eff(d);
    REQUIRE(r.size() == 2);
    CHECK(r[0].position.x == -782.5f);
    CHECK(r[0].position.y == 890.25f);
    CHECK(r[0].position.z == -147.0f);
    CHECK(r[0].radius == 850.0f);
    CHECK(r[0].cooldown[0] == 1000);
    CHECK(r[0].cooldown[1] == 2000);
    CHECK(r[0].random_delay == 300);
    CHECK(r[0].sound[1] == 165);
    CHECK(r[0].kind[0] == 0);
    CHECK(r[0].kind[1] == 1);
    CHECK(r[0].level[1] == -5);
    CHECK(r[0].fade_out[0] == 2000);
    CHECK(r[0].fade_out[1] == 3000);
    CHECK(r[0].param[0] == 50);
    CHECK(r[0].param[1] == 60);
    CHECK(r[1].radius == -2.0f);
}

TEST_CASE("_sndbnk.eff: EMIT from 1, LOOP from 162, CR/LF and spaces removed, .wav added") {
    auto b = parse_sound_bank("EMIT\r\nfire_lp\r\n wfall md \r\nbird.WAV\r\n\r\nLOOP\r\nwind_lp2\nnight");
    REQUIRE(b.size() == 5);
    CHECK(b.at(1) == "fire_lp.wav");
    CHECK(b.at(2) == "wfallmd.wav");
    CHECK(b.at(3) == "bird.WAV");
    CHECK(b.at(162) == "wind_lp2.wav");
    CHECK(b.at(163) == "night.wav");
    // A second EMIT restarts at 1 and replaces.
    auto again = parse_sound_bank("EMIT\na\nEMIT\nb\n");
    CHECK(again.at(1) == "b.wav");
}

TEST_CASE("soundassets.txt, mp3index.txt") {
    auto s = parse_sound_assets("1^Unknown\n39^Death_M.WAV\r\n100^WaterIn.WAV^\nx^junk\n");
    CHECK(s.size() == 2);
    CHECK(s.at(39) == "Death_M.WAV");
    CHECK(s.at(100) == "WaterIn.WAV");
    // An id past int32 wraps as 32-bit atoi on every platform (strtol's 64-bit long saturated it to -1 on Linux).
    auto wide = parse_sound_assets("99999999999999999999^Big.WAV\n");
    REQUIRE(wide.size() == 1);
    CHECK(wide.contains(1661992959));
    // The named lines (FUN_004ab800 gives each a free id; FUN_004ab700 finds it by name).
    auto n = parse_named_sound_assets("1^Unknown\nYouveGotMail^mail1.wav\r\n39^Death_M.WAV\n");
    REQUIRE(n.size() == 1);
    CHECK(n.at("YouveGotMail") == "mail1.wav");
    auto m = parse_mp3_index("bothunder.mp3\r\ncodecay.mp3\r\n\r\n poknowledge.mp3\n");
    REQUIRE(m.size() == 3);
    CHECK(m[2] == "poknowledge.mp3");
}

TEST_CASE(".emt rows: fields, version 1 defaults, short lines skipped") {
    auto rows = parse_emt("2,anguish.mp3,0,0,1.00,0,0,1,7.42,-14.08,6.84,0.00,0.00,0.00,0.00,5000,5000,0,1.00,0\n"
                          "short\n"
                          "1,torch3d.wav,3,2,0.75,100,200,0,-96.33,197.29,112.41,5.00,15.00,1.50,15.00,10,20,4,0.5\n");
    REQUIRE(rows.size() == 2);
    const EmtRow& a = rows[0];
    CHECK(a.version == 2);
    CHECK(a.file == "anguish.mp3");
    CHECK(a.gain == 1.0f);
    CHECK(a.loop_count == 1);
    CHECK(a.position.x == doctest::Approx(7.42));
    CHECK(a.position.y == doctest::Approx(-14.08));
    CHECK(a.position.z == doctest::Approx(6.84));
    CHECK(a.delay_min == 5000);
    CHECK(a.delay_max == 5000);
    CHECK(a.echo == 1.0f);
    CHECK_FALSE(a.user_disableable); // field 19 = 0
    const EmtRow& b = rows[1];
    CHECK(b.environment == 3);
    CHECK(b.period == 2);
    CHECK(b.fade_in == 100);
    CHECK(b.fade_out == 200);
    CHECK(b.full_volume_distance == 5.0f);
    CHECK(b.max_distance == 15.0f);
    CHECK(b.random_distance == 1.5f);
    CHECK(b.range == 15.0f);
    CHECK(b.xmi_track == 4);
    CHECK(b.user_disableable); // version 1
}

TEST_CASE("racedata.txt: 18 male, 18 female, 26 male, 26 female") {
    std::string line = "7";
    for (int i = 0; i < 88; ++i) line += "^" + std::to_string(i + 1);
    line += "^6^5^0\n";
    auto r = parse_race_sounds("# comment\n" + line);
    REQUIRE(r.contains(7));
    const auto& ids = r.at(7);
    CHECK(ids.male[0] == 1);
    CHECK(ids.male[17] == 18);
    CHECK(ids.female[0] == 19);
    CHECK(ids.female[17] == 36);
    CHECK(ids.male[18] == 37);
    CHECK(ids.male[43] == 62);
    CHECK(ids.female[18] == 63);
    CHECK(ids.female[43] == 88);
}

TEST_CASE("racedata.txt, live layout: a line per race and gender") {
    // race ^ gender ^ 44 ids ^ male height ^ female height ^ ^ 0 ^ code ^ ... (the live client's file)
    auto line = [](int race, const char* gender, int base, const char* code) {
        std::string l = std::to_string(race) + "^" + gender;
        for (int i = 0; i < 44; ++i) l += "^" + std::to_string(base + i);
        return l + "^6^6^^0^" + code + "^0^1^0^0^1^60^\n";
    };
    auto r = parse_race_sounds(line(10, "0", 100, "OGM") + line(10, "1", 200, "OGF") + line(75, "2", 300, "ELE"));
    REQUIRE(r.contains(10));
    CHECK(r.at(10).male[0] == 100);
    CHECK(r.at(10).male[43] == 143);
    CHECK(r.at(10).female[0] == 200);
    CHECK(r.at(10).female[43] == 243);
    CHECK(r.at(75).male[5] == 305); // gender 2: both
    CHECK(r.at(75).female[5] == 305);
    CHECK(is_live_racedata_line(line(1, "0", 0, "HUM")));
    std::string rof2 = "7";
    for (int i = 0; i < 90; ++i) rof2 += "^0";
    CHECK_FALSE(is_live_racedata_line(rof2));
}

TEST_CASE("spellsnew.eff: the stage sounds") {
    std::vector<std::byte> d(268 * 2);
    std::memcpy(d.data(), "FrostCoat", 9);
    put32(d, 64, 111);
    put32(d, 132, 0);
    put32(d, 200, 112);
    auto e = parse_spell_effect_sounds(d);
    REQUIRE(e.size() == 2);
    CHECK(e[0].name == "FrostCoat");
    CHECK(e[0].cast == 111);
    CHECK(e[0].impact == 112);
    CHECK(e[1].name.empty());
}

TEST_CASE("Sound files of the installed game") {
    const hailstone::GameDir* game = hailstone::test::game_dir();
    if (!game) return;
    // Greater Faydark: 19 records; record 12 is day 162 / night 164 ambience.
    auto eff = game->read("gfaydark_sounds.eff");
    REQUIRE(eff);
    CHECK(eff->size() == 1596);
    auto recs = parse_sounds_eff(*eff);
    REQUIRE(recs.size() == 19);
    CHECK(recs[12].kind[0] == 0);
    CHECK(recs[12].sound[0] == 162);
    CHECK(recs[12].sound[1] == 164);
    auto bank = parse_sound_bank(text(game->read("gfaydark_sndbnk.eff")));
    CHECK(bank.at(162) == "wind_lp2.wav");
    CHECK(bank.at(164) == "darkwds1.wav");
    // The global table and the old music index.
    auto assets = parse_sound_assets(text(game->read("soundassets.txt")));
    CHECK(assets.at(139) == "LevelUp.WAV");
    CHECK(assets.at(135) == "DoorWd_O.WAV");
    auto mp3 = parse_mp3_index(text(game->read("mp3index.txt")));
    REQUIRE(mp3.size() >= 2);
    CHECK(mp3[0] == "bothunder.mp3");
    // Human male sounds.
    auto races = parse_race_sounds(text(game->read("racedata.txt")));
    REQUIRE(races.contains(1));
    CHECK(races.at(1).male[3] == 32);    // JumpM_1
    CHECK(races.at(1).male[12] == 3001); // Walk_Lp
    CHECK(races.at(1).female[3] == 42);  // JumpF_1
    // spellsnew.eff: 675 effects.
    if (auto sn = game->read("spellsnew.eff")) {
        auto fx = parse_spell_effect_sounds(*sn);
        CHECK(fx.size() == sn->size() / 268);
    }
    // An EMT file: Anguish's one music row.
    if (auto emt = game->read("anguish.emt")) {
        auto rows = parse_emt(text(emt));
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].file == "anguish.mp3");
    }
}
