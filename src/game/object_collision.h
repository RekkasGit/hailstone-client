#pragma once

// Collision with movable objects (doors, lifts, platforms): an object's solid
// triangles in its own (model) space, queried through its current transform.
// Unlike the static zone mesh (game/collision.h) nothing is rebuilt when a
// door moves; the query is transformed into the object's space instead.

#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "game/swept_sphere.h"

namespace nmsc::assets { struct Mesh; }

namespace nmsc::game {

struct ObjectShape {
    std::vector<glm::vec3> tris; // solid triangles, 3 vertices each, model space (sight and floor probes)
    // Every non-passable triangle as a collision record (game/swept_sphere.h),
    // model space, for the movement sweep (CActorData::FindIntersectionSphere).
    std::vector<CollisionTriangle> records;
    glm::vec3 lo{0.0f}, hi{0.0f}; // bounds of the solid triangles
    float radius = 0.0f;          // largest vertex distance from the model origin

    // The solid parts of a mesh, by the rule game/collision.cpp uses for the
    // zone (everything but translucent and additive surfaces).
    static ObjectShape from_mesh(const assets::Mesh& mesh);
    bool empty() const { return tris.empty(); }
};

// CActorData::FindIntersectionSphere for a placed shape: the sweep moved into
// model space (the inverse transform; the radius divided by `scale`), its
// triangles tested there; a hit at t <= the sweep's t sets t, collided and
// the normal (back in the world). Returns true on a hit.
bool sweep_sphere(const ObjectShape& shape, const glm::mat4& transform, const glm::mat4& inverse, float scale,
                  SphereSweep& sweep);

// The first hit of the world segment a -> b with the shape placed by a
// transform whose inverse is given, as a fraction 0..1 of the segment.
std::optional<float> first_hit(const ObjectShape& shape, const glm::mat4& inverse, const glm::vec3& a,
                               const glm::vec3& b);

// True when the segment a -> b passes through the placed shape (hits
// strictly inside the segment, as CollisionMesh::line_of_sight).
bool blocks(const ObjectShape& shape, const glm::mat4& transform, const glm::mat4& inverse, float scale,
            const glm::vec3& a, const glm::vec3& b);

// The highest surface of the placed shape under (x, y) no higher than
// z + step_up, as CollisionMesh::floor_below.
std::optional<float> floor_under(const ObjectShape& shape, const glm::mat4& inverse, const glm::vec3& at,
                                 float step_up);


} // namespace nmsc::game
