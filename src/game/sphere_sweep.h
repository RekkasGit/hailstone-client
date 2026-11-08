#pragma once

// The swept-sphere geometry of a movement sweep: a sphere of radius r whose
// centre moves from c along d (t is a fraction of d), tested against one
// triangle, and the two results s3dCollideActorSweep @0x476560 hands back
// (where the sphere stops, and the slide). Hailstone's own implementation of
// the behaviour the original's routines are described to have
// (CCollisionInfoSphere::vf0 @0x7a28e0 with SweptSphereTriangle @0x7a6770 and
// its edge @0x7a9220, edge-region @0x7a63c0 and vertex @0x7a65d0 cases,
// GetClampPoint @0x7a2680, ComputeSkitter @0x7a2720). It is not a copy of
// them: the operations are ordered and rounded as reads naturally, so the
// results agree with the original's recordings within a small tolerance
// rather than bit for bit (tests/movement_vectors_test.cpp counts how many
// need it; docs/unknowns.md).
//
// The scalar F is float in game (the original ran with a 24-bit FPU control
// word, which is single precision) and double for recordings made at the
// FPU's 53-bit default.
//
// The cases, for one triangle A B C with record normal n (unit),
// edges e0 = B - A, e1 = C - B, e2 = A - C and N = e1 x e0:
//  - a sweep of zero length is an overlap test: a hit at t = 0 with normal n
//    when the centre is closer than r to the triangle;
//  - otherwise the sphere's point nearest the plane, P = c -+ r n, must move
//    toward the plane. When the sphere starts clear of the plane, P's crossing
//    point Q is classified against the three edge planes: inside all three is
//    a face hit; outside one, a sweep against that edge's capsule; outside two,
//    a sweep against their shared vertex. When the sphere already crosses the
//    plane the centre is classified instead: inside is a hit at t = 0;
//    outside one edge, that edge's capsule; outside two, the edge region of
//    their vertex picks one of its two edges;
//  - the hit normal is normalize(c + d t - contact).
// The original's edge test, when the sphere already holds the edge's start
// vertex, reports t = 0 without a contact point (its normal is then whatever
// memory held); here the contact is that vertex, and the hit says so
// (`contact_assumed`).

#include <glm/glm.hpp>

namespace hailstone::game::sweep {

template <class F>
using Vec3 = glm::vec<3, F, glm::defaultp>;

// v scaled to length 1, or zero when its length is below 2^-23 (Vec3_Normalize @0x441bb0 and 0x509130).
template <class F>
Vec3<F> normalize_or_zero(const Vec3<F>& v);

// CCollisionInfoBase(from, to) @0x441c90 and the sphere fields InitFromActor @0x476ab0 sets.
template <class F>
struct Start {
    Vec3<F> delta{0};       // to - from
    Vec3<F> direction{0};   // delta normalised, zero below 2^-23
    F length_inverse = 0;   // 1 / |delta|, 0 unless |delta| > 0
    F radius_squared = 0;
    F radius_inverse = 0;
};
template <class F>
Start<F> start(const Vec3<F>& from, const Vec3<F>& to, F radius);

// One triangle against the sphere at `centre` moving by `delta`; `t_max` is
// the sweep's current t, `length_inverse` 0 marks a zero-length sweep.
template <class F>
struct TriangleHit {
    bool hit = false;
    F t = 0;
    Vec3<F> normal{0};
    bool contact_assumed = false; // the edge case above: the original's normal is undefined
};
template <class F>
TriangleHit<F> sphere_triangle(const Vec3<F>& centre, const Vec3<F>& delta, F length_inverse, F t_max, F radius,
                               const Vec3<F>& n, const Vec3<F>& v0, const Vec3<F>& v1, const Vec3<F>& v2);

// GetClampPoint: the sphere stops 0.001 short of delta * t along the path.
template <class F>
Vec3<F> clamp_point(const Vec3<F>& from, const Vec3<F>& delta, F t);

// ComputeSkitter: after a hit with `normal` at t, the slide direction
// normalize(normalize(u - n (u . n)) + 0.01 n) and its distance
// max((1 - t) |delta| |s . u|, 0.0001); zero when u lies along n.
template <class F>
struct Skitter {
    Vec3<F> direction{0};
    F distance = 0;
};
template <class F>
Skitter<F> skitter(const Vec3<F>& direction, const Vec3<F>& delta, F t, const Vec3<F>& normal);

} // namespace hailstone::game::sweep
