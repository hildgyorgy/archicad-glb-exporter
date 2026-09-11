#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
#if defined (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#endif
#include "ThirdParty/earcut.hpp"
#if defined (__clang__)
#pragma clang diagnostic pop
#endif

namespace GlbGeometry {
using Point = std::array<double, 3>;
using Rings = std::vector<std::vector<Point>>;

class DegeneratePolygon final : public std::runtime_error {
public:
    DegeneratePolygon () : std::runtime_error ("Zero-area polygon") {}
};

// Indices address the concatenation of outer ring, then hole rings.
inline std::vector<std::uint32_t> Triangulate (const Rings& rings, const Point& normal)
{
    if (rings.empty () || rings.front ().size () < 3)
        throw std::runtime_error ("Empty polygon");
    int drop = 0;
    for (int i = 1; i < 3; ++i)
        if (std::abs (normal[i]) > std::abs (normal[drop])) drop = i;
    if (!std::isfinite (normal[drop]) || std::abs (normal[drop]) < 1e-12)
        throw std::runtime_error ("Invalid polygon normal");
    const int a = (drop + 1) % 3, b = (drop + 2) % 3;
    std::vector<std::vector<std::array<double, 2>>> projected;
    std::vector<Point> flat;
    double expectedArea = 0;
    const Point origin = rings.front ().front ();
    for (const auto& ring : rings) {
        if (ring.size () < 3) throw std::runtime_error ("Incomplete contour");
        projected.emplace_back ();
        for (const auto& p : ring) {
            for (double x : p) if (!std::isfinite (x)) throw std::runtime_error ("Invalid vertex");
            projected.back ().push_back ({p[a] - origin[a], p[b] - origin[b]});
            flat.push_back (p);
        }
        double area = 0;
        const auto& xy = projected.back ();
        for (std::size_t i = 0; i < xy.size (); ++i) {
            const auto& q = xy[(i + 1) % xy.size ()];
            area += xy[i][0] * q[1] - q[0] * xy[i][1];
        }
        expectedArea += (projected.size () == 1 ? 1 : -1) * std::abs (area) / 2;
    }
    if (std::abs (expectedArea) <= 1e-12)
        throw DegeneratePolygon ();
    if (expectedArea < 0)
        throw std::runtime_error ("Hole area exceeds outer contour area");
#if defined (_MSC_VER)
#pragma warning(push)
    // earcut's local Point alias triggers C4459 when this template is instantiated
    // beside GlbGeometry::Point. Keep the exception scoped to the third-party call.
#pragma warning(disable : 4459)
#endif
    auto indices = mapbox::earcut<std::uint32_t> (projected);
#if defined (_MSC_VER)
#pragma warning(pop)
#endif
    if (indices.empty () || indices.size () % 3)
        throw std::runtime_error ("Earcut returned no triangles");
    double actualArea = 0;
    for (std::size_t i = 0; i < indices.size (); i += 3) {
        for (int k = 0; k < 3; ++k)
            if (indices[i + k] >= flat.size ()) throw std::runtime_error ("Invalid triangle index");
        const auto& p = flat[indices[i]];
        const auto& q = flat[indices[i + 1]];
        const auto& r = flat[indices[i + 2]];
        const double cross = (q[a] - p[a]) * (r[b] - p[b]) - (q[b] - p[b]) * (r[a] - p[a]);
        actualArea += std::abs (cross) / 2;
        if (cross * normal[drop] < 0) std::swap (indices[i + 1], indices[i + 2]);
    }
    if (std::abs (actualArea - expectedArea) > std::max (1e-10, expectedArea * 1e-7))
        throw std::runtime_error ("Triangle area differs from contour area");
    return indices;
}
}
