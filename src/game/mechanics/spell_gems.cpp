// Spell gems (game/mechanics/spell_gems.h).
#include "game/mechanics/spell_gems.h"

#include "core/wrap_math.h"

namespace hailstone::game::mechanics::gems {

std::int32_t get_spell_mem_ticks_left(MemorizeBook& book, MemorizeWorld& w, MemorizeQueries& q) {
    if (book.gem == -1) return 0;
    std::int32_t step = 1;
    const std::int32_t needed = q.spell_level_needed(book.spell, w.class_id);
    std::int32_t interval = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(needed - q.level() + 5) * 100u); // imul (wraps)
    if (interval < 100) interval = 100;
    if (w.now - w.last_tick > static_cast<std::uint32_t>(interval)) {
        w.last_tick = w.now;
        const bool bard = w.class_id == 8;
        if (bard) {
            // DEX, then INT, as the original calls them: `a() + b()` leaves the order to the compiler, and MSVC
            // called INT first (the recorded calls' order failed only in its build).
            const std::int32_t dex = q.calc_dex();
            const std::int32_t intel = q.calc_int();
            std::int32_t sum =
                static_cast<std::int32_t>(static_cast<std::uint32_t>(dex) + static_cast<std::uint32_t>(intel));
            step = sum / 30 + 1;
        }
        std::int32_t meditate = q.adjusted_skill(31);
        if (meditate < 10) meditate = 10;
        if (bard && q.has_alternate_ability(kAaMemorizeBoost)) step += step;
        else step = meditate / 10 + step + 2;
        book.ticks_left = static_cast<std::int32_t>(static_cast<std::uint32_t>(book.ticks_left) -
                                                    static_cast<std::uint32_t>(step));
        if (book.ticks_left < 0) book.ticks_left = 0;
        if (book.ticks_left == 0) {
            ++w.item_pending;
            if (w.pending_reasons < 20) q.push_pending_reason();
            if (w.connected) {
                MemorizePacket p;
                p.slot = book.gem;
                p.spell_id = book.spell;
                p.scribing = 1;
                if (!q.send(p)) q.close_connection();
            } else if (!w.quiet_net_log) {
                q.debug_log();
            }
            w.last_tick = 0;
            q.cursor_attachment_vf94();
            book.gem = -1;
            book.spell = -1;
            return 0;
        }
    }
    return core::wrapped_div(static_cast<std::uint32_t>(book.ticks_left) * 1000u, kMemorizeTicks);
}

void finish_memorizing(std::int32_t& mode, bool keep_open, std::uint32_t gem, std::int32_t spell_id, std::uint32_t now,
                       FinishQueries& q) {
    q.set_memorized(gem, spell_id);
    q.say(kEqstrFinishedMemorizing, spell_id, kMemorizeColor);
    auto recast = static_cast<std::uint32_t>(q.recast_time(spell_id));
    q.set_gem_timer(gem, recast + now + 1000);
    if (mode == 1) {
        if (!keep_open) q.hide_book();
        mode = 0;
        return;
    }
    if (mode == 2) {
        q.continue_set_mem();
        return;
    }
    if (q.cast_window()) q.update_spell_gems(-1);
}

namespace {

void answered(std::int32_t& pending, MemorizeHandlerQueries& q) {
    q.pop_pending_reason();
    pending = pending > 0 ? pending - 1 : 0;
}

void start_item_recast(std::uint32_t now, MemorizeHandlerQueries& q) {
    if (!q.local_pc()) return;
    if (!q.is_cast_item_valid()) return;
    q.get_cast_item();
    if (q.item_cast_type() != 0) return;
    if (q.item_recast_time(0) <= 0) return;
    std::uint32_t ready = static_cast<std::uint32_t>(q.item_recast_time(0)) + now;
    std::uint32_t old = q.item_ready_time();
    q.set_item_ready_time(ready, ready != old);
}

} // namespace

std::int32_t handle_op_memorize_spell(std::int32_t scribing, std::int32_t slot, std::int32_t spell_id,
                                      std::int32_t reduction, std::uint32_t now, std::int32_t& item_pending,
                                      MemorizeHandlerQueries& q) {
    switch (scribing) {
    case -1:
        answered(item_pending, q);
        return 1;
    case 0:
    case 1:
        answered(item_pending, q);
        if (q.book_window()) {
            if (scribing == 0) q.finish_scribing(slot, spell_id);
            else q.finish_memorizing(slot, spell_id);
        }
        return 1;
    case 2:
        answered(item_pending, q);
        if (q.cast_window()) {
            q.forget_memorized_spell(slot);
            return 1;
        }
        if (q.local_pc()) q.set_memorized_spell(slot, -1);
        return 1;
    case 3:
        break;
    default:
        return 1;
    }
    if (!q.local_player()) return 1;
    start_item_recast(now, q);
    const std::int32_t gem = slot;
    if (gem > 0xb && gem != 0xc) return 1;
    if (!q.is_valid_spell_id(spell_id)) return 1;
    auto times = q.spell_times(spell_id);
    if (!times) return 1;
    auto [recast_time, recovery_time] = *times;
    auto recast = static_cast<std::int32_t>(static_cast<std::uint32_t>(recast_time) - static_cast<std::uint32_t>(reduction));
    if (gem == 0xc) {
        std::int32_t total = q.total_effect(0x127);
        recast = static_cast<std::int32_t>(static_cast<std::uint32_t>(recast_time) - static_cast<std::uint32_t>(total) * 1000u);
    }
    q.set_gem_timer(gem, now + static_cast<std::uint32_t>(recast));
    if (gem > 0xb) return 1;
    q.set_cast_slot(0xff);
    const bool bard = q.class_id() == 8;
    std::uint32_t ready = now;
    if (!bard) ready += static_cast<std::uint32_t>(recovery_time);
    q.set_recovery(ready);
    if (q.class_id() != 8) return 1;
    std::int32_t next = q.next_bard_twist_gem();
    if (static_cast<std::uint32_t>(next) - 1u > 0xb) return 1;
    q.stop_current_spell_cast(0);
    q.cast_gem(next - 1);
    return 1;
}

bool is_linked_spell_timer_ready(std::optional<std::int32_t> reuse_index, std::uint32_t now_unix,
                                 const std::function<std::uint32_t(std::int32_t)>& linked_timer) {
    if (!reuse_index) return false;
    std::int32_t index = *reuse_index;
    if (static_cast<std::uint32_t>(index) - 1u > 0x12) return true;
    return linked_timer(index) <= now_unix;
}

void set_linked_spell_reuse_timer(std::array<std::uint32_t, 20>& timers, std::uint32_t index, std::uint32_t ready) {
    if (index > 0x13) return;
    timers[index] = ready;
}

bool can_use_memorized_spell_slot(bool has_profile, std::int32_t profile_340c, std::int32_t gem,
                                  const SpellSlotTotals& totals) {
    if (!has_profile || profile_340c != 0) return true;
    auto total = static_cast<std::uint32_t>(totals.aa ? totals.aa() : 0);
    total += static_cast<std::uint32_t>(totals.buff ? totals.buff() : 0);
    total += static_cast<std::uint32_t>(totals.item ? totals.item() : 0);
    if (gem < 8) return true;
    return gem - 8 < static_cast<std::int32_t>(total);
}

} // namespace hailstone::game::mechanics::gems
