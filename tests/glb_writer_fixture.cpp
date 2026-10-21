#include "GlbWriter.hpp"
#include "MaterialConversion.hpp"

#include <fstream>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

template <class Operation> void RequireFailure (Operation operation, const char* message)
{
	try {
		operation ();
	} catch (const std::exception&) {
		return;
	}
	throw std::runtime_error (message);
}

void RequireNear (double actual, double expected, const char* message)
{
	if (std::abs (actual - expected) > 1.0e-6)
		throw std::runtime_error (message);
}

int main (int argumentCount, char** arguments)
{
	if (argumentCount != 2) {
		std::cerr << "Usage: DropViewGlbWriterFixture <output.glb>\n";
		return 2;
	}

	DropView::Glb::Model model;
	model.positions = {{0.0f, 0.0f, 0.0f},  {2.0f, 0.0f, 0.0f},  {0.0f, 3.0f, 0.0f},  {0.0f, 0.0f, 1.0f},
	                   {2.0f, 0.0f, 1.0f},  {0.0f, 3.0f, 1.0f},  {-2.0f, 1.0f, 4.0f}, {1.0f, 1.0f, 4.0f},
	                   {-2.0f, 5.0f, 4.0f}, {-2.0f, 1.0f, 5.0f}, {1.0f, 1.0f, 5.0f},  {-2.0f, 5.0f, 5.0f}};
	model.normals.assign (12, {0.0f, 0.0f, 1.0f});
	model.textureCoordinates = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
	model.textureCoordinates.resize (12, {0.0f, 0.0f});

	const std::vector<char> sharedImage {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n', 't', 'e', 's', 't'};
	const std::vector<char> normalImage {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n', 'n'};
	const std::vector<char> ormImage {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n', 'o'};
	const std::vector<char> emissiveImage {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n', 'e'};
	DropView::Glb::Material opaqueTextured;
	opaqueTextured.sourceIndex = 11;
	opaqueTextured.name = "Opaque textured surface";
	opaqueTextured.imageData = sharedImage;
	opaqueTextured.imageMimeType = "image/png";
	// A textured roof keeps a white multiplier even if Archicad has a surface swatch.
	DropView::MaterialConversion::ApplySurfaceColor (231.0 / 255.0, 207.0 / 255.0, 171.0 / 255.0, opaqueTextured);
	opaqueTextured.normalTexture = {normalImage, "image/png"};
	opaqueTextured.metallicRoughnessTexture = {ormImage, "image/png"};
	opaqueTextured.occlusionTexture = {ormImage, "image/png"};
	opaqueTextured.emissiveTexture = {emissiveImage, "image/png"};
	opaqueTextured.emissiveRed = 0.1;
	opaqueTextured.emissiveGreen = 0.2;
	opaqueTextured.emissiveBlue = 0.3;

	DropView::Glb::Material clearGlass;
	clearGlass.sourceIndex = 12;
	clearGlass.name = "Clear glass";
	clearGlass.sourceMaterialType = 5;
	clearGlass.sourceTransparencyPercent = 100.0;
	clearGlass.sourceSpecularPercent = 100.0;
	clearGlass.sourceShine = 10000.0;
	clearGlass.roughness = 0.03;
	clearGlass.transmission = 0.98;
	clearGlass.clearGlassOverride = true;

	DropView::Glb::Material tintedGlass;
	tintedGlass.sourceIndex = 13;
	tintedGlass.name = "Tinted rough glass";
	tintedGlass.sourceMaterialType = 5;
	tintedGlass.sourceTransparencyPercent = 65.0;
	tintedGlass.sourceSpecularPercent = 70.0;
	tintedGlass.sourceShine = 1800.0;
	tintedGlass.red = 0.125;
	tintedGlass.green = 0.25;
	tintedGlass.blue = 0.75;
	tintedGlass.roughness = 0.32;
	tintedGlass.transmission = 0.65;

	DropView::Glb::Material plant;
	plant.sourceIndex = 14;
	plant.name = "Leaf \"cutout\"";
	plant.alphaMask = true;
	plant.texture.mirrorX = true;
	plant.imageData = sharedImage;
	plant.imageMimeType = "image/png";

	DropView::Glb::Material coverageBlend;
	coverageBlend.sourceIndex = 15;
	coverageBlend.name = "Coverage blend";
	coverageBlend.alpha = 0.5;
	coverageBlend.texture.mirrorY = true;
	coverageBlend.imageData = sharedImage;
	coverageBlend.imageMimeType = "image/png";

	DropView::Glb::Material beigePlaster;
	beigePlaster.sourceIndex = 16;
	beigePlaster.name = "gipsz - szemcsés bézs";
	DropView::MaterialConversion::ApplySurfaceColor (231.0 / 255.0, 207.0 / 255.0, 171.0 / 255.0, beigePlaster);

	model.materials = {opaqueTextured, clearGlass, tintedGlass, plant, coverageBlend, beigePlaster};
	model.groups = {
	    {"layer:1", "Layer: Architecture", {{0, {0, 1, 2}}, {1, {3, 4, 5}}, {4, {0, 1, 2}}, {5, {0, 1, 2}}}},
	    {"layer:2", "Layer: Site", {{2, {6, 7, 8}}, {3, {9, 10, 11}}}},
	    {"layer:3", "Layer: Reused material", {{0, {0, 1, 2}}}}};
	model.designCredits = "Design: György \"George\" Hild\nLandscape: Example Studio";
	model.initialView = DropView::Glb::InitialView {};
	model.initialView->projection = DropView::Glb::CameraProjection::Perspective;
	model.initialView->position = {10.0f, 8.0f, 6.0f};
	model.initialView->target = {2.0f, 1.0f, -4.0f};
	model.initialView->up = {0.0f, 1.0f, 0.0f};
	model.initialView->verticalFieldOfViewRadians = 0.72;
	model.initialView->nearPlane = 0.02;
	model.initialView->archicadViewConeRadians = 0.91;
	model.initialView->archicadRollAngleRadians = 0.12;
	model.initialView->archicadTwoPointPerspective = true;
	model.initialView->archicadWindowWidth = 1600;
	model.initialView->archicadWindowHeight = 900;
	model.initialView->archicadZoomScaleX = 1.25;
	model.initialView->archicadZoomScaleY = 1.5;
	model.initialView->archicadZoomDisplacementX = 12.0;
	model.initialView->archicadZoomDisplacementY = -8.0;
	model.sun = DropView::Glb::ConvertArchicadSunAngles (240.0, 35.0);
	RequireNear (model.sun->azimuthRadians, 4.1887902047863905, "sun azimuth was not converted to radians");
	RequireNear (model.sun->altitudeRadians, 0.6108652381980153, "sun altitude was not converted to radians");
	RequireNear (model.sun->directionToSun.x, -0.4095760221444957, "sun X direction is incorrect");
	RequireNear (model.sun->directionToSun.y, 0.573576436351046, "sun must be above the horizon");
	RequireNear (model.sun->directionToSun.z, 0.7094064799162225, "sun Z direction is incorrect");
	RequireNear (model.sun->lightDirection.x, -model.sun->directionToSun.x,
	             "sunlight X direction is not opposite");
	RequireNear (model.sun->lightDirection.y, -model.sun->directionToSun.y,
	             "sunlight Y direction is not opposite");
	RequireNear (model.sun->lightDirection.z, -model.sun->directionToSun.z,
	             "sunlight Z direction is not opposite");
	model.sun->positionByDate = true;
	model.sun->year = 2026;
	model.sun->month = 9;
	model.sun->day = 25;
	model.sun->hour = 13;
	model.sun->minute = 30;
	model.sun->daylightSaving = true;
	DropView::Glb::WindowLight windowLight;
	windowLight.elementGuid = "lamp-guid";
	windowLight.libraryPartId = "window-light-library-id";
	windowLight.libraryPartName = "ablakfény 27";
	windowLight.position = {2.0f, 4.2f, -3.0f};
	windowLight.rotationRadians = 0.5;
	windowLight.enabled = true;
	windowLight.color = {0.8f, 0.9f, 1.0f};
	windowLight.defaultA = 1.2;
	windowLight.defaultB = 1.5;
	windowLight.xRatio = 2.0;
	windowLight.parameters.push_back ({"intensity", 5, 100.0, "", false});
	windowLight.parameters.push_back ({"label", 0, 0.0, "Window \"A\"", true});
	windowLight.lightComponentStatus = "available";
	DropView::Glb::ArchicadLightComponent lightComponent;
	lightComponent.type = 2;
	lightComponent.shadowStatus = 1;
	lightComponent.position = {2.0f, 4.2f, -3.0f};
	lightComponent.direction = {0.0f, 0.0f, -1.0f};
	lightComponent.color = {1.0f, 0.8f, 0.6f};
	lightComponent.radius = 0.5;
	lightComponent.cosInner = 0.9;
	lightComponent.cosOuter = 0.7;
	lightComponent.angularFalloff = 0.2;
	lightComponent.distanceStart = 0.1;
	lightComponent.distanceEnd = 6.0;
	lightComponent.distanceFalloff = 1.0;
	windowLight.lightComponents.push_back (lightComponent);
	model.windowLights.push_back (windowLight);
	DropView::Glb::WindowLight areaLight = windowLight;
	areaLight.elementGuid = "area-original";
	areaLight.position = {6.9325f, 2.7f, 7.44035f};
	areaLight.rotationRadians = 3.141592653589793;
	areaLight.reflected = true;
	areaLight.parameters.push_back ({"c4dDetAreaShape", 0, 0.0, "Another locale", true});
	areaLight.parameters.push_back ({"iC4dDetAreaShape", 0, 2.0, "", false});
	areaLight.parameters.push_back ({"c4dDetAreaX", 0, 11.646532, "", false});
	areaLight.parameters.push_back ({"c4dDetAreaY", 0, 2.4, "", false});
	areaLight.lightComponents.front ().type = 3;
	areaLight.lightComponents.front ().position = {12.755766f, 3.9f, 7.44035f};
	areaLight.lightComponents.front ().direction = {0.0f, 0.0f, 1.0f};
	areaLight.lightComponents.front ().color = {0.0f, 0.0f, 0.0f};
	model.windowLights.push_back (areaLight);
	areaLight.elementGuid = "area-flipped";
	areaLight.rotationRadians = 0.0;
	areaLight.reflected = false;
	areaLight.lightComponents.front ().direction = {0.0f, 0.0f, -1.0f};
	model.windowLights.push_back (areaLight);
	areaLight.elementGuid = "area-rotated";
	areaLight.rotationRadians = 4.71238898038469;
	areaLight.reflected = true;
	areaLight.position = {12.755768f, 2.7f, 13.263616f};
	areaLight.lightComponents.front ().direction = {1.0f, 0.0f, 0.0f};
	model.windowLights.push_back (areaLight);
	areaLight.elementGuid = "area-other-shape";
	for (auto& parameter : areaLight.parameters) {
		if (parameter.name == "c4dDetAreaShape")
			parameter.text = "Téglalap";
		else if (parameter.name == "iC4dDetAreaShape")
			parameter.value = 1.0;
	}
	model.windowLights.push_back (areaLight);
	const std::vector<char> glb = DropView::Glb::BuildBinary (model, "Drop & View \"writer\"\ntest");

	RequireFailure ([] { (void)DropView::Glb::BuildBinary ({}, "empty"); }, "Model without materials was accepted");
	RequireFailure (
	    [model] () mutable {
		    model.groups.clear ();
		    (void)DropView::Glb::BuildBinary (model, "missing groups");
	    },
	    "Model without groups was accepted");
	RequireFailure (
	    [model] () mutable {
		    model.normals.pop_back ();
		    (void)DropView::Glb::BuildBinary (model, "mismatched attributes");
	    },
	    "Mismatched vertex attributes were accepted");
	RequireFailure (
	    [model] () mutable {
		    model.groups[0].primitives[0].indices = {0, 1};
		    (void)DropView::Glb::BuildBinary (model, "incomplete triangle");
	    },
	    "Incomplete triangle was accepted");
	RequireFailure (
	    [model] () mutable {
		    model.groups[0].primitives[0].indices = {0, 1, 99};
		    (void)DropView::Glb::BuildBinary (model, "invalid index");
	    },
	    "Out-of-range vertex index was accepted");
	RequireFailure (
	    [model] () mutable {
		    model.groups[0].primitives[0].materialIndex = model.materials.size ();
		    (void)DropView::Glb::BuildBinary (model, "invalid material");
	    },
	    "Out-of-range material index was accepted");
	RequireFailure (
	    [model] () mutable {
		    model.normals[0].x = std::numeric_limits<float>::quiet_NaN ();
		    (void)DropView::Glb::BuildBinary (model, "invalid normal");
	    },
	    "Non-finite normal was written to a GLB");
	RequireFailure (
	    [model] () mutable {
		    model.positions[0].x = std::numeric_limits<float>::infinity ();
		    (void)DropView::Glb::BuildBinary (model, "invalid position");
	    },
	    "Non-finite position was written to a GLB");
	std::ofstream output (arguments[1], std::ios::binary | std::ios::trunc);
	if (!output)
		throw std::runtime_error ("Cannot create GLB fixture");
	output.write (glb.data (), static_cast<std::streamsize> (glb.size ()));
	if (!output)
		throw std::runtime_error ("Cannot write GLB fixture");

	model.designCredits.clear ();
	model.windowLights.clear ();
	model.initialView->projection = DropView::Glb::CameraProjection::Orthographic;
	model.initialView->orthographicXMag = 12.5;
	model.initialView->orthographicYMag = 7.25;
	model.initialView->nearPlane = 0.1;
	model.initialView->farPlane = 250.0;
	model.initialView->archicadProjectionMode = 3;
	model.initialView->archicadProjectionMatrix[0] = 0.5;
	model.initialView->archicadInverseProjectionMatrix[0] = 2.0;
	const std::vector<char> orthographicGlb =
	    DropView::Glb::BuildBinary (model, "Drop & View orthographic writer test");
	std::ofstream orthographicOutput (std::string (arguments[1]) + ".ortho.glb", std::ios::binary | std::ios::trunc);
	if (!orthographicOutput)
		throw std::runtime_error ("Cannot create orthographic GLB fixture");
	orthographicOutput.write (orthographicGlb.data (), static_cast<std::streamsize> (orthographicGlb.size ()));
	if (!orthographicOutput)
		throw std::runtime_error ("Cannot write orthographic GLB fixture");

	DropView::Glb::Model dedupModel;
	dedupModel.positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
	                        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
	                        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
	dedupModel.normals.assign (9, {0.0f, 0.0f, 1.0f});
	dedupModel.normals[5] = {0.0f, 1.0f, 0.0f};
	dedupModel.textureCoordinates = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
	                                 {0.0f, 0.0f}, {0.5f, 0.0f}, {0.0f, 1.0f},
	                                 {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
	dedupModel.materials.emplace_back ();
	dedupModel.groups = {{"dedup", "Deduplication", {{0, {0, 1, 2, 3, 4, 5, 6, 7, 8}}, {0, {6, 7, 8}}}}};
	const std::vector<char> dedupGlb = DropView::Glb::BuildBinary (dedupModel, "Exact vertex deduplication test");
	std::ofstream dedupOutput (std::string (arguments[1]) + ".dedup.glb", std::ios::binary | std::ios::trunc);
	if (!dedupOutput)
		throw std::runtime_error ("Cannot create deduplication GLB fixture");
	dedupOutput.write (dedupGlb.data (), static_cast<std::streamsize> (dedupGlb.size ()));
	if (!dedupOutput)
		throw std::runtime_error ("Cannot write deduplication GLB fixture");

	DropView::Glb::Model indexWidthModel;
	indexWidthModel.materials.emplace_back ();
	for (std::uint32_t i = 0; i < 65537; ++i) {
		indexWidthModel.positions.push_back ({static_cast<float> (i), static_cast<float> (i % 3), 0.0f});
		indexWidthModel.normals.push_back ({0.0f, 0.0f, 1.0f});
		indexWidthModel.textureCoordinates.push_back ({0.0f, 0.0f});
	}
	std::vector<std::uint32_t> shortIndices;
	shortIndices.reserve (65538);
	for (std::uint32_t i = 0; i < 65536; ++i)
		shortIndices.push_back (i);
	shortIndices.push_back (0);
	shortIndices.push_back (1);
	std::vector<std::uint32_t> longIndices = shortIndices;
	longIndices[65536] = 65536;
	indexWidthModel.groups = {{"index-width", "Index width boundary",
	                           {{0, std::move (shortIndices)}, {0, std::move (longIndices)}}}};
	const std::vector<char> indexWidthGlb = DropView::Glb::BuildBinary (indexWidthModel, "Index width test");
	std::ofstream indexWidthOutput (std::string (arguments[1]) + ".index-width.glb",
	                                std::ios::binary | std::ios::trunc);
	if (!indexWidthOutput)
		throw std::runtime_error ("Cannot create index-width GLB fixture");
	indexWidthOutput.write (indexWidthGlb.data (), static_cast<std::streamsize> (indexWidthGlb.size ()));
	if (!indexWidthOutput)
		throw std::runtime_error ("Cannot write index-width GLB fixture");

	DropView::Glb::Model precisionModel;
	precisionModel.positions = {{6.707906246185303f, -8.881784197001252e-16f, 0.0f},
	                            {7.0f, 0.0f, 0.0f}, {6.707906246185303f, 1.0f, 0.0f}};
	precisionModel.normals.assign (3, {0.0f, 0.0f, 1.0f});
	precisionModel.textureCoordinates.assign (3, {0.0f, 0.0f});
	precisionModel.materials.emplace_back ();
	precisionModel.groups = {{"precision", "Float bounds", {{0, {0, 1, 2}}}}};
	const std::vector<char> precisionGlb = DropView::Glb::BuildBinary (precisionModel, "Float bounds test");
	std::ofstream precisionOutput (std::string (arguments[1]) + ".precision.glb",
	                               std::ios::binary | std::ios::trunc);
	if (!precisionOutput)
		throw std::runtime_error ("Cannot create precision GLB fixture");
	precisionOutput.write (precisionGlb.data (), static_cast<std::streamsize> (precisionGlb.size ()));
	if (!precisionOutput)
		throw std::runtime_error ("Cannot write precision GLB fixture");

	DropView::Glb::Model variantModel;
	variantModel.positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
	variantModel.normals.assign (3, {0.0f, 0.0f, 1.0f});
	variantModel.textureCoordinates = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
	variantModel.alternateTextureCoordinates = {{0.0f, 0.0f}, {2.0f, 0.0f}, {0.0f, 2.0f}};
	variantModel.materials = {opaqueTextured, opaqueTextured};
	variantModel.materials[1].name = "Maxon PBR";
	variantModel.materials[1].textureCoordinateSet = 1;
	variantModel.materials[1].multiplyBaseColorTexture = true;
	variantModel.materials[1].red = 0.5;
	variantModel.alternateMaterialVariantName = "Maxon / Redshift PBR";
	variantModel.groups = {{"variant", "Variant", {{0, {0, 1, 2}, 1}}}};
	const std::vector<char> variantGlb = DropView::Glb::BuildBinary (variantModel, "Variant test");
	std::ofstream variantOutput (std::string (arguments[1]) + ".variant.glb", std::ios::binary | std::ios::trunc);
	if (!variantOutput)
		throw std::runtime_error ("Cannot create variant GLB fixture");
	variantOutput.write (variantGlb.data (), static_cast<std::streamsize> (variantGlb.size ()));
	if (!variantOutput)
		throw std::runtime_error ("Cannot write variant GLB fixture");
	return 0;
}
