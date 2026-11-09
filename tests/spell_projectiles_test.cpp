// game/spell_projectiles.h: which spells fly or fall, and the projectiles timed to the server's landings.
#include <doctest/doctest.h>

#include <cmath>

#include "game/spell_projectiles.h"
#include "game/spells.h"

using namespace hailstone::game;

namespace {

SpellData nuke(const char* name, std::uint8_t target_type, std::uint8_t resist) {
    SpellData s;
    s.name = name;
    s.target_type = target_type;
    s.resist = resist;
    s.effect.fill(254);
    s.effect[0] = 0; // current HP
    s.base[0] = -100;
    return s;
}

// Caster at the origin, target 80 units along x.
ProjectileWorld world() {
    ProjectileWorld w;
    w.hands = [](std::uint32_t id, const glm::vec3&) -> std::optional<glm::vec3> {
        if (id == 1) return glm::vec3(0, 0, 4);
        return std::nullopt;
    };
    w.chest = [](std::uint32_t id) -> std::optional<glm::vec3> {
        if (id == 2) return glm::vec3(80, 0, 3);
        return std::nullopt;
    };
    w.feet = [](std::uint32_t id) -> std::optional<glm::vec3> {
        if (id == 2) return glm::vec3(80, 0, 0);
        return std::nullopt;
    };
    return w;
}

void run(SpellProjectiles& p, double from, double to, const ProjectileWorld& w) {
    for (double t = from; t <= to + 1e-9; t += 1.0 / 60.0) p.update(t, w);
}

} // namespace

TEST_CASE("projectile styles from the spell's target, resist and name") {
    auto s = projectile_style(nuke("Fire Bolt", kTargetSingle, 2));
    CHECK(s.form == ProjectileForm::Bolt);
    CHECK(s.element == SpellElement::Fire);
    CHECK_FALSE(s.server_missile);
    s = projectile_style(nuke("Ice Comet", kTargetSingle, 3));
    CHECK(s.form == ProjectileForm::Comet);
    CHECK(s.element == SpellElement::Cold);
    s = projectile_style(nuke("Lightning Bolt", kTargetSingle, 1));
    CHECK(s.element == SpellElement::Lightning);
    s = projectile_style(nuke("Rain of Fire", kTargetAETarget, 2));
    CHECK(s.form == ProjectileForm::Rain);
    s = projectile_style(nuke("Bolt of Flame", kTargetOptional, 2));
    CHECK(s.form == ProjectileForm::Bolt);
    CHECK(s.server_missile);
    // Not projectiles: a heal, damage over time, a point-blank area, a lifetap.
    auto heal = nuke("Light Healing", kTargetSingle, 0);
    heal.spell_type = 1;
    heal.base[0] = 100;
    CHECK(projectile_style(heal).form == ProjectileForm::None);
    auto dot = nuke("Flame Lick", kTargetSingle, 2);
    dot.duration_formula = 3;
    dot.duration_cap = 6;
    CHECK(projectile_style(dot).form == ProjectileForm::None);
    CHECK(projectile_style(nuke("Fire Flux", kTargetAECaster, 2)).form == ProjectileForm::None);
    CHECK(projectile_style(nuke("Lifetap", kTargetTap, 1)).form == ProjectileForm::None);
}

TEST_CASE("the player's bolt leaves in time to arrive as the cast ends, and bursts on the landing") {
    SpellProjectiles p;
    auto w = world();
    auto style = projectile_style(nuke("Fire Bolt", kTargetSingle, 2));
    p.own_cast(77, style, 1, 2, 0.0, 2.0);
    run(p, 0.0, 1.0, w);
    REQUIRE(p.projectiles().size() == 1);
    CHECK(p.projectiles()[0].phase == Projectile::Phase::Waiting);
    run(p, 1.0 + 1.0 / 60.0, 1.9, w);
    CHECK(p.projectiles()[0].phase == Projectile::Phase::Flying);
    CHECK(p.projectiles()[0].head.x > 0.0f);
    run(p, 1.9 + 1.0 / 60.0, 2.05, w);
    // There before the server's word: it waits.
    CHECK(p.projectiles()[0].phase == Projectile::Phase::Holding);
    CHECK(p.landed(77, style, 1, 2, 2.05) == 0.0);
    CHECK(p.projectiles()[0].phase == Projectile::Phase::Burst);
    CHECK_FALSE(p.sparks().empty());
    run(p, 2.06, 3.5, w);
    CHECK(p.projectiles().empty());
}

TEST_CASE("an interrupted cast's bolt never leaves; one that never lands fizzles") {
    SpellProjectiles p;
    auto w = world();
    auto style = projectile_style(nuke("Fire Bolt", kTargetSingle, 2));
    p.own_cast(77, style, 1, 2, 0.0, 3.0);
    run(p, 0.0, 0.5, w);
    p.own_cast_ended(1, 0.5);
    CHECK(p.projectiles().empty());

    p.own_cast(77, style, 1, 2, 10.0, 1.0);
    run(p, 10.0, 11.0 + SpellProjectiles::kHold + 0.05, w);
    REQUIRE(p.projectiles().size() == 1);
    CHECK(p.projectiles()[0].phase == Projectile::Phase::Fizzle);
    run(p, 11.8, 12.5, w);
    CHECK(p.projectiles().empty());
}

TEST_CASE("another's bolt leaves on the landing, and the impact waits for it") {
    SpellProjectiles p;
    auto w = world();
    auto style = projectile_style(nuke("Fire Bolt", kTargetSingle, 2));
    double wait = p.landed(77, style, 1, 2, 0.0, 80.0f);
    CHECK(wait == doctest::Approx(80.0 / SpellProjectiles::kBoltSpeed));
    run(p, 0.0, wait - 0.05, w);
    REQUIRE(p.projectiles().size() == 1);
    CHECK(p.projectiles()[0].phase == Projectile::Phase::Flying);
    run(p, wait - 0.05 + 1.0 / 60.0, wait + 0.05, w);
    CHECK(p.projectiles()[0].phase == Projectile::Phase::Burst);
    CHECK(p.projectiles()[0].head.x == doctest::Approx(80.0f).epsilon(0.02));
}

TEST_CASE("a comet falls onto its target's foot; a rain drops three") {
    SpellProjectiles p;
    auto w = world();
    auto comet = projectile_style(nuke("Ice Comet", kTargetSingle, 3));
    double wait = p.landed(78, comet, 1, 2, 0.0, 80.0f);
    CHECK(wait == doctest::Approx(SpellProjectiles::kCometFall));
    run(p, 0.0, 0.2, w);
    REQUIRE(p.projectiles().size() == 1);
    CHECK(p.projectiles()[0].head.z > 20.0f);
    run(p, 0.2 + 1.0 / 60.0, wait + 0.05, w);
    CHECK(p.projectiles()[0].phase == Projectile::Phase::Burst);
    CHECK(p.projectiles()[0].head.z == doctest::Approx(0.0f).epsilon(0.05));

    SpellProjectiles r;
    r.landed(79, projectile_style(nuke("Rain of Fire", kTargetAETarget, 2)), 1, 2, 0.0, 80.0f);
    CHECK(r.projectiles().size() == 3);
}

TEST_CASE("a server-flown bolt follows its missile and bursts where it ends") {
    SpellProjectiles p;
    auto style = projectile_style(nuke("Bolt of Flame", kTargetOptional, 2));
    p.cast_begun(80, style, 1, 0.0);
    // No own-cast projectile for it: the server's missile shows the flight.
    p.own_cast(80, style, 1, 2, 0.0, 2.0);
    CHECK(p.projectiles().empty());
    CHECK(p.follow_missile(5, 1, 2, 2.0));
    CHECK(p.follows(5));
    glm::vec3 at(10, 0, 4);
    bool flying = true;
    auto w = world();
    w.missile = [&](std::uint32_t serial) -> std::optional<glm::vec3> {
        if (serial == 5 && flying) return at;
        return std::nullopt;
    };
    p.update(2.0, w);
    CHECK(p.projectiles()[0].head.x == doctest::Approx(10.0f));
    at = {50, 0, 4};
    p.update(2.1, w);
    flying = false;
    p.update(2.2, w);
    CHECK(p.projectiles()[0].phase == Projectile::Phase::Burst);
    CHECK(p.projectiles()[0].head.x == doctest::Approx(50.0f));
    CHECK(p.landed(80, style, 1, 2, 2.2) == 0.0);
    // Another's missile with no remembered cast of a server-flown spell is not followed.
    CHECK_FALSE(p.follow_missile(6, 9, 2, 2.2));
}

TEST_CASE("projectiles: a distance or time that is not a number gives the shortest flight, not a NaN") {
    ProjectileStyle style;
    CHECK(SpellProjectiles::bolt_flight(style, std::nanf("")) == SpellProjectiles::kMinFlight);
    CHECK(SpellProjectiles::bolt_flight(style, INFINITY) == SpellProjectiles::kMaxFlight);
    Projectile p;
    p.launch_at = 1.0;
    p.arrive_at = std::nan("");
    CHECK(SpellProjectiles::flight(p, 1.5) == 1.0f);
    p.arrive_at = 2.0;
    CHECK(SpellProjectiles::flight(p, std::nan("")) == 1.0f);
    CHECK(SpellProjectiles::flight(p, 1.5) == doctest::Approx(0.5f));
}
