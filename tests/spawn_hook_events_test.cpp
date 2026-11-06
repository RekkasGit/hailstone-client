// OnAddSpawn / OnRemoveSpawn from the connection's spawn list (GameConnection::raise_hook_events): each change once,
// and nothing on a frame that changed nothing.
#include <doctest/doctest.h>

#include <utility>
#include <vector>

#include "mq_test_world.h"
#include "plugin/hooks.h"

using namespace nmsc;

TEST_CASE("Spawn hooks: each add and remove once; a list as large as before with another spawn in it is a change") {
    plugin::Hooks hooks;
    net::ConnectionOptions options;
    options.hooks = &hooks;
    mq_test::World w("nmsc_spawn_hook_events_test", options);
    using A = net::GameConnectionTestAccess;
    auto& spawns = A::world(*w.conn).spawns;
    std::vector<std::pair<std::uint32_t, bool>> got;
    auto owner = hooks.new_owner("test");
    hooks.on_spawn(owner, [&](std::uint32_t id, bool added) { got.push_back({id, added}); });

    A::raise_hook_events(*w.conn); // what the world holds now comes as added
    const std::size_t first = got.size();
    CHECK(first == spawns.size());
    got.clear();

    spawns[5001].id = 5001;
    spawns[5002].id = 5002;
    A::raise_hook_events(*w.conn);
    CHECK(got == std::vector<std::pair<std::uint32_t, bool>>{{5001, true}, {5002, true}});
    got.clear();

    for (int i = 0; i < 3; ++i) A::raise_hook_events(*w.conn); // no change: nothing
    CHECK(got.empty());

    // The same count: 5001 went and 5003 came between two frames.
    spawns.erase(5001);
    spawns[5003].id = 5003;
    A::raise_hook_events(*w.conn);
    CHECK(got == std::vector<std::pair<std::uint32_t, bool>>{{5001, false}, {5003, true}});
    got.clear();
    A::raise_hook_events(*w.conn);
    CHECK(got.empty());

    spawns.erase(5002);
    A::raise_hook_events(*w.conn);
    CHECK(got == std::vector<std::pair<std::uint32_t, bool>>{{5002, false}});
}
