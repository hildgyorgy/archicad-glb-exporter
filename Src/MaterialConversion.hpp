#pragma once

#include "GlbWriter.hpp"

#include <algorithm>
#include <cmath>

namespace DropView::MaterialConversion {

struct ArchicadMaterialProperties {
	bool declaredGlass = false;
	double transparencyPercent = 0.0;
	double specularPercent = 0.0;
	double shine = 0.0;
	double emissionAttenuation = 0.0;
	bool usesAlphaCutout = false;
};

// Archicad's surface and emission colour pickers provide sRGB values.
// glTF material colour factors are linear; texture RGB is decoded by the viewer.
inline double SrgbToLinear (double channel)
{
	channel = std::clamp (channel, 0.0, 1.0);
	return channel <= 0.04045 ? channel / 12.92 : std::pow ((channel + 0.055) / 1.055, 2.4);
}

inline void ApplySurfaceColor (double red, double green, double blue, DropView::Glb::Material& material)
{
	material.red = SrgbToLinear (red);
	material.green = SrgbToLinear (green);
	material.blue = SrgbToLinear (blue);
}

inline void ApplyEmissionColor (double red, double green, double blue, double attenuation,
                                DropView::Glb::Material& material)
{
	// Preserve the existing Archicad emission strength; only decode its RGB colour.
	const double emissionFactor = std::clamp (attenuation / 100.0, 0.0, 1.0);
	material.emissiveRed = SrgbToLinear (red) * emissionFactor;
	material.emissiveGreen = SrgbToLinear (green) * emissionFactor;
	material.emissiveBlue = SrgbToLinear (blue) * emissionFactor;
}

inline bool UsesAlphaCutout (bool imageHasAlpha, bool useAlpha, bool transparencyPattern,
                             double transparencyPercent)
{
	if (!imageHasAlpha)
		return false;

	// Archicad normally marks cutout textures with both texture-status flags.
	// Some library surfaces (for example chain-link fencing) instead expose a
	// transparent surface plus an RGBA image whose alpha carries the coverage.
	// Requiring both flags loses that mask. The transparency fallback is narrow:
	// opaque RGBA textures such as the zinc texture remain opaque.
	return (useAlpha && transparencyPattern) || transparencyPercent > 0.0;
}

inline bool IsClearGlassCandidate (const ArchicadMaterialProperties& source)
{
	return source.transparencyPercent >= 50.0 && !source.usesAlphaCutout;
}

inline bool IsHighConfidenceClearGlass (const ArchicadMaterialProperties& source)
{
	return IsClearGlassCandidate (source) && source.declaredGlass && source.transparencyPercent >= 65.0 &&
	       source.specularPercent >= 50.0 && source.shine >= 1000.0 && source.emissionAttenuation <= 1.0;
}

inline void ApplyTransparency (const ArchicadMaterialProperties& source, bool exportAsClearGlass,
                               DropView::Glb::Material& material)
{
	if (exportAsClearGlass) {
		material.transmission = 0.98;
		material.roughness = 0.03;
		material.ior = 1.5;
		material.metallic = 0.0;
		material.alpha = 1.0;
		material.alphaMask = false;
		material.clearGlassOverride = true;
		return;
	}

	if (source.usesAlphaCutout) {
		// Texture alpha is raster coverage, not optical transmission. MASK keeps
		// the drawn parts opaque and discards the empty parts of the image.
		material.transmission = 0.0;
		material.alpha = 1.0;
		return;
	}

	const double sourceTransmission = std::clamp (source.transparencyPercent / 100.0, 0.0, 1.0);
	if (sourceTransmission > 0.0 || source.declaredGlass) {
		// Archicad transparency describes transmitted light, not raster coverage.
		// Keep the surface opaque to the rasterizer and use thin-wall glTF
		// transmission. The source values remain stored in extras.archicad.
		material.transmission = sourceTransmission;
		material.alpha = 1.0;
		material.ior = 1.5;
		const double phongExponent = std::max (0.0, source.shine / 100.0);
		material.roughness = std::clamp (std::sqrt (2.0 / (phongExponent + 2.0)), 0.04, 1.0);
	}
}

} // namespace DropView::MaterialConversion
