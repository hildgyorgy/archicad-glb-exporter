#pragma once

#include "PolygonTriangulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>
#include <utility>
#include <vector>

namespace GlbGeometry {

struct SmoothingFace {
	bool valid = false;
	std::vector<std::vector<std::uint32_t>> contours;
	Point normal {};
};

struct SmoothingEdge {
	std::uint32_t vertex1 = 0;
	std::uint32_t vertex2 = 0;
	std::size_t face1 = 0;
	std::size_t face2 = 0;
	bool smooth = false;
};

using CornerNormals = std::vector<std::map<std::uint32_t, Point>>;

namespace Detail {

class DisjointSets {
public:
	explicit DisjointSets (std::size_t size) : parents (size), ranks (size, 0)
	{
		std::iota (parents.begin (), parents.end (), std::size_t {0});
	}

	std::size_t Find (std::size_t item)
	{
		if (parents[item] != item)
			parents[item] = Find (parents[item]);
		return parents[item];
	}

	void Join (std::size_t first, std::size_t second)
	{
		first = Find (first);
		second = Find (second);
		if (first == second)
			return;
		if (ranks[first] < ranks[second])
			std::swap (first, second);
		parents[second] = first;
		if (ranks[first] == ranks[second])
			++ranks[first];
	}

private:
	std::vector<std::size_t> parents;
	std::vector<unsigned char> ranks;
};

inline Point Normalize (Point value)
{
	const double length = std::sqrt (value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
	if (length <= 1.0e-12)
		return {};
	for (double& component : value)
		component /= length;
	return value;
}

inline double CornerAngle (const Point& previous, const Point& current, const Point& next)
{
	Point first {previous[0] - current[0], previous[1] - current[1], previous[2] - current[2]};
	Point second {next[0] - current[0], next[1] - current[1], next[2] - current[2]};
	first = Normalize (first);
	second = Normalize (second);
	const double firstLengthSquared = first[0] * first[0] + first[1] * first[1] + first[2] * first[2];
	const double secondLengthSquared = second[0] * second[0] + second[1] * second[1] + second[2] * second[2];
	if (firstLengthSquared == 0.0 || secondLengthSquared == 0.0)
		return 1.0;
	const double cosine = std::clamp (first[0] * second[0] + first[1] * second[1] + first[2] * second[2], -1.0, 1.0);
	return std::acos (cosine);
}

} // namespace Detail

inline CornerNormals CalculateCornerNormals (const std::vector<Point>& vertices,
                                             const std::vector<SmoothingFace>& faces,
                                             const std::vector<SmoothingEdge>& edges)
{
	using Corner = std::pair<std::size_t, std::uint32_t>;
	std::map<Corner, std::size_t> cornerIds;
	for (std::size_t faceIndex = 0; faceIndex < faces.size (); ++faceIndex) {
		if (!faces[faceIndex].valid)
			continue;
		for (const auto& contour : faces[faceIndex].contours)
			for (std::uint32_t vertex : contour)
				cornerIds.try_emplace ({faceIndex, vertex}, cornerIds.size ());
	}

	Detail::DisjointSets sets (cornerIds.size ());
	for (const SmoothingEdge& edge : edges) {
		if (!edge.smooth || edge.face1 >= faces.size () || edge.face2 >= faces.size ())
			continue;
		for (std::uint32_t vertex : {edge.vertex1, edge.vertex2}) {
			const auto first = cornerIds.find ({edge.face1, vertex});
			const auto second = cornerIds.find ({edge.face2, vertex});
			if (first != cornerIds.end () && second != cornerIds.end ())
				sets.Join (first->second, second->second);
		}
	}

	std::vector<Point> sums (cornerIds.size ());
	for (std::size_t faceIndex = 0; faceIndex < faces.size (); ++faceIndex) {
		const SmoothingFace& face = faces[faceIndex];
		if (!face.valid)
			continue;
		for (const auto& contour : face.contours) {
			for (std::size_t i = 0; i < contour.size (); ++i) {
				const std::uint32_t vertex = contour[i];
				if (vertex >= vertices.size ())
					continue;
				const std::uint32_t previous = contour[(i + contour.size () - 1) % contour.size ()];
				const std::uint32_t next = contour[(i + 1) % contour.size ()];
				if (previous >= vertices.size () || next >= vertices.size ())
					continue;
				const double weight = Detail::CornerAngle (vertices[previous], vertices[vertex], vertices[next]);
				Point& sum = sums[sets.Find (cornerIds.at ({faceIndex, vertex}))];
				for (std::size_t component = 0; component < 3; ++component)
					sum[component] += face.normal[component] * weight;
			}
		}
	}

	CornerNormals result (faces.size ());
	for (const auto& [corner, id] : cornerIds)
		result[corner.first][corner.second] = Detail::Normalize (sums[sets.Find (id)]);
	return result;
}

} // namespace GlbGeometry
