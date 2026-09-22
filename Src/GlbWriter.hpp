#pragma once

#include <cstddef>
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

struct TextureImage {
	std::vector<char> data;
	std::string mimeType;
};

struct Material {
	std::int32_t sourceIndex = 0;
	std::string name;
	std::int32_t sourceMaterialType = 0;
	double sourceTransparencyPercent = 0.0;
	double sourceSpecularPercent = 0.0;
	double sourceShine = 0.0;
	double sourceEmissionAttenuation = 0.0;
	double red = 1.0;
	double green = 1.0;
	double blue = 1.0;
	double alpha = 1.0;
	double metallic = 0.0;
	double roughness = 1.0;
	double transmission = 0.0;
	double ior = 1.5;
	bool clearGlassOverride = false;
	double emissiveRed = 0.0;
	double emissiveGreen = 0.0;
	double emissiveBlue = 0.0;
	TextureParameters texture;
	bool alphaMask = false;
	std::vector<char> imageData;
	std::string imageMimeType;
	TextureImage normalTexture;
	TextureImage metallicRoughnessTexture;
	TextureImage occlusionTexture;
	TextureImage emissiveTexture;
};

struct Primitive {
	std::size_t materialIndex = 0;
	std::vector<std::uint32_t> indices;
};

struct Group {
	std::string key;
	std::string name;
	std::vector<Primitive> primitives;
};

struct Model {
	std::vector<Vec3> positions;
	std::vector<Vec3> normals;
	std::vector<Vec2> textureCoordinates;
	std::vector<Material> materials;
	std::vector<Group> groups;
};

std::vector<char> BuildBinary (const Model& model, const std::string& generator);

} // namespace DropView::Glb
