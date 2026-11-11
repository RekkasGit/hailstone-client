#include "game/sphere_sweep.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace hailstone::game::sweep {

namespace {

template <class F>
constexpr F kTiny = static_cast<F>(0x1p-23); // the normalisers' and the vertex test's zero
template <class F>
constexpr F kStopShort = static_cast<F>(0.001f); // GetClampPoint's gap before the contact
template <class F>
constexpr F kPushOff = static_cast<F>(0.01f); // ComputeSkitter: the slide leans this much off the surface
template <class F>
constexpr F kMinSlide = static_cast<F>(0.0001f);
template <class F>
constexpr F kEdgeSlack = static_cast<F>(0.01f); // the edge test's still-sphere case
template <class F>
constexpr F kParallel = static_cast<F>(0.9999999f); // the capsule test treats the move as along the axis

template <class F>
F dot(const Vec3<F>& a, const Vec3<F>& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// |v|, its squares summed in double: the x87's registers have a wider exponent than a float, so the original's
// lengths of vectors near the float range (1e19 and up) do not overflow, and neither do these.
template <class F>
F length(const Vec3<F>& v) {
    double x = static_cast<double>(v.x), y = static_cast<double>(v.y), z = static_cast<double>(v.z);
    return static_cast<F>(std::sqrt(x * x + y * y + z * z));
}

template <class F>
Vec3<F> cross(const Vec3<F>& a, const Vec3<F>& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// A hit of the sphere's centre path: t, and the point of the triangle it touches.
template <class F>
struct Contact {
    F t = 0;
    Vec3<F> point{0};
    bool assumed = false;
};

// The squared distance from p to the segment s + e u, 0 <= u <= 1.
template <class F>
F point_segment_distance_sq(const Vec3<F>& p, const Vec3<F>& s, const Vec3<F>& e) {
    F ee = dot(e, e);
    F u = ee > 0 ? std::clamp(dot(p - s, e) / ee, F(0), F(1)) : F(0);
    Vec3<F> q = p - (s + e * u);
    return dot(q, q);
}

// The squared distance from p to the triangle a b c (the closest point by Voronoi region).
template <class F>
F point_triangle_distance_sq(const Vec3<F>& p, const Vec3<F>& a, const Vec3<F>& b, const Vec3<F>& c) {
    Vec3<F> ab = b - a, ac = c - a, ap = p - a;
    F d1 = dot(ab, ap), d2 = dot(ac, ap);
    auto at = [&](const Vec3<F>& q) {
        Vec3<F> w = p - q;
        return dot(w, w);
    };
    if (d1 <= 0 && d2 <= 0) return at(a);
    Vec3<F> bp = p - b;
    F d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return at(b);
    F vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return at(a + ab * (d1 / (d1 - d3)));
    Vec3<F> cp = p - c;
    F d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return at(c);
    F vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return at(a + ac * (d2 / (d2 - d6)));
    F va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return at(b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6))));
    F denom = va + vb + vc;
    if (!(denom != 0)) return at(a);
    return at(a + ab * (vb / denom) + ac * (vc / denom));
}

// The centre path c + d t against the sphere of radius r around v (SweptSphereVertex): the smaller root of
// |c + d t - v|^2 = r^2, kept when t <= t_max. There is no t >= 0 test: a sphere that already holds the vertex
// gives a negative t.
template <class F>
std::optional<Contact<F>> sweep_vertex(const Vec3<F>& c, const Vec3<F>& d, F r, F t_max, const Vec3<F>& v) {
    Vec3<F> q = c - v;
    Vec3<F> side = cross(d, q);
    F dd = dot(d, d);
    if (!(dot(side, side) <= dd * r * r)) return std::nullopt; // the path never comes within r
    F b = dot(d, q);
    F k = dot(q, q) - r * r;
    F disc = std::fabs(b * b - k * dd);
    if (disc < kTiny<F>) return Contact<F>{0, v};
    F root = std::sqrt(disc);
    F f = 1 - b / root;
    if (std::fabs(f) < kTiny<F>) return std::nullopt;
    F t = (k / root) / f; // k / (root - b)
    if (!(t <= t_max)) return std::nullopt;
    return Contact<F>{t, v};
}

// The centre path against the capsule of radius r around the segment s + e u (SweptSphereEdge): the first surface
// crossing that counts (below), 0 for one before the move, provided one lies within the move; the contact is the
// edge's point nearest the centre then.
template <class F>
std::optional<Contact<F>> sweep_edge(const Vec3<F>& c, const Vec3<F>& d, F r, F t_max, const Vec3<F>& s,
                                     const Vec3<F>& e) {
    F rr = r * r;
    if (d.x == 0 && d.y == 0 && d.z == 0) {
        if (point_segment_distance_sq(c, s, e) - kEdgeSlack<F> <= rr) return Contact<F>{0, c};
        return std::nullopt;
    }
    Vec3<F> q = c - s;
    if (dot(q, q) < rr) {
        // The sphere already holds the edge's start: a hit at once when moving toward it. The original leaves the
        // contact unset here; the start vertex is the point the sphere holds.
        if (dot(d, s) > dot(d, c)) return Contact<F>{0, s, true};
        return std::nullopt;
    }
    F ee = dot(e, e);
    F d_len = length(d);
    bool along_axis = !(d_len > kTiny<F>) || !(ee > 0);
    if (!along_axis) along_axis = std::fabs(dot(e, d)) / (std::sqrt(ee) * d_len) >= kParallel<F>;

    // Where along the axis (0 at s, 1 at s + e) the centre is at time t.
    auto axis_at = [&](F t) { return ee > 0 ? dot(q + d * t, e) / ee : F(0); };
    // The native capsule query generates at most two candidates, in part order rather than time order: cylinder,
    // start cap, then far cap. Its [0,1] filter counts usable candidates without compacting this time array.
    // SweptSphereEdge selects candidate zero when exactly one is usable, even when that candidate is outside the
    // move. Preserve that observable rule rather than replacing it with the earliest geometric intersection.
    std::array<F, 2> candidates{};
    std::size_t count = 0;
    auto keep = [&](F t) { if (count < candidates.size()) candidates[count++] = t; };
    // Roots of a t^2 + 2 h t + k = 0.
    auto roots = [&](F a, F h, F k, auto&& on_root) {
        if (!(a > 0)) return;
        F disc = h * h - a * k;
        if (!(disc >= 0)) return;
        F root = std::sqrt(disc);
        on_root((-h - root) / a);
        if (disc != 0) on_root((-h + root) / a);
    };
    if (along_axis && ee > 0 && d_len > 0) {
        const F edge_length = std::sqrt(ee);
        const Vec3<F> axis = e / edge_length;
        const F along = dot(q, axis);
        const Vec3<F> across = q - axis * along;
        const F cap_sq = rr - dot(across, across);
        const F direction = dot(d, axis);
        if (direction > 0 && cap_sq >= 0) {
            const F cap = std::sqrt(cap_sq);
            keep((-along - cap) / d_len);
            keep((edge_length - along + cap) / d_len);
        } else if (direction < 0 && cap_sq > 0) {
            const F cap = std::sqrt(cap_sq);
            keep((along + cap) / d_len);
            keep(-(edge_length - along + cap) / d_len);
        }
    } else if (!along_axis) {
        // The cylinder: the parts of q and d across the axis.
        Vec3<F> q_across = q - e * (dot(q, e) / ee);
        Vec3<F> d_across = d - e * (dot(d, e) / ee);
        bool cylinder_tangent = false;
        roots(dot(d_across, d_across), dot(q_across, d_across), dot(q_across, q_across) - rr, [&](F t) {
            F u = axis_at(t);
            // A cylinder tangent includes its far endpoint; the two-root case leaves that endpoint to the cap.
            const bool tangent = dot(q_across, d_across) * dot(q_across, d_across) ==
                                 dot(d_across, d_across) * (dot(q_across, q_across) - rr);
            if (u >= 0 && (u < 1 || (tangent && u == 1))) {
                keep(t);
                cylinder_tangent = tangent;
            }
        });
        const Vec3<F> q_end = c - (s + e);
        const F dd = dot(d, d);
        if (!cylinder_tangent && count < 2) roots(dd, dot(q, d), dot(q, q) - rr, [&](F t) {
            if (axis_at(t) < 0) keep(t);
        });
        if (!cylinder_tangent && count < 2) roots(dd, dot(q_end, d), dot(q_end, q_end) - rr, [&](F t) {
            if (axis_at(t) >= 1) keep(t);
        });
    }
    std::size_t usable = 0;
    for (std::size_t i = 0; i < count; ++i) if (candidates[i] >= 0 && candidates[i] <= 1) ++usable;
    if (usable == 0) return std::nullopt;
    F first = candidates[0];
    if (usable == 2) first = std::min(first, candidates[1]);
    F t = std::max(first, F(0));
    if (!(t < t_max)) return std::nullopt;
    Vec3<F> point = s + e * std::clamp(axis_at(t), F(0), F(1));
    if (!(dot(d, point) >= dot(d, c))) return std::nullopt; // the edge must lie ahead of the centre
    return Contact<F>{t, point};
}

template <class F>
std::optional<Contact<F>> sweep_triangle(const Vec3<F>& c, const Vec3<F>& d, F r, F t_max, const Vec3<F>& n,
                                         const std::array<Vec3<F>, 3>& v) {
    if (dot(n, n) == 0) return std::nullopt;
    const std::array<Vec3<F>, 3> e{v[1] - v[0], v[2] - v[1], v[0] - v[2]};
    Vec3<F> big_n = cross(e[1], e[0]);
    // The edge planes' normals point into the triangle.
    const std::array<Vec3<F>, 3> m{cross(e[0], big_n), cross(e[1], big_n), cross(e[2], big_n)};
    F plane = dot(big_n, v[0]);
    F dist = dot(big_n, c) - plane;
    F rr = dot(big_n, big_n) * r * r;
    F toward = dot(d, n);
    bool behind = !(dot(big_n, c) > plane);
    if (behind ? !(toward >= 0) : !(toward <= 0)) return std::nullopt; // moving away from the plane
    Vec3<F> p = behind ? c + n * r : c - n * r;

    auto outside = [&](const Vec3<F>& x) {
        std::array<bool, 3> out{};
        for (std::size_t i = 0; i < 3; ++i) out[i] = !(dot(m[i], x) >= dot(m[i], v[i]));
        return out;
    };
    // The vertex shared by two edges outside: B for e0 e1, A for e0 e2, C for e1 e2.
    auto shared_vertex = [](const std::array<bool, 3>& out) -> std::size_t {
        if (out[0] && out[1]) return 1;
        if (out[0] && out[2]) return 0;
        return 2;
    };
    auto count = [](const std::array<bool, 3>& out) { return int(out[0]) + int(out[1]) + int(out[2]); };
    auto lone_edge = [](const std::array<bool, 3>& out) -> std::size_t { return out[0] ? 0 : out[1] ? 1 : 2; };

    if (!(dist * dist <= rr)) {
        // Clear of the plane: where P crosses it.
        if (toward == 0) return std::nullopt;
        F t = (dot(n, v[0]) - dot(n, p)) / toward;
        Vec3<F> crossing = p + d * t;
        auto out = outside(crossing);
        switch (count(out)) {
        case 0:
            if (!(t <= t_max)) return std::nullopt;
            return Contact<F>{t, crossing};
        case 1: {
            std::size_t i = lone_edge(out);
            return sweep_edge(c, d, r, t_max, v[i], e[i]);
        }
        case 2: return sweep_vertex(c, d, r, t_max, v[shared_vertex(out)]);
        default: return std::nullopt;
        }
    }

    // Already across the plane: where the centre is.
    auto out = outside(c);
    switch (count(out)) {
    case 0: return Contact<F>{0, p};
    case 1: {
        std::size_t i = lone_edge(out);
        return sweep_edge(c, d, r, t_max, v[i], e[i]);
    }
    case 2: {
        // The edge region of vertex k: the edge before it or the one after, by where the centre's path meets the
        // plane through the vertex.
        std::size_t k = shared_vertex(out);
        Vec3<F> q = v[k] - c;
        if (dot(q, q) < r * r) return std::nullopt;
        if (!(dot(q, d) > 0)) return std::nullopt;
        // SweptSphereTriangle's edge-region selector @0x7a63c0 needs a crossing of the plane through the vertex.
        // A parallel path has no crossing; dividing by zero here instead picked an edge through NaN comparisons.
        if (toward == 0) return std::nullopt;
        F t = (dot(n, v[k]) - dot(n, c)) / toward;
        Vec3<F> x = c + d * t;
        if (!(dot(e[k], v[k]) <= dot(e[k], x))) {
            std::size_t before = (k + 2) % 3;
            return sweep_edge(c, d, r, t_max, v[before], e[before]);
        }
        return sweep_edge(c, d, r, t_max, v[k], e[k]);
    }
    default: return std::nullopt;
    }
}

} // namespace

template <class F>
Vec3<F> normalize_or_zero(const Vec3<F>& v) {
    F len = length(v);
    if (len > -kTiny<F> && len < kTiny<F>) return Vec3<F>(0);
    return v / len;
}

template <class F>
Start<F> start(const Vec3<F>& from, const Vec3<F>& to, F radius) {
    Start<F> s;
    s.delta = to - from;
    s.direction = normalize_or_zero(s.delta);
    F len = length(s.delta);
    s.length_inverse = len > 0 ? 1 / len : F(0);
    s.radius_squared = radius * radius;
    s.radius_inverse = 1 / radius;
    return s;
}

template <class F>
TriangleHit<F> sphere_triangle(const Vec3<F>& centre, const Vec3<F>& delta, F length_inverse, F t_max, F radius,
                               const Vec3<F>& n, const Vec3<F>& v0, const Vec3<F>& v1, const Vec3<F>& v2) {
    TriangleHit<F> h;
    if (length_inverse == 0) {
        // A sweep of no length: the overlap test.
        if (point_triangle_distance_sq(centre, v0, v1, v2) < radius * radius) {
            h.hit = true;
            h.t = 0;
            h.normal = n;
        }
        return h;
    }
    auto c = sweep_triangle(centre, delta, radius, t_max, n, {v0, v1, v2});
    if (!c) return h;
    h.hit = true;
    h.t = c->t;
    h.normal = normalize_or_zero(centre + delta * c->t - c->point);
    h.contact_assumed = c->assumed;
    return h;
}

template <class F>
Vec3<F> clamp_point(const Vec3<F>& from, const Vec3<F>& delta, F t) {
    Vec3<F> v = delta * t;
    F len = length(v);
    if (len <= kStopShort<F>) return from; // a NaN length goes on, so a NaN path gives a NaN stop
    return from + normalize_or_zero(v) * std::max(len - kStopShort<F>, F(0));
}

template <class F>
Skitter<F> skitter(const Vec3<F>& u, const Vec3<F>& delta, F t, const Vec3<F>& n) {
    Skitter<F> k;
    Vec3<F> along = u - n * dot(u, n);
    F len = length(along);
    Vec3<F> s = normalize_or_zero(along);
    // No slide when u lies along n: the length below the normaliser's zero counts as none (as do NaN lengths).
    if (!(len >= kTiny<F>)) {
        k.direction = s;
        return k;
    }
    F distance = std::fabs(dot(s, u)) * length(delta) * (1 - t);
    k.direction = normalize_or_zero(s + n * kPushOff<F>);
    k.distance = distance > kMinSlide<F> ? distance : kMinSlide<F>; // NaN gives the minimum
    return k;
}

#define HAILSTONE_SWEEP_INSTANTIATE(F)                                                                              \
    template Vec3<F> normalize_or_zero<F>(const Vec3<F>&);                                                          \
    template Start<F> start<F>(const Vec3<F>&, const Vec3<F>&, F);                                                  \
    template TriangleHit<F> sphere_triangle<F>(const Vec3<F>&, const Vec3<F>&, F, F, F, const Vec3<F>&,             \
                                                const Vec3<F>&, const Vec3<F>&, const Vec3<F>&);                     \
    template Vec3<F> clamp_point<F>(const Vec3<F>&, const Vec3<F>&, F);                                             \
    template Skitter<F> skitter<F>(const Vec3<F>&, const Vec3<F>&, F, const Vec3<F>&);
HAILSTONE_SWEEP_INSTANTIATE(float)
HAILSTONE_SWEEP_INSTANTIATE(double)
#undef HAILSTONE_SWEEP_INSTANTIATE

} // namespace hailstone::game::sweep
