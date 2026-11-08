// The original's vectors (recorded from eqgame.exe over a fake model whose virtual calls are
// logged): NPC tints, faces, beards, Luclin hair / beard parts and colours, classic body materials, new-model and
// old-model armour with hero's forge pieces and helms (game/actor_look.h). Each record's model calls and table
// lookups are compared in order, field by field.

#include <doctest/doctest.h>

#include <array>
#include <format>
#include <map>
#include <string>
#include <vector>

#include "game/actor_look.h"
#include "vector_files.h"

using hailstone::test::each_vector;
using hailstone::test::Json;
namespace game = hailstone::game;

namespace {

using Fields = std::map<std::string, std::string>;

// A JSON value as comparison text: numbers as written, bools as 0 / 1, null as "null".
std::string text_of(const Json& j) {
    switch (j.type) {
    case Json::Type::Null: return "null";
    case Json::Type::Bool: return j.b ? "1" : "0";
    case Json::Type::String: return "\"" + j.text + "\"";
    default: return j.text;
    }
}

std::string describe(const Fields& f) {
    std::string s = "{";
    for (const auto& [k, v] : f) s += k + "=" + v + " ";
    return s + "}";
}

std::string compare_list(const char* what, const Json& want, const std::vector<Fields>& got) {
    std::vector<Fields> expected;
    for (const Json& item : want.items) {
        Fields f;
        for (const auto& [k, v] : item.fields) f[k] = text_of(v);
        expected.push_back(std::move(f));
    }
    for (std::size_t i = 0; i < std::max(expected.size(), got.size()); ++i) {
        std::string e = i < expected.size() ? describe(expected[i]) : "(none)";
        std::string g = i < got.size() ? describe(got[i]) : "(none)";
        if (e != g) return std::format("{} #{}: {} original {}", what, i, g, e);
    }
    return "";
}

std::string num(std::int64_t v) { return std::to_string(v); }
std::string str(const std::string& s) { return "\"" + s + "\""; }

// The fake model and tables the vectors were recorded over, recording every call.
struct Recorder : game::ActorEnv {
    bool is_new = false;
    bool dags = true;
    bool parts = true;
    bool part_objects = true;
    bool hidden = false;
    bool attach_takes = true;
    bool replace_takes = true;
    std::optional<game::NewArmorTag> tag;
    bool adjustments = false;
    std::array<game::NewArmorAdjustment, 0x40> adjustment{};
    std::uint32_t hair_tint = 0;
    bool mock_set_fheb = false;
    bool mock_refresh_hair = false;
    std::vector<Fields> lookups, fheb;

    // One fake call: its fixed fields, and the text / colour fields the fake writes (the
    // record's name, name2 and color).
    struct Raw {
        Fields fixed;
        std::string name, name2;
        std::uint32_t color = 0;
    };
    std::vector<Raw> raw;
    // The fake's log holds 48 calls; once full, a call only rewrites the last entry's name / name2 / colour
    // (Add returns it without clearing), so the entry keeps its first op.
    void add(Fields fixed, const std::string* name = nullptr, const std::string* name2 = nullptr,
             const std::uint32_t* color = nullptr) {
        if (raw.size() < 48) raw.push_back({std::move(fixed), {}, {}, 0});
        Raw& r = raw.back();
        if (name) r.name = *name;
        if (name2) r.name2 = *name2;
        if (color) r.color = *color;
    }
    // The calls as the vectors write them (WriteCalls).
    std::vector<Fields> calls() const {
        std::vector<Fields> out;
        for (const Raw& r : raw) {
            Fields f = r.fixed;
            const std::string& op = f["op"];
            if (op == str("attach") || op == str("part_overlay") || op == str("robe_off") || op == str("robe_mesh")) {
                f["name"] = str(r.name);
            } else if (op == str("replace_material")) {
                f["from"] = str(r.name);
                f["to"] = str(r.name2);
                f["color"] = num(r.color);
            } else if (op == str("part_tint") || op == str("robe_tint")) {
                f["color"] = num(r.color);
            }
            out.push_back(std::move(f));
        }
        return out;
    }
    Recorder() {
        for (int k = 0; k < 0x40; ++k) adjustment[static_cast<std::size_t>(k)].part = k;
    }
    bool new_model() override { return is_new; }
    bool has_dag(std::int32_t) override { return dags; }
    bool has_part(std::int32_t) override { return parts; }
    std::optional<std::string> part_object(std::int32_t part) override {
        if (!part_objects) return std::nullopt;
        return std::format("PART{:02X}", static_cast<unsigned>(part & 0x3f));
    }
    bool mesh_hidden(std::int32_t slot) override { return hidden && slot == 1; }
    bool attach(std::int32_t dag, const std::string& name, const game::NewArmorAdjustment* adj) override {
        add({{"op", str("attach")}, {"dag", num(dag)}, {"adjustment_part", adj ? num(adj->part) : "null"}}, &name);
        return attach_takes;
    }
    void tint_part(std::int32_t dag, std::uint32_t color) override {
        add({{"op", str("part_tint")}, {"dag", num(dag)}, {"flag", "0"}}, nullptr, nullptr, &color);
    }
    void overlay_part(std::int32_t dag, const std::string& name) override {
        add({{"op", str("part_overlay")}, {"dag", num(dag)}}, &name);
    }
    bool replace_material(const std::string& from, const std::string& to, std::uint32_t color) override {
        add({{"op", str("replace_material")}, {"mode", "0"}, {"tint2_given", "0"}}, &from, &to, &color);
        return replace_takes;
    }
    void show_part(std::int32_t slot, std::uint8_t hide, std::int32_t bank) override {
        add({{"op", str("hide_mesh")}, {"slot", num(slot)}, {"hide", num(hide)}, {"bank", num(bank)}});
    }
    void robe_tint(std::int32_t index, std::uint32_t color) override {
        add({{"op", str("robe_tint")}, {"index", num(index)}}, nullptr, nullptr, &color);
    }
    void robe_off(const std::string& code) override {
        add({{"op", str("robe_off")}}, &code);
    }
    void robe_mesh(std::int32_t on, const std::string& code) override {
        add({{"op", str("robe_mesh")}, {"on", num(on)}}, &code);
    }
    std::optional<game::NewArmorTag> find_new_armor_tag(std::uint32_t id, const std::string&) override {
        lookups.push_back({{"fn", str("find_new_armor_tag")}, {"new_armor_id", num(id)}, {"ret_nonnull", tag ? "1" : "0"}});
        return tag;
    }
    const game::NewArmorAdjustment* find_adjustment(std::int32_t part, std::int32_t variant, const std::string&) override {
        lookups.push_back({{"fn", str("find_adjustment")}, {"part", num(part)}, {"variant", num(variant)},
                           {"ret_nonnull", adjustments ? "1" : "0"}});
        return adjustments ? &adjustment[static_cast<std::size_t>(part & 0x3f)] : nullptr;
    }
    std::uint32_t customization_hair_color(const game::ActorLook&, std::uint8_t) override { return hair_tint; }
    std::uint32_t set_fheb(game::ActorLook& look, std::uint32_t id, std::uint32_t type, std::uint32_t arg) override {
        if (!mock_set_fheb) return ActorEnv::set_fheb(look, id, type, arg);
        fheb.push_back({{"fn", str("set_fheb")}, {"id", num(id)}, {"type", num(type)}, {"arg", num(arg)}, {"ret", "119"}});
        return 0x77;
    }
    void refresh_hair(game::ActorLook& look, std::uint8_t show_hair) override {
        if (!mock_refresh_hair) return ActorEnv::refresh_hair(look, show_hair);
        lookups.push_back({{"fn", str("refresh_hair")}, {"show_hair", num(show_hair)}});
    }
};

game::ActorLook look_of(const Json& in) {
    game::ActorLook look;
    look.race = in["race"].i32();
    look.gender = static_cast<std::uint8_t>(in["gender"].i32());
    look.code = in["model_code"].text;
    return look;
}

void read_record(const Json& r, game::ActorLook& look, std::size_t slot) {
    game::ArmorRecord& a = look.armor[slot];
    a.material = r["material"].i32();
    a.variation = r["variation"].i32();
    a.material2 = r["material2"].i32();
    a.new_armor_id = r["new_armor_id"].u32();
    a.new_armor_type = r["new_armor_type"].u32();
    look.tint[slot] = r["tint"].u32();
}

void read_tag(const Json& in, Recorder& env) {
    const Json& t = in["new_armor_tag"];
    if (!t.is_null()) {
        game::NewArmorTag tag;
        tag.it_number = t["it_number"].i32();
        tag.it_pair = t["it_pair"].i32();
        tag.ignore_adjustment = t["ignore_adjustment"].truthy();
        tag.show_hair = t["show_hair"].truthy();
        tag.hide_mesh = t["hide_mesh"].truthy();
        env.tag = tag;
    }
    env.adjustments = in["adjustments_exist"].truthy();
}

std::string expect_int(const char* what, std::int64_t got, const Json& want) {
    if (got != want.i64()) return std::format("{} {} original {}", what, got, want.i64());
    return "";
}

} // namespace

TEST_CASE("ActorClient_ApplyNpcTint vectors") {
    each_vector("ActorClient_ApplyNpcTint.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        const Json& out = r["out"];
        auto entry = game::npc_tint_entry(in["index"].i32(), in["table_count"].i32());
        // Which actors get SetTint (the list's nodes, else the actor), as the original walks them.
        std::vector<Fields> tints;
        if (entry) {
            // The vectors give the entry as its offset from the table in a 32-bit address space.
            auto e = static_cast<std::int32_t>(static_cast<std::uint32_t>(*entry) * 4u) / 4;
            auto add = [&](const char* actor, int node) {
                tints.push_back({{"actor", str(actor)}, {"node", num(node)}, {"entry", num(e)}, {"flag", "0"}});
            };
            const Json& list = in["actor_list"];
            if (list.items.empty()) {
                if (in["has_actor"].truthy()) add("actor", -1);
            } else {
                for (std::size_t n = 0; n < list.items.size(); ++n) {
                    if (list[n]["first"].truthy()) add("first", static_cast<int>(n));
                    if (list[n]["second"].truthy()) add("second", static_cast<int>(n));
                }
            }
        }
        bool value = !tints.empty();
        if (value != out["value"].truthy()) return std::format("index {}: value {}", in["index"].i32(), value);
        return compare_list("tints", out["tints"], tints);
    });
}

TEST_CASE("ActorClient_SplitFace vectors") {
    each_vector("ActorClient_SplitFace.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        const Json& out = r["out"];
        auto got = game::split_face(in["face"].i32(), static_cast<std::uint8_t>(in["current_face"].i32()),
                                    in["race"].i32(), in["new_model"].truthy());
        if (auto e = expect_int("value", got.index, out["value"]); !e.empty()) return e;
        bool wanted = in["variant_wanted"].truthy();
        if (!wanted || !got.variant) {
            if (!out["variant"].is_null()) return "variant should be written";
        } else if (auto e = expect_int("variant", *got.variant, out["variant"]); !e.empty()) {
            return e;
        }
        return "";
    });
}

TEST_CASE("ActorClient_GetBeardKind vectors") {
    each_vector("ActorClient_GetBeardKind.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        return expect_int("kind", game::beard_kind(in["race"].i32(), static_cast<std::uint8_t>(in["gender"].i32())),
                          r["out"]["value"]);
    });
}

TEST_CASE("ActorClient_NormalizeSlotTextureIds vectors") {
    each_vector("ActorClient_NormalizeSlotTextureIds.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        const Json& out = r["out"];
        std::int32_t a = in["material"].i32();
        std::int32_t c = 0x7fff1234; // untouched marker
        game::normalize_slot_texture_ids(in["slot"].i32(), a, c);
        if (auto e = expect_int("a", a, out["a"]); !e.empty()) return e;
        if (c == 0x7fff1234) return out["overlay"].is_null() ? "" : "overlay should be written";
        return expect_int("overlay", c, out["overlay"]);
    });
}

TEST_CASE("ActorClient_GetAllowedHairColorIndexRange vectors") {
    each_vector("ActorClient_GetAllowedHairColorIndexRange.jsonl", [](const Json& r) -> std::string {
        std::int32_t race = r["in"]["race"].i32();
        auto range = game::allowed_hair_color_range(race < 0 ? 5 : race); // a negative race: the actor's (5)
        if (auto e = expect_int("low", range.low, r["out"]["low"]); !e.empty()) return e;
        return expect_int("high", range.high, r["out"]["high"]);
    });
}

TEST_CASE("ActorClient_AttachHelmIT vectors") {
    each_vector("ActorClient_AttachHelmIT.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        const Json& out = r["out"];
        game::ActorLook look = look_of(in);
        look.face = static_cast<std::uint8_t>(in["face"].i32());
        look.hair_style = 3;
        look.beard = 4;
        Recorder env;
        env.is_new = in["old_model_check"].truthy();
        env.dags = in["has_dags_8_9"].truthy();
        env.attach_takes = in["attach_takes"].truthy();
        bool value = game::attach_helm_it(look, env, in["id"].i32());
        if (auto e = expect_int("value", value, out["value"]); !e.empty()) return e;
        if (auto e = expect_int("hair_style", look.hair_style, out["hair_style"]); !e.empty()) return e;
        if (auto e = expect_int("beard", look.beard, out["beard"]); !e.empty()) return e;
        return compare_list("calls", out["calls"], env.calls());
    });
}

TEST_CASE("ActorClient_SetFHEB_Color vectors") {
    each_vector("ActorClient_SetFHEB_Color.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        const Json& out = r["out"];
        game::ActorLook look = look_of(in);
        look.face = static_cast<std::uint8_t>(in["face"].i32());
        look.hair_color = 0x55;
        look.beard_color = 0x66;
        look.new_style = in["new_model_classic"].truthy();
        Recorder env;
        env.dags = env.parts = in["has_dags_8_9"].truthy();
        env.mock_set_fheb = true;
        std::uint32_t value = game::set_fheb_color(look, env, in["color"].u32(), in["type"].u32(), 0x1234);
        if (auto e = expect_int("value", value & 0xff, out["value"]); !e.empty()) return e;
        if (auto e = expect_int("hair_color", look.hair_color, out["hair_color"]); !e.empty()) return e;
        if (auto e = expect_int("beard_color", look.beard_color, out["beard_color"]); !e.empty()) return e;
        if (auto e = compare_list("set_fheb", out["set_fheb"], env.fheb); !e.empty()) return e;
        return compare_list("calls", out["calls"], env.calls());
    });
}

TEST_CASE("ActorClient_SwapBodyMaterial vectors") {
    each_vector("ActorClient_SwapBodyMaterial.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        const Json& out = r["out"];
        game::ActorLook look = look_of(in);
        look.luclin_model = in["luclin_model"].truthy();
        look.face_variant = in["face_variant"].i32();
        Recorder env;
        env.replace_takes = in["replace_takes"].truthy();
        std::int32_t slot = in["slot"].i32();
        std::uint8_t value = game::swap_body_material(look, env, slot, in["from"].i32(), in["to"].i32(),
                                                      in["tint"].u32(), in["variant"].i32(), in["kind"].i32(),
                                                      in["no_tint"].truthy() ? 1u : 0u);
        if (auto e = expect_int("value", value, out["value"]); !e.empty()) return e;
        if (slot >= 0 && slot < 9) {
            auto s = static_cast<std::size_t>(slot);
            if (auto e = expect_int("slot_material", look.armor[s].material, out["slot_material"]); !e.empty()) return e;
            if (auto e = expect_int("slot_tint", look.tint[s], out["slot_tint"]); !e.empty()) return e;
        }
        if (auto e = expect_int("robe", look.armor[1].material, out["robe"]); !e.empty()) return e;
        return compare_list("calls", out["calls"], env.calls());
    });
}

TEST_CASE("ActorClient_UpdateNewArmorSlot vectors") {
    each_vector("ActorClient_UpdateNewArmorSlot.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        const Json& out = r["out"];
        game::ActorLook look = look_of(in);
        auto slot = in["slot"].u32();
        read_record(in["record"][0], look, slot);
        look.armor[1].material = in["robe"].i32(); // the chest record is the robe (+0xa0)
        look.new_armor_filtered = in["new_armor_filtered"].truthy();
        look.show_helm = in["show_helm"].truthy();
        look.hair_style = static_cast<std::uint8_t>(in["hair_style"].i32());
        look.hair_color = static_cast<std::uint8_t>(in["hair_color"].i32());
        Recorder env;
        env.is_new = true;
        env.part_objects = in["parts_exist"].truthy();
        env.parts = in["dag_parts_exist"].truthy();
        env.attach_takes = in["attach_takes"].truthy();
        env.hair_tint = in["hair_tint"].u32();
        read_tag(in, env);
        game::update_new_armor_slot(look, env, slot);
        if (auto e = compare_list("lookups", out["lookups"], env.lookups); !e.empty()) return e;
        return compare_list("calls", out["calls"], env.calls());
    });
}

TEST_CASE("ActorClient_ApplyArmorTexture vectors") {
    each_vector("ActorClient_ApplyArmorTexture.jsonl", [](const Json& r) -> std::string {
        const Json& in = r["in"];
        const Json& out = r["out"];
        game::ActorLook look = look_of(in);
        for (std::size_t s = 0; s < 9; ++s) read_record(in["records"][s], look, s);
        look.new_armor_filtered = in["new_armor_filtered"].truthy();
        look.show_helm = in["show_helm"].truthy();
        Recorder env;
        env.hidden = in["chest_mesh_hidden"].truthy();
        env.mock_refresh_hair = true;
        read_tag(in, env);
        game::apply_armor_texture(look, env, in["slot"].i32());
        if (auto e = compare_list("lookups", out["lookups"], env.lookups); !e.empty()) return e;
        return compare_list("calls", out["calls"], env.calls());
    });
}
