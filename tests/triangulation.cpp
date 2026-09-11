#include "../Src/PolygonTriangulation.hpp"
#include <iostream>
#include <stdexcept>

using namespace GlbGeometry;
using XY = std::array<double, 2>;
using Shape = std::vector<std::vector<XY>>;

void Require (bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error (message);
}

bool inside (const XY& p, const std::vector<XY>& ring)
{
	bool result = false;
	for (std::size_t i = 0, j = ring.size () - 1; i < ring.size (); j = i++) {
		const auto& a = ring[i];
		const auto& b = ring[j];
		if ((a[1] > p[1]) != (b[1] > p[1]) && p[0] < (b[0] - a[0]) * (p[1] - a[1]) / (b[1] - a[1]) + a[0])
			result = !result;
	}
	return result;
}
void check (const Shape& shape, double area, int axis, bool reverse)
{
	Rings rings;
	Point normal {};
	normal[axis] = reverse ? -1 : 1;
	int a = (axis + 1) % 3, b = (axis + 2) % 3;
	std::vector<Point> flat;
	for (auto ring : shape) {
		if (reverse)
			std::reverse (ring.begin (), ring.end ());
		rings.emplace_back ();
		for (auto xy : ring) {
			Point p {};
			p[axis] = 3;
			p[a] = xy[0] + 100000;
			p[b] = xy[1] - 100000;
			rings.back ().push_back (p);
			flat.push_back (p);
		}
	}
	auto indices = Triangulate (rings, normal);
	double sum = 0;
	for (std::size_t i = 0; i < indices.size (); i += 3) {
		const auto& p = flat.at (indices[i]);
		const auto& q = flat.at (indices[i + 1]);
		const auto& r = flat.at (indices[i + 2]);
		double cr = (q[a] - p[a]) * (r[b] - p[b]) - (q[b] - p[b]) * (r[a] - p[a]);
		Require (cr * normal[axis] > 0, "Triangle winding is incorrect");
		sum += std::abs (cr) / 2;
		// Interior samples independently catch triangles crossing the notch or holes.
		for (int u = 1; u < 10; ++u)
			for (int v = 1; u + v < 10; ++v) {
				XY sample {};
				for (int k = 0; k < 2; ++k) {
					int dim = k == 0 ? a : b;
					sample[k] = (p[dim] * (10 - u - v) + q[dim] * u + r[dim] * v) / 10 - (k == 0 ? 100000 : -100000);
				}
				Require (inside (sample, shape[0]), "Triangle extends outside the outer contour");
				for (std::size_t h = 1; h < shape.size (); ++h)
					Require (!inside (sample, shape[h]), "Triangle crosses a hole");
			}
	}
	Require (std::abs (sum - area) < 1e-7, "Triangulated area differs from expected area");
}
int main ()
{
	std::vector<std::pair<Shape, double>> cases = {
	    {{{{0, 0}, {6, 0}, {6, 4}, {0, 4}}}, 24},
	    {{{{0, 0}, {6, 0}, {6, 2}, {2, 2}, {2, 5}, {0, 5}}}, 18},
	    {{{{0, 0}, {10, 0}, {10, 8}, {0, 8}}, {{2, 2}, {2, 5}, {5, 5}, {5, 2}}}, 71},
	    {{{{0, 0}, {10, 0}, {10, 8}, {0, 8}}, {{2, 2}, {2, 5}, {5, 5}, {5, 2}}, {{7, 2}, {7, 4}, {9, 4}, {9, 2}}}, 67},
	    {{{{0, 0}, {6, 0}, {6, 2}, {2, 2}, {2, 5}, {0, 5}}, {{0.5, 0.5}, {0.5, 1.5}, {1.5, 1.5}, {1.5, 0.5}}}, 17},
	    {{{{0, 0}, {3, 0}, {6, 0}, {6, 4}, {0, 4}, {0, 0}}}, 24}};
	// Regression contour recovered from the intact side faces of l_alak.glb.
	cases.push_back ({Shape {std::vector<XY> {{2.0721874237060547, -16.84885025024414},
	                                          {17.39390754699707, -16.84885025024414},
	                                          {4.742133617401123, -5.285401344299316},
	                                          {-15.370153427124023, -5.9485673904418945},
	                                          {-8.905878067016602, -16.84885025024414},
	                                          {-4.069102764129639, -11.398709297180176}}},
	                  233.9003642715718});
	for (auto& c : cases)
		for (int axis = 0; axis < 3; ++axis)
			for (bool reverse : {false, true})
				check (c.first, c.second, axis, reverse);
	for (int axis = 0; axis < 3; ++axis)
		for (bool reverse : {false, true}) {
			Rings rings (1);
			Point normal {};
			normal[axis] = reverse ? -1 : 1;
			int a = (axis + 1) % 3, b = (axis + 2) % 3;
			for (XY xy : std::vector<XY> {{0, 0}, {1, 0}, {2, 0}}) {
				Point p {};
				p[a] = xy[0];
				p[b] = xy[1];
				rings[0].push_back (p);
			}
			bool rejected = false;
			try {
				(void)Triangulate (rings, normal);
			} catch (const DegeneratePolygon&) {
				rejected = true;
			}
			Require (rejected, "Zero-area polygon was not rejected");
		}
	std::cout << "48 triangulation cases passed: area, cutouts, holes, winding, three planes, large offset, zero-area "
	             "rejection.\n";
}
