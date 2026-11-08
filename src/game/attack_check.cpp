#include "game/attack_check.h"

#include <unordered_map>

#include "game/race_models.h"
#include "game/spells.h"
#include "game/world_state.h"

namespace hailstone::game {

namespace mech = mechanics;


namespace {

class LiveAttackQueries final : public mech::AttackQueries {
public:
    explicit LiveAttackQueries(const AttackCheckInput& in) : in_(in) {}

    const mech::AttackSpawn* spawn(std::uint32_t id) {
        if (auto it = cache_.find(id); it != cache_.end()) return &it->second;
        auto s = in_.world->spawns.find(id);
        if (s == in_.world->spawns.end()) return nullptr;
        const Spawn& sp = s->second;
        mech::AttackSpawn a;
        a.type = sp.is_corpse ? 2 : sp.is_npc ? 1 : 0;
        a.id = sp.id;
        a.master_id = sp.pet_owner_id;
        a.body_wet = sp.body_wet;
        a.pvp = sp.pvp != 0;
        a.guild_id = static_cast<std::int32_t>(sp.guild_id);
        a.deity = sp.deity;
        levels_[id] = id == in_.world->player_id && in_.my_level ? in_.my_level : sp.level;
        races_[id] = {sp.race, sp.gender};
        gms_[id] = sp.gm;
        return &cache_.emplace(id, a).first->second;
    }

    const mech::AttackSpawn* attacker_of(const mech::AttackSpawn& self) override { return &self; }
    std::uint8_t level(const mech::AttackSpawn& s) override { return levels_[s.id]; }
    std::uint32_t top_master_id(const mech::AttackSpawn& s) override {
        // PlayerClient_GetTopMasterSpawnID: up the MasterID chain.
        std::uint32_t id = s.master_id;
        for (int i = 0; i < 8 && id; ++i) {
            const mech::AttackSpawn* m = spawn(id);
            if (!m || !m->master_id) break;
            id = m->master_id;
        }
        return id;
    }
    bool is_vehicle_race(const mech::AttackSpawn& s) override { 
        auto [race, gender] = races_[s.id];
        return game::is_vehicle_race(static_cast<std::uint16_t>(race), gender);
    }
    bool is_gm(const mech::AttackSpawn& s) override { return gms_[s.id]; }
    bool is_valid_spell_id(std::int32_t id) override { return id > 0 && in_.spells && in_.spells->get(static_cast<std::uint32_t>(id)); }
    const mech::AttackSpell* spell_by_id(std::int32_t id) override {
        const SpellData* d = id > 0 && in_.spells ? in_.spells->get(static_cast<std::uint32_t>(id)) : nullptr;
        if (!d) return nullptr;
        spell_ = {static_cast<std::int32_t>(d->target_type), d->spell_type};
        spell_data_ = d;
        return &spell_;
    }
    bool spell_affects(const mech::AttackSpell&, std::int32_t attribute) override {
        return spell_data_ && spell_data_->has_effect(attribute);
    }
    const mech::AttackSpawn* spawn_by_id(std::uint32_t id) override { return spawn(id); }
    bool not_on_same_deity(const mech::AttackSpawn& self, const mech::AttackSpawn& other,
                           const mech::AttackSpell* spell) override {
        return mech::not_on_same_deity(&self, &other, spell != nullptr, gms_[other.id]);
    }
    bool has_owner(const mech::AttackSpawn& t) override { return t.type == 0; }
    bool is_corpse(const mech::AttackSpawn& s) override { return s.type >= 2; }
    bool is_self_or_group_member(const mech::AttackSpawn& s) override {
        auto it = in_.world->spawns.find(s.id);
        if (it == in_.world->spawns.end()) return false;
        if (s.id == in_.world->player_id) return true;
        return in_.self_or_group_member && in_.self_or_group_member(it->second.name);
    }
    bool is_newbie_zone(std::uint32_t) override { return false; }

private:
    const AttackCheckInput& in_;
    std::unordered_map<std::uint32_t, mech::AttackSpawn> cache_;
    std::unordered_map<std::uint32_t, std::uint8_t> levels_;
    std::unordered_map<std::uint32_t, std::pair<std::uint32_t, std::uint8_t>> races_; // race, gender
    std::unordered_map<std::uint32_t, bool> gms_;
    mech::AttackSpell spell_;
    const SpellData* spell_data_ = nullptr;
};

} // namespace

bool allowed_to_attack(const AttackCheckInput& in) {
    if (!in.world) return false;
    LiveAttackQueries q(in);
    const mech::AttackSpawn* me = q.spawn(in.world->player_id);
    if (!me) return false;
    const mech::AttackSpawn* target = in.target_id ? q.spawn(in.target_id) : nullptr;
    return mech::allowed_to_attack(*me, target, in.spell, in.rules, q);
}

} // namespace hailstone::game
