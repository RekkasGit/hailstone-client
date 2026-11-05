#include "game/mechanics/touching_switch.h"

#include "game/collision_callbacks.h"

namespace nmsc::game::mechanics {

TouchResult touching_switch(const TouchedSwitch& sw, TouchSpawn& spawn, std::uint32_t now,
                            std::optional<std::uint8_t>& last_damage_switch, const TouchHooks& hooks) {
    TouchResult r;
    if (sw.state == 4) return r; // a sprung trap: forgotten
    if (sw.type >= 0x73) {
        if (!switch_acts_on_touch(sw.type)) {
            r.clear = false;
            return r;
        }
        std::uint32_t interval = sw.repop_ms < 1000 ? 400 : 1000;
        if (last_damage_switch == sw.id && now - spawn.last_touch_ms < interval) {
            r.clear = false;
            return r;
        }
        last_damage_switch = sw.id;
        r.damage = sw.damage;
        spawn.last_touch_ms = now;
    } else if (sw.type == 0x23) {
        if (now - spawn.last_touch_ms > 1000) {
            spawn.last_touch_ms = now;
            if (hooks.random_percent && hooks.random_percent() > 0x32) r.use = TouchResult::Use{0, 0};
        }
    } else if (sw.type == 0x39) {
        if (now - spawn.last_touch_ms > 2000) {
            spawn.last_touch_ms = now;
            if (spawn.spawn_type == 0 && spawn.has_inventory) {
                std::int32_t key = spawn.cursor_item_id.value_or(-1);
                std::int32_t zone = hooks.zone_point_zone ? hooks.zone_point_zone(sw.param) : -1;
                bool s3d = hooks.zone_s3d_exists && hooks.zone_s3d_exists(zone);
                bool eqg = !s3d && hooks.zone_eqg_exists && hooks.zone_eqg_exists(zone);
                if (s3d || eqg || sw.adventure_id > 0 || sw.dyn_id != 0 || sw.has_script != 0)
                    r.use = TouchResult::Use{spawn.spawn_id, key};
            }
        }
    } else if (sw.type == 0x32 && spawn.spawn_type == 0) {
        if (sw.state != 0) {
            if (now - spawn.last_touch_ms > sw.repop_ms) r.close_now = true;
        } else {
            r.use = TouchResult::Use{spawn.spawn_id, -1};
            spawn.last_touch_ms = now;
        }
    }
    return r;
}

} // namespace nmsc::game::mechanics
