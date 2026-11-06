// The usable spell gems on a stock server (no server extension): CanUseMemorizedSpellSlot @0x433bc0 over the
// connection's AA cache, re-evaluated as the owned AAs change (GameConnection::update_usable_gems).
#include <doctest/doctest.h>

#include "game/alt_adv.h"
#include "game/mechanics/character.h"
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

TEST_CASE("with_character: the reused sheet follows the inventory, and a call from inside a call has its own") {
    // with_character builds into one kept sheet (no per-frame copy of every item's storage); a nested call must not
    // rebuild the sheet the outer character is reading.
    mq_test::World w("nmsc_usable_gems_nested_test");
    net::GameConnection& c = *w.conn;
    net::Item ring;
    ring.name = "Ring of the Test";
    REQUIRE(A::inventory(c).put(net::ItemSlot::possessions(15), ring));
    int outer_seen = 0, inner_seen = 0;
    A::with_character(c, [&](game::mechanics::Character& outer) {
        A::inventory(c).take(net::ItemSlot::possessions(15));
        A::with_character(c, [&](game::mechanics::Character& inner) { inner_seen = inner.sheet().item(15) ? 1 : 2; });
        outer_seen = outer.sheet().item(15) && outer.sheet().item(15)->name == "Ring of the Test" ? 1 : 2;
    });
    CHECK(inner_seen == 2); // the ring is gone for the inner one
    CHECK(outer_seen == 1); // the outer one's sheet is as it was built
    A::with_character(c, [&](game::mechanics::Character& ch) { outer_seen = ch.sheet().item(15) ? 1 : 2; });
    CHECK(outer_seen == 2); // the kept sheet, built again, has no ring
}
