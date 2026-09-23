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

	if (exportAsClearGlass) {
		material.transmission = 0.98;
		material.roughness = 0.03;
		material.ior = 1.5;
		material.metallic = 0.0;
		material.alpha = 1.0;
		material.clearGlassOverride = true;
	}
}

} // namespace DropView::MaterialConversion
