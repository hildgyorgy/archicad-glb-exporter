#include "GlbWriter.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

template <class Operation> void RequireFailure (Operation operation, const char* message)
{
	try {
		operation ();
	} catch (const std::exception&) {
		return;
	}
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

	model.materials = {opaqueTextured, clearGlass, tintedGlass, plant, coverageBlend};
	model.groups = {{"layer:1", "Layer: Architecture", {{0, {0, 1, 2}}, {1, {3, 4, 5}}, {4, {0, 1, 2}}}},
	                {"layer:2", "Layer: Site", {{2, {6, 7, 8}}, {3, {9, 10, 11}}}},
	                {"layer:3", "Layer: Reused material", {{0, {0, 1, 2}}}}};
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
	std::ofstream output (arguments[1], std::ios::binary | std::ios::trunc);
	if (!output)
		throw std::runtime_error ("Cannot create GLB fixture");
	output.write (glb.data (), static_cast<std::streamsize> (glb.size ()));
	if (!output)
		throw std::runtime_error ("Cannot write GLB fixture");
	return 0;
}
