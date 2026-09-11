#include "GlbWriter.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

int main (int argumentCount, char** arguments)
{
	if (argumentCount != 2) {
		std::cerr << "Usage: DropViewGlbWriterFixture <output.glb>\n";
		return 2;
	}

	DropView::Glb::Model model;
	model.positions = {
		{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f},
		{0.0f, 0.0f, 1.0f}, {2.0f, 0.0f, 1.0f}, {0.0f, 3.0f, 1.0f}
	};
	model.normals.assign (6, {0.0f, 0.0f, 1.0f});
	model.textureCoordinates = {
		{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
		{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}
	};

	const std::vector<char> sharedImage {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n', 't', 'e', 's', 't'};
	DropView::Glb::Material masked;
	masked.name = "Masked \"surface\"";
	masked.alphaMask = true;
	masked.texture.mirrorX = true;
	masked.indices = {0, 1, 2};
	masked.imageData = sharedImage;
	masked.imageMimeType = "image/png";

	DropView::Glb::Material blended;
	blended.name = "Blended surface";
	blended.alpha = 0.5;
	blended.texture.mirrorY = true;
	blended.indices = {3, 4, 5};
	blended.imageData = sharedImage;
	blended.imageMimeType = "image/png";

	model.materials = {masked, blended};
	const std::vector<char> glb = DropView::Glb::BuildBinary (model, "Drop & View writer test");
	std::ofstream output (arguments[1], std::ios::binary | std::ios::trunc);
	if (!output)
		throw std::runtime_error ("Cannot create GLB fixture");
	output.write (glb.data (), static_cast<std::streamsize> (glb.size ())); 
	if (!output)
		throw std::runtime_error ("Cannot write GLB fixture");
	return 0;
}
