// rof2decomp batch 256's vectors (recorded from the original eqgame.exe): the listener's place and room, the
// player races of the swim sounds, the music entries and their selection, and the spell effect stage sounds.

#include <doctest/doctest.h>

#include <format>
#include <set>
#include <string>
#include <vector>

#include "audio/listener.h"
#include "audio/music_manager.h"
#include "game/sound_rules.h"
#include "vector_files.h"

using nmsc::test::each_vector;
using nmsc::test::Json;
using nmsc::game::mechanics::FpuPrecision;

namespace {

std::uint32_t vec_bits(const Json& j) { return nmsc::test::f32_bits(j); }
float vec_float(const Json& j) { return std::bit_cast<float>(nmsc::test::f32_bits(j)); }

FpuPrecision precision_of(const Json& r) {
    return r["precision"].text == "x87-53" ? FpuPrecision::Double53 : FpuPrecision::Single24;
}

std::string bits_diff(const std::string& what, std::uint32_t got, const Json& want) {
    if (got == vec_bits(want)) return "";
    return std::format("{}: {:08x}, original {:08x}", what, got, vec_bits(want));
}

} // namespace

TEST_CASE("UpdateListenerEnvironment vectors") {
    each_vector("UpdateListenerEnvironment.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        auto env = nmsc::audio::listener_environment(in["have_local_player"].truthy(), vec_float(in["ceiling"]),
                                                     vec_float(in["floor"]), in["indoors"].truthy(),
                                                     precision_of(r));
        const Json& want = r["out"]["environment"];
        if (want.is_null() != !env.has_value() || (env && *env != want.i32()))
            return std::format("ceiling {} floor {}: {}, original {}", in["ceiling"].text, in["floor"].text,
                               env ? *env : 0, want.is_null() ? 0 : want.i32());
        if (r["out"]["special_environment"].truthy()) return "special environment left set";
        return "";
    });
}

TEST_CASE("EqSoundManager_SetListenerLocation vectors") {
    each_vector("EqSoundManager_SetListenerLocation.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        const Json& calls = r["out"]["calls"];
        if (in["sound_disabled"].truthy()) return calls.items.empty() ? "" : "a call while disabled";
        if (calls.items.size() != 1) return "no call";
        auto l = nmsc::audio::eq_listener_location(vec_bits(in["y"]), vec_bits(in["x"]), vec_bits(in["z"]),
                                                   vec_bits(in["heading"]), precision_of(r));
        const Json& c = calls[0];
        for (auto d : {bits_diff("x", l.x, c["x"]), bits_diff("y", l.y, c["y"]), bits_diff("z", l.z, c["z"]),
                       bits_diff("angle", l.angle, c["angle"])})
            if (!d.empty()) return d;
        return "";
    });
}

TEST_CASE("SoundManager_SetListenerLocation vectors") {
    each_vector("SoundManager_SetListenerLocation.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        const Json& out = r["out"];
        auto o = nmsc::audio::sound_manager_listener(vec_bits(in["x"]), vec_bits(in["y"]), vec_bits(in["z"]),
                                                     vec_bits(in["angle"]));
        for (auto d : {bits_diff("stored x", o.stored[0], out["stored_x"]), bits_diff("stored y", o.stored[1], out["stored_y"]),
                       bits_diff("stored z", o.stored[2], out["stored_z"]),
                       bits_diff("stored angle", o.stored[3], out["stored_angle"])})
            if (!d.empty()) return d;
        const Json& calls = out["calls"];
        if (!in["has_3d_listener"].truthy()) return calls.items.empty() ? "" : "calls without a listener";
        if (calls.items.size() != 2) return "two calls expected";
        const Json& p = calls[0];
        const Json& f = calls[1];
        for (auto d : {bits_diff("x", o.position[0], p["x"]), bits_diff("y", o.position[1], p["y"]),
                       bits_diff("z", o.position[2], p["z"]),
                       bits_diff("face x (angle " + in["angle"].text + ")", o.face[0], f["face_x"]),
                       bits_diff("face y (angle " + in["angle"].text + ")", o.face[1], f["face_y"]),
                       bits_diff("face z", o.face[2], f["face_z"]), bits_diff("up x", o.up[0], f["up_x"]),
                       bits_diff("up y", o.up[1], f["up_y"]), bits_diff("up z", o.up[2], f["up_z"])})
            if (!d.empty()) return d;
        return "";
    });
}

TEST_CASE("LegalPlayerRace vectors") {
    each_vector("LegalPlayerRace.jsonl", [](const Json& r) -> std::string {
        bool got = nmsc::audio::legal_player_race(r["in"]["race"].i32(), r["in"]["spawn_race"].i32());
        if (got != r["out"]["value"].truthy())
            return std::format("race {} spawn {}: {}", r["in"]["race"].i32(), r["in"]["spawn_race"].i32(), got);
        return "";
    });
}

TEST_CASE("LoadGlobalMusic vectors") {
    each_vector("LoadGlobalMusic.jsonl", [](const Json& r) -> std::string {
        std::set<std::string> files;
        for (const Json& f : r["in"]["files_found"].items) files.insert(f.text);
        auto got = nmsc::audio::load_global_music([&](const std::string& f) { return files.contains(f); },
                                                  r["in"]["rand"].i32());
        const Json& want = r["out"]["set"];
        if (got.size() != want.items.size()) return std::format("{} calls, original {}", got.size(), want.items.size());
        for (std::size_t i = 0; i < got.size(); ++i) {
            const auto& g = got[i];
            const Json& w = want[i];
            std::string asset = w["asset"].is_null() ? "" : w["asset"].text;
            if (g.entry != w["entry"].i32() || g.asset != asset || g.track != w["track"].i32() ||
                std::bit_cast<std::uint32_t>(g.volume) != vec_bits(w["volume"]) || g.unused != w["unused"].i32() ||
                g.priority != w["priority"].i32() || g.fade_in_ms != w["fade_in_ms"].i32() ||
                g.fade_out_replaced_ms != w["fade_out_replaced_ms"].i32() ||
                g.fade_out_stopped_ms != w["fade_out_stopped_ms"].i32() || g.pool != w["pool"].i32() ||
                w["byte_30"].i32() != 0 || w["byte_31"].i32() != 0)
                return std::format("call {}: entry {} '{}' track {} priority {} fades {}/{}/{} pool {}", i, g.entry,
                                   g.asset, g.track, g.priority, g.fade_in_ms, g.fade_out_replaced_ms,
                                   g.fade_out_stopped_ms, g.pool);
        }
        return "";
    });
}

TEST_CASE("MusicManager_SelectActiveEntry vectors") {
    each_vector("MusicManager_SelectActiveEntry.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        std::vector<nmsc::audio::MusicSelectEntry> entries;
        for (const Json& e : in["entries"].items)
            entries.push_back({e["selected"].truthy(), e["finished"].truthy(), e["priority"].i32(),
                               e["start_requested"].truthy()});
        int current = in["current"].is_null() ? -1 : in["current"].i32();
        float volume = vec_float(in["volume"]);
        auto s = nmsc::audio::select_active_entry(entries, current, in["music_enabled"].truthy(), volume);
        const Json& out = r["out"];
        int playing = out["playing"].is_null() ? -1 : out["playing"].i32();
        if (s.playing != playing) return std::format("playing {}, original {}", s.playing, playing);
        // The calls the port's answer makes, in the original's order.
        std::vector<std::string> got, want;
        if (s.fade_out >= 0) got.push_back(std::format("fade_out {} {}", s.fade_out, s.fade_out_nothing_replaces));
        if (s.fade_in) got.push_back(std::format("fade_in {} {:08x}", s.playing, vec_bits(in["volume"])));
        if (s.fade_instances) {
            const Json& e = in["entries"][static_cast<std::size_t>(s.playing)];
            for (std::int64_t k = 0; k < e["asset_instances"].i64() && k < 16; ++k)
                got.push_back(std::format("instance_fade 00000000 {}", e["fade_out_ms"].i32()));
        }
        for (const Json& c : out["calls"].items) {
            const std::string& fn = c["fn"].text;
            if (fn == "fade_out") want.push_back(std::format("fade_out {} {}", c["entry"].i32(), c["nothing_replaces"].truthy()));
            else if (fn == "fade_in") want.push_back(std::format("fade_in {} {:08x}", c["entry"].i32(), vec_bits(c["volume"])));
            else want.push_back(std::format("instance_fade {:08x} {}", vec_bits(c["volume"]), c["ms"].i32()));
        }
        if (got != want) {
            std::string g, w;
            for (const auto& x : got) g += "[" + x + "]";
            for (const auto& x : want) w += "[" + x + "]";
            return "calls " + g + ", original " + w;
        }
        return "";
    });
}

TEST_CASE("DoSpellEffect vectors") {
    each_vector("DoSpellEffect.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        nmsc::game::SpellEffectStageInput s;
        s.stage = static_cast<std::uint32_t>(in["stage"].i64());
        s.have_spell = in["have_spell"].truthy();
        s.have_effect = in["have_effect"].truthy();
        s.have_old_record = in["have_old_record"].truthy();
        s.have_new_record = in["have_new_record"].truthy();
        s.have_caster = in["have_caster"].truthy();
        s.have_target = in["have_target"].truthy();
        for (std::size_t k = 0; k < 3; ++k) s.stage_sounds[k] = in["stage_sounds"][k].i32();
        s.particles_filtered = in["particles_filtered"].truthy();
        s.dragon_breath = in["dragon_breath"].i32();
        s.particle_density = vec_float(in["particle_density"]);
        s.particle_opacity = vec_float(in["particle_opacity"]);
        s.spell_effects_option = in["spell_effects_option"].i32();
        auto e = nmsc::game::spell_effect_stage(s);
        const Json& out = r["out"];
        if (static_cast<std::int32_t>(e.value) != out["value"].i32()) return std::format("value {}", e.value);
        std::int32_t last = e.spawn_stage_emitters ? in["spawn_stage_emitters_result"].i32() : 0x5a5a5a5a;
        if (last != out["last_stage_emitters"].i32()) return "last stage emitters differ";
        std::vector<std::string> got, want;
        if (e.sound) got.push_back(std::format("play_sound {} {}", e.sound_at_target ? "target" : "caster", *e.sound));
        if (e.filter_asked) got.push_back(std::format("particle_filter {}", s.stage));
        if (e.dragon_breath_asked) got.push_back("uses_dragon_breath");
        if (e.spawn_stage_emitters) got.push_back(std::format("spawn_stage_emitters {} new", s.stage));
        if (e.do_particle_effect) got.push_back(std::format("do_particle_effect {} old", s.stage));
        for (const Json& c : out["calls"].items) {
            const std::string& fn = c["fn"].text;
            if (fn == "play_sound") want.push_back(std::format("play_sound {} {}", c["at"].text, c["sound"].i32()));
            else if (fn == "particle_filter") {
                if (c["caster"].text != "caster" || c["target"].text != (s.have_target ? "target" : "none"))
                    return "filter spawns";
                want.push_back(std::format("particle_filter {}", c["stage"].i32()));
            } else if (fn == "uses_dragon_breath") want.push_back(fn);
            else if (fn == "spawn_stage_emitters") {
                if (c["arg9"].i32() != 0 || c["arg10"].i32() != 0 || c["arg11"].i32() != -1 || c["a6"].i32() != in["a6"].i32() ||
                    c["a7"].i32() != in["a7"].i32())
                    return "emitter arguments";
                want.push_back(std::format("spawn_stage_emitters {} {}", c["stage"].i32(), c["record"].text));
            } else {
                if (c["arg6"].i32() != 0) return "particle argument 6";
                want.push_back(std::format("do_particle_effect {} {}", c["stage"].i32(), c["record"].text));
            }
        }
        if (got != want) {
            std::string g, w;
            for (const auto& x : got) g += "[" + x + "]";
            for (const auto& x : want) w += "[" + x + "]";
            return "calls " + g + ", original " + w;
        }
        return "";
    });
}
