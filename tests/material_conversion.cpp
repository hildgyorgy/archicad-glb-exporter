#include "MaterialConversion.hpp"

#include <cmath>
#include <stdexcept>

namespace {

void Require (bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error (message);
}

void RequireNear (double actual, double expected, const char* message)
{
	if (std::abs (actual - expected) > 1.0e-9)
		throw std::runtime_error (message);
}

} // namespace

int main ()
{
	using DropView::MaterialConversion::ApplyTransparency;
	using DropView::MaterialConversion::ArchicadMaterialProperties;
	using DropView::MaterialConversion::IsClearGlassCandidate;
	using DropView::MaterialConversion::IsHighConfidenceClearGlass;

	const ArchicadMaterialProperties likelyClearGlass {true, 69.0, 78.0, 8000.0, 0.0, false};
	Require (IsClearGlassCandidate (likelyClearGlass), "physical clear-glass candidate was not offered");
	Require (IsHighConfidenceClearGlass (likelyClearGlass), "high-confidence clear glass was not preselected");

	DropView::Glb::Material clearGlass;
	clearGlass.name = "Any language and any surface name";
	clearGlass.metallic = 0.75;
	clearGlass.alpha = 0.2;
	ApplyTransparency (likelyClearGlass, true, clearGlass);
	RequireNear (clearGlass.transmission, 0.98, "clear glass transmission changed");
	RequireNear (clearGlass.roughness, 0.03, "clear glass roughness changed");
	RequireNear (clearGlass.ior, 1.5, "clear glass IOR changed");
	RequireNear (clearGlass.metallic, 0.0, "clear glass remained metallic");
	RequireNear (clearGlass.alpha, 1.0, "clear glass uses raster coverage alpha");
	Require (clearGlass.clearGlassOverride, "clear glass override was not recorded");

	const ArchicadMaterialProperties tintedRoughGlass {true, 58.0, 35.0, 300.0, 0.0, false};
	Require (IsClearGlassCandidate (tintedRoughGlass), "selectable glass was excluded from the dialog");
	Require (!IsHighConfidenceClearGlass (tintedRoughGlass), "questionable glass was preselected");
	DropView::Glb::Material preservedGlass;
	preservedGlass.name = "Name must not affect conversion";
	ApplyTransparency (tintedRoughGlass, false, preservedGlass);
	RequireNear (preservedGlass.transmission, 0.58, "unselected glass transparency was overridden");
	Require (preservedGlass.roughness > 0.03, "unselected rough glass was converted to clear glass");
	RequireNear (preservedGlass.alpha, 1.0, "physical transmission became coverage alpha");
	Require (!preservedGlass.clearGlassOverride, "unselected glass was marked as overridden");

	const ArchicadMaterialProperties leafCutout {true, 75.0, 80.0, 8000.0, 0.0, true};
	Require (!IsClearGlassCandidate (leafCutout), "alpha-cutout texture was offered as clear glass");
	const ArchicadMaterialProperties lampGlass {true, 75.0, 80.0, 8000.0, 40.0, false};
	Require (IsClearGlassCandidate (lampGlass), "transparent lamp glass was missing from the user-selectable list");
	Require (!IsHighConfidenceClearGlass (lampGlass), "emissive lamp glass was preselected");
	const ArchicadMaterialProperties genericTransparentSurface {false, 90.0, 90.0, 9000.0, 0.0, false};
	Require (IsClearGlassCandidate (genericTransparentSurface), "generic transparency was missing from the list");
	Require (!IsHighConfidenceClearGlass (genericTransparentSurface),
	         "generic transparency was preselected without an Archicad glass declaration");
	const ArchicadMaterialProperties lowTransparencySurface {true, 49.0, 90.0, 9000.0, 0.0, false};
	Require (!IsClearGlassCandidate (lowTransparencySurface), "surface below the 50 percent threshold was offered");
	return 0;
}
