#include "app/player_movement.h"

#include <cmath>

#include "game/sine_table.h"

// Ladder_MountClickedLadder (game/mechanics/ladder.h) on the player's body.

namespace hailstone::app {

namespace {

namespace mech = game::mechanics;

class BodyLadder final : public mech::LadderQueries {
public:
    BodyLadder(const mech::LadderSwitch& l, const MoveWorld* world, float radius, float avatar)
        : ladder_(l), world_(world), radius_(radius), avatar_(avatar) {}
    double actor_height() override { return radius_; } // the actor's vf44
    double ladder_height() override { return ladder_.height; }
    double sine(float h) override { return game::cmath_sin(h); }
    double cosine(float h) override { return game::cmath_cos(h); }
    double root(float v) override { return static_cast<float>(std::sqrt(static_cast<double>(v))); }
    void too_far() override { too_far_ = true; }
    // PlayerZoneClient::CanIFitHere (not rewritten; as PlayerCollision's): a floor under the spawn's z and room
    // for twice AvatarHeight above it.
    bool can_i_fit_here(float x, float y, float z) override {
        if (!world_) return true;
        auto floor = world_->floor_below({x, y, z}, 0.0f);
        if (!floor) return false;
        auto ceiling = world_->ceiling_above({x, y, *floor + 0.01f});
        return !ceiling || *ceiling >= *floor + 2.0f * avatar_;
    }
    void update_player_actor() override { moved_ = true; }
    double floor_height(float x, float y, float z, std::uint32_t) override {
        if (!world_) return -1e27f;
        auto floor = world_->floor_below({x, y, z}, 0.0f);
        return floor ? static_cast<double>(*floor) : static_cast<double>(-1e27f);
    }
    bool too_far_ = false, moved_ = false;

private:
    mech::LadderSwitch ladder_;
    const MoveWorld* world_;
    float radius_, avatar_;
};

} // namespace

bool PlayerMovement::mount_ladder(const mech::LadderSwitch& ladder, const MoveWorld* world, float avatar_height,
                                  bool& too_far) {
    mech::PhysicsBody& b = ctl_.body;
    mech::LadderPlayer p;
    p.x = b.feet.x;
    p.y = b.feet.y;
    p.z = b.feet.z + avatar_height; // the spawn's z
    p.heading = b.heading;
    p.movement_mode = b.colliding_type;
    p.speed_x = b.velocity.x;
    p.speed_y = b.velocity.y;
    p.speed_z = b.velocity.z;
    p.speed_run = b.speed_run;
    p.pitch = b.camera_angle;
    p.floor_height = b.floor_height;
    p.avatar_height = avatar_height;
    BodyLadder q(ladder, world, radius_, avatar_height);
    bool done = mech::mount_clicked_ladder(ladder, p, q);
    too_far = q.too_far_;
    if (!done) return false;
    b.feet = glm::vec3(p.x, p.y, p.z - avatar_height);
    b.heading = p.heading;
    b.colliding_type = p.movement_mode;
    b.velocity = glm::vec3(p.speed_x, p.speed_y, p.speed_z);
    b.speed_run = p.speed_run;
    b.camera_angle = p.pitch;
    b.floor_height = p.floor_height;
    collision_.colliding_type = p.movement_mode;
    if (q.moved_) actor_valid_ = false;
    return true;
}

} // namespace hailstone::app
