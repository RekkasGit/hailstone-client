// The usable spell gems on a stock server (no server extension): CanUseMemorizedSpellSlot @0x433bc0 over the
// connection's AA cache, re-evaluated as the owned AAs change (GameConnection::update_usable_gems).
#include <doctest/doctest.h>

#include "game/alt_adv.h"
#include "mq_test_world.h"

using namespace nmsc;

namespace {

using A = net::GameConnectionTestAccess;

// A rank of Mnemonic Retention: SPA 326 (SpellSlotIncrease) with `extra` gems, for a cleric (class 2).
game::AltAbility mnemonic_retention(std::uint32_t rank_id, std::int32_t extra) {
    game::AltAbility a;
    a.id = rank_id;
    a.classes = 1u << 2;
    a.level_req = 1;
    a.effects.push_back({326, extra, 0, 1});
    return a;
}

} // namespace

TEST_CASE("a stock server's character has 8 gems, and 8 + SPA 326 as Mnemonic Retention ranks are owned and lost") {
    mq_test::World w("nmsc_usable_gems_test");
    net::GameConnection& c = *w.conn;
    game::AltAdvState& aa = A::alt_adv(c);
    aa.add(mnemonic_retention(1071, 1));
    aa.add(mnemonic_retention(4764, 2));
    aa.add(mnemonic_retention(7681, 4));

    A::update_usable_gems(c);
    CHECK(c.usable_gems() == 8);

    aa.set_owned(0, {}, {{1071, 1, 0}}); // OP_RespondAA: rank 1
    A::update_usable_gems(c);
    CHECK(c.usable_gems() == 9);

    aa.set_owned(0, {}, {{4764, 2, 0}}); // trained to rank 2
    A::update_usable_gems(c);
    CHECK(c.usable_gems() == 10);

    aa.set_owned(0, {}, {{7681, 4, 0}}); // rank 4: all 12
    A::update_usable_gems(c);
    CHECK(c.usable_gems() == 12);

    aa.clear_owned(); // OP_ClearAA
    A::update_usable_gems(c);
    CHECK(c.usable_gems() == 8);
}
