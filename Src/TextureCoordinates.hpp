#pragma once

#include <cmath>

namespace DropView::TextureCoordinates {

struct Coordinate {
	double u = 0.0;
	double v = 0.0;
};

// ACAPI_ModelAccess_GetTextureCoord returns model-space texture coordinates for
// the individual polygon. Apply the surface attribute's rotation and physical
// image size exactly as ModelerAPI::TextureCoordinate::ApplyMaterialParameters
// does, then convert Archicad's V axis to glTF's texture convention.
inline Coordinate ConvertToGltf (double u, double v, double rotationRadians, double xSize, double ySize)
{
	constexpr double Epsilon = 1.0e-9;
	const double cosine = std::cos (rotationRadians);
	const double sine = std::sin (rotationRadians);
	const double rotatedU = cosine * u - sine * v;
	const double rotatedV = sine * u + cosine * v;
	double scaledU = rotatedU;
	double scaledV = rotatedV;
	if (std::abs (xSize) > Epsilon)
		scaledU = rotatedU / xSize;
	if (std::abs (ySize) > Epsilon)
		scaledV = rotatedV / ySize;
	return {scaledU, 1.0 - scaledV};
}

} // namespace DropView::TextureCoordinates
