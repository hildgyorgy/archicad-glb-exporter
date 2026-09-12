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
	model.positions = {{0.0f, 0.0f, 0.0f},  {2.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f},
	                   {0.0f, 0.0f, 1.0f},  {2.0f, 0.0f, 1.0f}, {0.0f, 3.0f, 1.0f},
	                   {-2.0f, 1.0f, 4.0f}, {1.0f, 1.0f, 4.0f}, {-2.0f, 5.0f, 4.0f}};
	model.normals.assign (9, {0.0f, 0.0f, 1.0f});
	model.textureCoordinates = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
	model.textureCoordinates.resize (9, {0.0f, 0.0f});

	const std::vector<char> sharedImage {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n', 't', 'e', 's', 't'};
	DropView::Glb::Material masked;
	masked.name = "Masked \"surface\"";
	masked.alphaMask = true;
	masked.texture.mirrorX = true;
	masked.imageData = sharedImage;
	masked.imageMimeType = "image/png";

	DropView::Glb::Material blended;
	blended.name = "Blended surface";
	blended.alpha = 0.5;
	blended.texture.mirrorY = true;
	blended.imageData = sharedImage;
	blended.imageMimeType = "image/png";

	DropView::Glb::Material solid;
	solid.name = "Solid \\ surface\nline";
	solid.red = 0.125;
	solid.green = 0.25;
	solid.blue = 0.75;

	model.materials = {masked, blended, solid};
	model.groups = {{"layer:1", "Layer: Architecture", {{0, {0, 1, 2}}, {1, {3, 4, 5}}}},
	                {"layer:2", "Layer: Site", {{2, {6, 7, 8}}}},
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
