#include "TextureCoordinates.hpp"

#include <cmath>
#include <stdexcept>

namespace {

void RequireNear (double actual, double expected, const char* message)
{
	if (std::abs (actual - expected) > 1.0e-9)
		throw std::runtime_error (message);
}

} // namespace

int main ()
{
	using DropView::TextureCoordinates::ConvertToGltf;
	constexpr double Pi = 3.1415926535897932384626433832795;

	const auto unrotated = ConvertToGltf (4.0, 6.0, 0.0, 2.0, 3.0);
	RequireNear (unrotated.u, 2.0, "texture X size was not applied");
	RequireNear (unrotated.v, -1.0, "texture Y size or glTF V-axis conversion was not applied");

	const auto quarterTurn = ConvertToGltf (2.0, 4.0, Pi / 2.0, 2.0, 4.0);
	RequireNear (quarterTurn.u, -2.0, "a 90-degree Archicad texture rotation was lost");
	RequireNear (quarterTurn.v, 0.5, "rotated V coordinate is incorrect");

	// Regression: treating the Archicad radian value as degrees produced an
	// almost unrotated texture and turned vertical wood grain horizontally.
	const auto wallPanel = ConvertToGltf (0.0, 1.0, Pi / 2.0, 1.0, 1.0);
	RequireNear (wallPanel.u, -1.0, "vertical panel texture axis was not rotated into U");
	RequireNear (wallPanel.v, 1.0, "vertical panel texture retained the wrong V variation");

	return 0;
}
