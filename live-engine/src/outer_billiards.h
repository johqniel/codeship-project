#pragma once

#include <vector>
#include <cstddef>
#include <cmath>
#include "vec2.h"

inline bool is_curved_in_plane(const Vec2& q, double curvature) {
    return (1.0 + curvature * dot(q, q)) > 1e-7;
}

inline bool point_in_triangle(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& q) {
    const double d0 = cross(b - a, q - a);
    const double d1 = cross(c - b, q - b);
    const double d2 = cross(a - c, q - c);
    const bool has_neg = d0 < 0.0 || d1 < 0.0 || d2 < 0.0;
    const bool has_pos = d0 > 0.0 || d1 > 0.0 || d2 > 0.0;
    return !(has_neg && has_pos);
}

inline bool point_in_convex_hull(const std::vector<Vec2>& pts, const Vec2& q) {
    const int n = static_cast<int>(pts.size());
    if (n < 3) return false;

    const int m = n - 1;
    const Vec2 extra = pts[m];

    bool inside_polygon = true;
    for (int i = 0; i < m; i++) {
        if (cross(pts[(i + 1) % m] - pts[i], q - pts[i]) < 0.0) {
            inside_polygon = false;
            break;
        }
    }
    if (inside_polygon) return true;

    for (int i = 0; i < m; i++) {
        if (point_in_triangle(extra, pts[i], pts[(i + 1) % m], q)) return true;
    }
    return false;
}

inline int outer_billiards_tangent_vertex(const std::vector<Vec2>& verts, const Vec2& p) {
    int best = 0;
    for (int i = 1; i < static_cast<int>(verts.size()); i++) {
        const Vec2 a = verts[best] - p;
        const Vec2 b = verts[i] - p;
        const double turn = cross(a, b);
        if (turn < 0.0) best = i;
        else if (turn == 0.0 && dot(b, b) > dot(a, a)) best = i;
    }
    return best;
}

inline int outer_billiards_pivot(const std::vector<Vec2>& verts, const Vec2& p, double curvature) {
    if (verts.size() < 2 || !is_curved_in_plane(p, curvature)) return -1;
    return outer_billiards_tangent_vertex(verts, p);
}

inline Vec2 outer_billiards_reflect(const Vec2& pivot, const Vec2& p, double curvature) {
    const double nv = 1.0 + curvature * dot(pivot, pivot);
    if (nv <= 1e-9) return p;
    const double a = 2.0 * (curvature * dot(p, pivot) + 1.0) / nv;
    const double denom = a - 1.0;
    if (denom > -1e-9 && denom < 1e-9) return p;
    return (pivot * a - p) / denom;
}

inline bool is_convex_ccw(const std::vector<Vec2>& poly) {
    if (poly.size() < 3) return false;
    const size_t n = poly.size();
    double sign = 0.0;
    for (size_t i = 0; i < n; i++) {
        const Vec2 a = poly[i];
        const Vec2 b = poly[(i + 1) % n];
        const Vec2 c = poly[(i + 2) % n];
        const double z = cross(b - a, c - b);
        if (std::abs(z) < 1e-9) continue;
        if (sign == 0.0) sign = z;
        else if ((sign > 0.0) != (z > 0.0)) return false;
    }
    return sign > 0.0;
}
