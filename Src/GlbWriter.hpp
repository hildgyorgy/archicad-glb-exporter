#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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
	double rotationRadians = 0.0;
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

enum class CameraProjection { Perspective, Orthographic };

struct SunSettings {
	double azimuthRadians = 0.0;
	double altitudeRadians = 0.0;
	Vec3 directionToSun {1.0f, 0.0f, 0.0f};
	Vec3 lightDirection {-1.0f, 0.0f, 0.0f};
	bool positionByDate = false;
	unsigned short year = 0;
	unsigned short month = 0;
	unsigned short day = 0;
	unsigned short hour = 0;
	unsigned short minute = 0;
	unsigned short second = 0;
	bool daylightSaving = false;
};

// Experimental Archicad Window Light data. These are source parameters, not a
// glTF punctual light or a photometrically calibrated area emitter.
struct LampParameter {
	std::string name;
	std::int32_t type = 0;
	double value = 0.0;
	std::string text;
	bool isText = false;
};

// Archicad's generated GDL LIGHT primitive, in glTF axis order. This is
// diagnostic data; it is not necessarily the Cineware area-light emitter.
struct ArchicadLightComponent {
	std::int32_t type = 0;
	std::int32_t shadowStatus = 0;
	Vec3 position {0.0f, 0.0f, 0.0f};
	Vec3 direction {0.0f, 0.0f, 0.0f};
	Vec3 color {0.0f, 0.0f, 0.0f};
	double radius = 0.0;
	double cosInner = 0.0;
	double cosOuter = 0.0;
	double angularFalloff = 0.0;
	double distanceStart = 0.0;
	double distanceEnd = 0.0;
	double distanceFalloff = 0.0;
};

struct WindowLight {
	std::string elementGuid;
	std::string libraryPartId;
	std::string libraryPartName;
	Vec3 position {0.0f, 0.0f, 0.0f};
	double rotationRadians = 0.0;
	bool reflected = false;
	bool enabled = false;
	Vec3 color {1.0f, 1.0f, 1.0f};
	double defaultA = 0.0;
	double defaultB = 0.0;
	double xRatio = 1.0;
	double yRatio = 1.0;
	std::vector<LampParameter> parameters;
	std::string lightComponentStatus = "unavailable";
	std::vector<ArchicadLightComponent> lightComponents;
};

SunSettings ConvertArchicadSunAngles (double azimuthDegrees, double altitudeDegrees);

struct InitialView {
	CameraProjection projection = CameraProjection::Perspective;
	Vec3 position {0.0f, 0.0f, 1.0f};
	Vec3 target {0.0f, 0.0f, 0.0f};
	Vec3 up {0.0f, 1.0f, 0.0f};
	double verticalFieldOfViewRadians = 0.7853981633974483;
	double orthographicXMag = 1.0;
	double orthographicYMag = 1.0;
	double nearPlane = 0.01;
	double farPlane = 1000.0;
	double archicadViewConeRadians = 0.0;
	double archicadRollAngleRadians = 0.0;
	bool archicadTwoPointPerspective = false;
	short archicadProjectionMode = 0;
	short archicadWindowWidth = 0;
	short archicadWindowHeight = 0;
	double archicadZoomScaleX = 1.0;
	double archicadZoomScaleY = 1.0;
	double archicadZoomDisplacementX = 0.0;
	double archicadZoomDisplacementY = 0.0;
	std::array<double, 12> archicadProjectionMatrix {};
	std::array<double, 12> archicadInverseProjectionMatrix {};
};

struct Model {
	std::vector<Vec3> positions;
	std::vector<Vec3> normals;
	std::vector<Vec2> textureCoordinates;
	std::vector<Material> materials;
	std::vector<Group> groups;
	std::string designCredits;
	// Explicit opt-in on every export; never inherited from saved preferences.
	bool allowMaterialEditing = false;
	std::optional<InitialView> initialView;
	std::optional<SunSettings> sun;
	std::vector<WindowLight> windowLights;
};

std::vector<char> BuildBinary (const Model& model, const std::string& generator);

} // namespace DropView::Glb
