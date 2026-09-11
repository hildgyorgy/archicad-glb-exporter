#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace DropView::Glb {

struct Vec3 {
	float x;
	float y;
	float z;
};

struct Vec2 {
	float u;
	float v;
};

struct TextureParameters {
	double xSize = 1.0;
	double ySize = 1.0;
	double rotationDegrees = 0.0;
	bool mirrorX = false;
	bool mirrorY = false;
	bool useAlpha = false;
	bool transparencyPattern = false;
};

struct Material {
	std::int32_t sourceIndex = 0;
	std::string name;
	double red = 1.0;
	double green = 1.0;
	double blue = 1.0;
	double alpha = 1.0;
	TextureParameters texture;
	bool alphaMask = false;
	std::vector<std::uint32_t> indices;
	std::vector<char> imageData;
	std::string imageMimeType;
};

struct Model {
	std::vector<Vec3> positions;
	std::vector<Vec3> normals;
	std::vector<Vec2> textureCoordinates;
	std::vector<Material> materials;
};

std::vector<char> BuildBinary (const Model& model, const std::string& generator);

} // namespace DropView::Glb
