#include "SurfaceNormals.hpp"

#include <cmath>
#include <stdexcept>

using namespace GlbGeometry;

namespace {

void Require (bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error (message);
}

bool NearlyEqual (double first, double second)
{
	return std::abs (first - second) < 1.0e-6;
}

} // namespace

int main ()
{
	const std::vector<Point> vertices = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0},
	                                     {0.0, 1.0, 0.0}, {1.0, 0.0, 1.0}, {1.0, 1.0, 1.0}};
	const std::vector<SmoothingFace> faces = {{true, {{{0, 1, 2, 3}}}, {0.0, 0.0, 1.0}},
	                                          {true, {{{1, 4, 5, 2}}}, {1.0, 0.0, 0.0}}};

	const CornerNormals hard = CalculateCornerNormals (vertices, faces, {{1, 2, 0, 1, false}});
	Require (NearlyEqual (hard[0].at (1)[0], 0.0) && NearlyEqual (hard[0].at (1)[2], 1.0),
	         "A hard edge changed the first face normal");
	Require (NearlyEqual (hard[1].at (1)[0], 1.0) && NearlyEqual (hard[1].at (1)[2], 0.0),
	         "A hard edge changed the second face normal");

	const CornerNormals smooth = CalculateCornerNormals (vertices, faces, {{1, 2, 0, 1, true}});
	const double diagonal = std::sqrt (0.5);
	for (std::uint32_t sharedVertex : {1U, 2U}) {
		Require (NearlyEqual (smooth[0].at (sharedVertex)[0], diagonal) &&
		             NearlyEqual (smooth[0].at (sharedVertex)[2], diagonal),
		         "A smooth edge did not average its adjacent face normals");
		Require (NearlyEqual (smooth[1].at (sharedVertex)[0], diagonal) &&
		             NearlyEqual (smooth[1].at (sharedVertex)[2], diagonal),
		         "Adjacent faces received different normals along a smooth edge");
	}
	Require (NearlyEqual (smooth[0].at (0)[0], 0.0) && NearlyEqual (smooth[0].at (0)[2], 1.0),
	         "Smoothing crossed an unrelated corner");
}
