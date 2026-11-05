#pragma once

// What the MQ side (/nav, /moveto, the Navigation TLO: mq/commands.cpp, mq/tlo_game.cpp) asks of the client's
// navigation (app/navigator.h), without knowing the client.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include "nav/nav_mesh.h"

namespace nmsc::nav {

struct NavStatus {
    bool mesh_loaded = false; // some of the zone's mesh is there
    bool mesh_complete = false;
    std::size_t tiles_done = 0, tiles_total = 0;
    bool active = false;      // a goal is being walked to
    bool paused = false;
    std::string goal;         // what is being walked to, for the chat and the TLO
    float remaining = 0.0f;   // the flat distance left along the path
    // /stick (NavControl::stick): the spawn kept to, 0 none, and how near.
    std::uint32_t stick_spawn = 0;
    float stick_distance = 0.0f;
};

// /stick's options (MQ2MoveUtils' words, mq/nav_commands.cpp).
struct StickOptions {
    float distance = 0.0f; // flat; 0: inside the player's melee range of the spawn (Navigator::kStickMeleeShare)
    bool behind = false;   // stand at its back (within 45 degrees of straight behind it)
};

class NavControl {
public:
    virtual ~NavControl() = default;
    // Each returns "" when the walk started, or why not (for the chat).
    // A point, world coordinates; done within `distance` (flat) of it. `straight`: no mesh, walk straight at it
    // (/moveto).
    virtual std::string go_to_point(const glm::vec3& world, float distance, std::string label, bool straight = false) = 0;
    // A spawn, followed as it moves.
    virtual std::string go_to_spawn(std::uint32_t id, float distance) = 0;
    // A door or switch; `click` uses it on arrival.
    virtual std::string go_to_door(std::uint8_t id, bool click) = 0;
    // Keeps to a spawn until unstick, the spawn goes, or the player moves: walks (over the mesh when it is far or out
    // of sight) whenever it is further than the distance, and faces it while near enough. A walk ends it.
    virtual std::string stick(std::uint32_t id, const StickOptions& options) = 0;
    virtual void unstick(std::string_view why) = 0;
    // Ends the walk and the stick.
    virtual void stop(std::string_view why) = 0;
    virtual void set_paused(bool paused) = 0;
    virtual NavStatus status() const = 0;
    // The path's length from the player to a point over the mesh; nullopt when there is no path.
    virtual std::optional<float> path_length(const glm::vec3& world) const = 0;
    virtual PathOptions& options() = 0;
    // Builds the zone's mesh again from its geometry, ignoring the cache.
    virtual void rebuild() = 0;
    virtual void set_overlay(bool on) = 0;
    virtual bool overlay() const = 0;
};

} // namespace nmsc::nav
