#include "GlbWriter.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace DropView::Glb {
namespace {

namespace Constant {
constexpr std::uint32_t GlbMagic = 0x46546C67;
constexpr std::uint32_t GlbVersion = 2;
constexpr std::uint32_t JsonChunk = 0x4E4F534A;
constexpr std::uint32_t BinaryChunk = 0x004E4942;
constexpr int FloatComponent = 5126;
constexpr int UnsignedShortComponent = 5123;
constexpr int UnsignedIntComponent = 5125;
constexpr int ArrayBuffer = 34962;
constexpr int ElementArrayBuffer = 34963;
constexpr int Repeat = 10497;
constexpr int MirroredRepeat = 33648;
constexpr int ArchicadPointLightType = 3; // APILght_PointID in the Archicad 28/29 DevKits.
} // namespace Constant

struct PackedGeometry {
	std::vector<Vec3> positions;
	std::vector<Vec3> normals;
	std::vector<Vec2> textureCoordinates;
	std::vector<Vec2> alternateTextureCoordinates;
	std::vector<std::uint32_t> indices;
	Vec3 minimum {std::numeric_limits<float>::max (), std::numeric_limits<float>::max (),
	              std::numeric_limits<float>::max ()};
	Vec3 maximum {-minimum.x, -minimum.y, -minimum.z};
};

using VertexKey = std::array<std::uint32_t, 10>;

struct VertexKeyHash {
	std::size_t operator() (const VertexKey& key) const
	{
		std::size_t hash = 0;
		for (const std::uint32_t component : key)
			hash ^= std::hash<std::uint32_t> {} (component) + 0x9e3779b9U + (hash << 6) + (hash >> 2);
		return hash;
	}
};

VertexKey MakeVertexKey (const Vec3& position, const Vec3& normal, const Vec2& textureCoordinate,
                         const Vec2& alternateTextureCoordinate)
{
	// Compare the eight stored float components, not struct bytes (which may contain padding).
	// Bitwise equality avoids welding across even the smallest normal or UV difference.
	return {std::bit_cast<std::uint32_t> (position.x), std::bit_cast<std::uint32_t> (position.y),
	        std::bit_cast<std::uint32_t> (position.z), std::bit_cast<std::uint32_t> (normal.x),
	        std::bit_cast<std::uint32_t> (normal.y), std::bit_cast<std::uint32_t> (normal.z),
	        std::bit_cast<std::uint32_t> (textureCoordinate.u), std::bit_cast<std::uint32_t> (textureCoordinate.v),
	        std::bit_cast<std::uint32_t> (alternateTextureCoordinate.u),
	        std::bit_cast<std::uint32_t> (alternateTextureCoordinate.v)};
}

struct BufferView {
	std::uint32_t offset;
	std::uint32_t length;
	std::optional<int> target;
};

struct Accessor {
	std::size_t bufferView;
	int componentType;
	std::size_t count;
	const char* type;
	std::optional<Vec3> minimum;
	std::optional<Vec3> maximum;
};

struct PrimitiveReferences {
	std::size_t materialIndex;
	std::optional<std::size_t> alternateMaterialIndex;
	std::size_t positionAccessor;
	std::size_t normalAccessor;
	std::size_t textureCoordinateAccessor;
	std::optional<std::size_t> alternateTextureCoordinateAccessor;
	std::size_t indexAccessor;
};

struct PackedPrimitive {
	std::size_t materialIndex;
	std::optional<std::size_t> alternateMaterialIndex;
	PackedGeometry geometry;
};

struct PackedGroup {
	std::string name;
	std::vector<PackedPrimitive> primitives;
};

struct EmbeddedImage {
	std::size_t bufferView;
	const std::vector<char>* data;
	std::string mimeType;
};

struct TextureBinding {
	std::size_t materialIndex;
	std::size_t imageIndex;
};

struct CameraNodeTransform {
	std::array<double, 16> matrix;
};

Vec3 Subtract (const Vec3& left, const Vec3& right)
{
	return {left.x - right.x, left.y - right.y, left.z - right.z};
}

double Length (const Vec3& value)
{
	return std::sqrt (static_cast<double> (value.x) * value.x + static_cast<double> (value.y) * value.y +
	                  static_cast<double> (value.z) * value.z);
}

Vec3 Normalize (const Vec3& value, const char* description)
{
	const double length = Length (value);
	if (!std::isfinite (length) || length <= 1.0e-9)
		throw std::invalid_argument (std::string (description) + " has zero length");
	return {static_cast<float> (value.x / length), static_cast<float> (value.y / length),
	        static_cast<float> (value.z / length)};
}

Vec3 Cross (const Vec3& left, const Vec3& right)
{
	return {left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z,
	        left.x * right.y - left.y * right.x};
}

struct RectangularWindowEmitter {
	Vec3 center;
	Vec3 normal;
	Vec3 widthAxis;
	Vec3 heightAxis;
	double width;
	double height;
};

std::optional<RectangularWindowEmitter> DeriveRectangularWindowEmitter (const WindowLight& light)
{
	// The generated point-light component supplies the world-space center and
	// inward normal in the tested Window Light 27 exports. Its black colour and
	// falloff are not a calibrated rectangular emitter.
	if (light.lightComponentStatus != "available" || light.lightComponents.size () != 1 ||
	    light.lightComponents.front ().type != Constant::ArchicadPointLightType)
		return std::nullopt;

	double width = 0.0;
	double height = 0.0;
	bool rectangular = false;
	for (const LampParameter& parameter : light.parameters) {
		if (parameter.name == "iC4dDetAreaShape" && !parameter.isText)
			// Archicad 29 Window Light 27: 2 is the rectangular shape in the
			// inspected export. Do not infer shapes from localized display text.
			rectangular = parameter.value == 2.0;
		else if (parameter.name == "c4dDetAreaX" && !parameter.isText)
			width = parameter.value;
		else if (parameter.name == "c4dDetAreaY" && !parameter.isText)
			height = parameter.value;
	}
	if (!rectangular || !std::isfinite (width) || !std::isfinite (height) || width <= 0.0 || height <= 0.0)
		return std::nullopt;

	const ArchicadLightComponent& component = light.lightComponents.front ();
	const Vec3& center = component.position;
	const Vec3& direction = component.direction;
	if (!std::isfinite (center.x) || !std::isfinite (center.y) || !std::isfinite (center.z) ||
	    !std::isfinite (direction.x) || !std::isfinite (direction.y) || !std::isfinite (direction.z))
		return std::nullopt;
	const double directionLength = Length (direction);
	// This interpretation is established for upright window lights. Avoid
	// inventing an in-plane roll for a tilted or vertical-facing emitter.
	if (!std::isfinite (directionLength) || directionLength <= 1.0e-9 ||
	    std::abs (direction.y / directionLength) > 1.0e-4)
		return std::nullopt;
	const double horizontalLength = std::hypot (direction.x, direction.z);
	const Vec3 normal {static_cast<float> (direction.x / horizontalLength), 0.0f,
	                   static_cast<float> (direction.z / horizontalLength)};
	const Vec3 heightAxis {0.0f, 1.0f, 0.0f};
	const Vec3 widthAxis = Cross (heightAxis, normal);
	return RectangularWindowEmitter {center, normal, widthAxis, heightAxis, width, height};
}

CameraNodeTransform BuildCameraNodeTransform (const InitialView& view)
{
	const Vec3 forward = Normalize (Subtract (view.target, view.position), "Camera viewing direction");
	const Vec3 right = Normalize (Cross (forward, view.up), "Camera right direction");
	const Vec3 up = Normalize (Cross (right, forward), "Camera up direction");
	const Vec3 backward {-forward.x, -forward.y, -forward.z};
	return {{{right.x, right.y, right.z, 0.0, up.x, up.y, up.z, 0.0, backward.x, backward.y, backward.z, 0.0,
	          view.position.x, view.position.y, view.position.z, 1.0}}};
}

void WriteVec3 (std::ostringstream& json, const Vec3& value)
{
	json << '[' << value.x << ',' << value.y << ',' << value.z << ']';
}

void WriteAccessorBound (std::ostringstream& json, const Vec3& value)
{
	const auto flags = json.flags ();
	const auto precision = json.precision ();
	json << std::defaultfloat << std::setprecision (std::numeric_limits<float>::max_digits10);
	WriteVec3 (json, value);
	json.flags (flags);
	json.precision (precision);
}

template <std::size_t Size> void WriteDoubleArray (std::ostringstream& json, const std::array<double, Size>& values)
{
	json << '[';
	for (std::size_t index = 0; index < Size; ++index) {
		if (index > 0)
			json << ',';
		json << values[index];
	}
	json << ']';
}

std::uint32_t CheckedU32 (std::size_t value, const char* description)
{
	if (value > std::numeric_limits<std::uint32_t>::max ())
		throw std::length_error (std::string (description) + " exceeds the GLB 32-bit size limit");
	return static_cast<std::uint32_t> (value);
}

void AppendU32 (std::vector<char>& data, std::uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8)
		data.push_back (static_cast<char> ((value >> shift) & 0xff));
}

void AppendBytes (std::vector<char>& data, const void* source, std::size_t size)
{
	if (size == 0)
		return;
	const char* bytes = static_cast<const char*> (source);
	data.insert (data.end (), bytes, bytes + size);
}

std::string EscapeJsonString (const std::string& value)
{
	std::ostringstream escaped;
	for (const unsigned char character : value) {
		switch (character) {
			case '\"':
				escaped << "\\\"";
				break;
			case '\\':
				escaped << "\\\\";
				break;
			case '\b':
				escaped << "\\b";
				break;
			case '\f':
				escaped << "\\f";
				break;
			case '\n':
				escaped << "\\n";
				break;
			case '\r':
				escaped << "\\r";
				break;
			case '\t':
				escaped << "\\t";
				break;
			default:
				if (character < 0x20)
					escaped << "\\u" << std::hex << std::setw (4) << std::setfill ('0') << static_cast<int> (character)
					        << std::dec;
				else
					escaped << static_cast<char> (character);
		}
	}
	return escaped.str ();
}

class LayoutBuilder {
public:
	template <class Item> std::size_t AddTypedBufferView (const std::vector<Item>& values, int target)
	{
		return AddBufferView (values.data (), values.size () * sizeof (Item), target);
	}

	std::size_t AddImageBufferView (const std::vector<char>& bytes)
	{
		return AddBufferView (bytes.data (), bytes.size (), std::nullopt);
	}

	std::size_t AddAccessor (std::size_t bufferView, int componentType, std::size_t count, const char* type,
	                         std::optional<Vec3> minimum = std::nullopt, std::optional<Vec3> maximum = std::nullopt)
	{
		accessors.push_back ({bufferView, componentType, count, type, minimum, maximum});
		return accessors.size () - 1;
	}

	std::vector<char> binary;
	std::vector<BufferView> bufferViews;
	std::vector<Accessor> accessors;

private:
	std::size_t AddBufferView (const void* data, std::size_t size, std::optional<int> target)
	{
		while (binary.size () % 4 != 0)
			binary.push_back (0);
		const std::uint32_t offset = CheckedU32 (binary.size (), "Buffer offset");
		AppendBytes (binary, data, size);
		bufferViews.push_back ({offset, CheckedU32 (size, "Buffer view"), target});
		return bufferViews.size () - 1;
	}
};

PackedGeometry PackGeometry (const Model& model, const Primitive& primitive)
{
	if (primitive.materialIndex >= model.materials.size ())
		throw std::out_of_range ("Primitive references an invalid material");
	if (primitive.alternateMaterialIndex.has_value () && *primitive.alternateMaterialIndex >= model.materials.size ())
		throw std::out_of_range ("Primitive references an invalid alternate material");
	if (primitive.indices.empty () || primitive.indices.size () % 3 != 0)
		throw std::invalid_argument ("Every primitive must contain complete triangles");

	PackedGeometry packed;
	std::unordered_map<VertexKey, std::uint32_t, VertexKeyHash> remappedIndices;
	for (const std::uint32_t sourceIndex : primitive.indices) {
		if (sourceIndex >= model.positions.size ())
			throw std::out_of_range ("Primitive index exceeds the vertex arrays");
		const Vec3& position = model.positions[sourceIndex];
		const Vec3& normal = model.normals[sourceIndex];
		const Vec2& textureCoordinate = model.textureCoordinates[sourceIndex];
		const Vec2 alternateTextureCoordinate = model.alternateTextureCoordinates.empty ()
		                                            ? Vec2 {0.0f, 0.0f}
		                                            : model.alternateTextureCoordinates[sourceIndex];
		if (!std::isfinite (position.x) || !std::isfinite (position.y) || !std::isfinite (position.z) ||
		    !std::isfinite (normal.x) || !std::isfinite (normal.y) || !std::isfinite (normal.z) ||
		    !std::isfinite (textureCoordinate.u) || !std::isfinite (textureCoordinate.v) ||
		    !std::isfinite (alternateTextureCoordinate.u) || !std::isfinite (alternateTextureCoordinate.v))
			throw std::invalid_argument ("Primitive contains a non-finite vertex attribute");
		const VertexKey key = MakeVertexKey (position, normal, textureCoordinate, alternateTextureCoordinate);
		auto remapped = remappedIndices.find (key);
		if (remapped == remappedIndices.end ()) {
			const std::uint32_t targetIndex = CheckedU32 (packed.positions.size (), "Vertex count");
			remappedIndices.emplace (key, targetIndex);
			packed.positions.push_back (position);
			packed.normals.push_back (normal);
			packed.textureCoordinates.push_back (textureCoordinate);
			if (!model.alternateTextureCoordinates.empty ())
				packed.alternateTextureCoordinates.push_back (alternateTextureCoordinate);
			const Vec3& point = packed.positions.back ();
			packed.minimum.x = std::min (packed.minimum.x, point.x);
			packed.minimum.y = std::min (packed.minimum.y, point.y);
			packed.minimum.z = std::min (packed.minimum.z, point.z);
			packed.maximum.x = std::max (packed.maximum.x, point.x);
			packed.maximum.y = std::max (packed.maximum.y, point.y);
			packed.maximum.z = std::max (packed.maximum.z, point.z);
			packed.indices.push_back (targetIndex);
		} else {
			packed.indices.push_back (remapped->second);
		}
	}
	return packed;
}

std::vector<PackedGroup> PackGroups (const Model& model)
{
	if (model.positions.size () != model.normals.size () || model.positions.size () != model.textureCoordinates.size ())
		throw std::invalid_argument ("Position, normal and texture-coordinate counts differ");
	if (!model.alternateTextureCoordinates.empty () &&
	    model.alternateTextureCoordinates.size () != model.positions.size ())
		throw std::invalid_argument ("Alternate texture-coordinate count differs");
	if (model.groups.empty ())
		throw std::invalid_argument ("The GLB model has no groups");

	std::vector<PackedGroup> result;
	result.reserve (model.groups.size ());
	for (const Group& group : model.groups) {
		if (group.name.empty () || group.primitives.empty ())
			throw std::invalid_argument ("Every group must have a name and geometry");
		PackedGroup packedGroup;
		packedGroup.name = group.name;
		packedGroup.primitives.reserve (group.primitives.size ());
		for (const Primitive& primitive : group.primitives)
			packedGroup.primitives.push_back ({primitive.materialIndex, primitive.alternateMaterialIndex,
			                                   PackGeometry (model, primitive)});
		result.push_back (std::move (packedGroup));
	}
	return result;
}

} // namespace

SunSettings ConvertArchicadSunAngles (double azimuthDegrees, double altitudeDegrees)
{
	constexpr double Pi = 3.14159265358979323846;
	constexpr double DegreesToRadians = Pi / 180.0;
	if (!std::isfinite (azimuthDegrees) || !std::isfinite (altitudeDegrees))
		throw std::invalid_argument ("Archicad sun angles must be finite");

	SunSettings sun;
	sun.azimuthRadians = azimuthDegrees * DegreesToRadians;
	sun.altitudeRadians = altitudeDegrees * DegreesToRadians;
	const double horizontal = std::cos (sun.altitudeRadians);
	// Archicad's polar coordinates use East as azimuth zero. Convert Z-up Archicad coordinates to Y-up glTF.
	sun.directionToSun = Normalize (
	    {static_cast<float> (horizontal * std::cos (sun.azimuthRadians)),
	     static_cast<float> (std::sin (sun.altitudeRadians)),
	     static_cast<float> (-horizontal * std::sin (sun.azimuthRadians))},
	    "Direction toward the sun");
	sun.lightDirection = {-sun.directionToSun.x, -sun.directionToSun.y, -sun.directionToSun.z};
	return sun;
}

std::vector<char> BuildBinary (const Model& model, const std::string& generator)
{
	if (model.materials.empty ())
		throw std::invalid_argument ("The GLB model has no materials");

	const std::vector<PackedGroup> packedGroups = PackGroups (model);
	LayoutBuilder layout;
	std::vector<std::vector<PrimitiveReferences>> primitiveReferences;
	primitiveReferences.reserve (packedGroups.size ());
	for (const PackedGroup& group : packedGroups) {
		primitiveReferences.emplace_back ();
		primitiveReferences.back ().reserve (group.primitives.size ());
		for (const PackedPrimitive& primitive : group.primitives) {
			const PackedGeometry& packed = primitive.geometry;
			const std::size_t positionView = layout.AddTypedBufferView (packed.positions, Constant::ArrayBuffer);
			const std::size_t normalView = layout.AddTypedBufferView (packed.normals, Constant::ArrayBuffer);
			const std::size_t textureCoordinateView =
			    layout.AddTypedBufferView (packed.textureCoordinates, Constant::ArrayBuffer);
			std::optional<std::size_t> alternateTextureCoordinateAccessor;
			if (!packed.alternateTextureCoordinates.empty ()) {
				const std::size_t view = layout.AddTypedBufferView (packed.alternateTextureCoordinates,
				                                                     Constant::ArrayBuffer);
				alternateTextureCoordinateAccessor = layout.AddAccessor (
				    view, Constant::FloatComponent, packed.alternateTextureCoordinates.size (), "VEC2");
			}
			std::size_t indexView;
			int indexComponentType;
			if (packed.positions.size () <= static_cast<std::size_t> (std::numeric_limits<std::uint16_t>::max ()) + 1) {
				std::vector<std::uint16_t> compactIndices;
				compactIndices.reserve (packed.indices.size ());
				std::transform (packed.indices.begin (), packed.indices.end (), std::back_inserter (compactIndices),
				                [] (std::uint32_t index) { return static_cast<std::uint16_t> (index); });
				indexView = layout.AddTypedBufferView (compactIndices, Constant::ElementArrayBuffer);
				indexComponentType = Constant::UnsignedShortComponent;
			} else {
				indexView = layout.AddTypedBufferView (packed.indices, Constant::ElementArrayBuffer);
				indexComponentType = Constant::UnsignedIntComponent;
			}
			primitiveReferences.back ().push_back (
			    {primitive.materialIndex, primitive.alternateMaterialIndex,
			     layout.AddAccessor (positionView, Constant::FloatComponent, packed.positions.size (), "VEC3",
			                         packed.minimum, packed.maximum),
			     layout.AddAccessor (normalView, Constant::FloatComponent, packed.normals.size (), "VEC3"),
			     layout.AddAccessor (textureCoordinateView, Constant::FloatComponent, packed.textureCoordinates.size (),
			                         "VEC2"),
			     alternateTextureCoordinateAccessor,
			     layout.AddAccessor (indexView, indexComponentType, packed.indices.size (), "SCALAR")});
		}
	}

	std::vector<EmbeddedImage> embeddedImages;
	std::vector<TextureBinding> textureBindings;
	std::vector<int> materialTextureIndices (model.materials.size (), -1);
	std::vector<int> materialNormalTextureIndices (model.materials.size (), -1);
	std::vector<int> materialMetallicRoughnessTextureIndices (model.materials.size (), -1);
	std::vector<int> materialOcclusionTextureIndices (model.materials.size (), -1);
	std::vector<int> materialEmissiveTextureIndices (model.materials.size (), -1);
	auto addTexture = [&] (std::size_t materialIndex, const std::vector<char>& data, const std::string& mimeType) {
		if (data.empty ())
			return -1;
		auto existingImage =
		    std::find_if (embeddedImages.begin (), embeddedImages.end (), [&] (const EmbeddedImage& image) {
			    return image.mimeType == mimeType && *image.data == data;
		    });
		std::size_t imageIndex = 0;
		if (existingImage == embeddedImages.end ()) {
			imageIndex = embeddedImages.size ();
			embeddedImages.push_back ({layout.AddImageBufferView (data), &data, mimeType});
		} else {
			imageIndex = static_cast<std::size_t> (std::distance (embeddedImages.begin (), existingImage));
		}
		const int textureIndex = static_cast<int> (textureBindings.size ());
		textureBindings.push_back ({materialIndex, imageIndex});
		return textureIndex;
	};
	for (std::size_t i = 0; i < model.materials.size (); ++i) {
		const Material& material = model.materials[i];
		materialTextureIndices[i] = addTexture (i, material.imageData, material.imageMimeType);
		materialNormalTextureIndices[i] = addTexture (i, material.normalTexture.data, material.normalTexture.mimeType);
		materialMetallicRoughnessTextureIndices[i] =
		    addTexture (i, material.metallicRoughnessTexture.data, material.metallicRoughnessTexture.mimeType);
		materialOcclusionTextureIndices[i] =
		    addTexture (i, material.occlusionTexture.data, material.occlusionTexture.mimeType);
		materialEmissiveTextureIndices[i] =
		    addTexture (i, material.emissiveTexture.data, material.emissiveTexture.mimeType);
	}
	while (layout.binary.size () % 4 != 0)
		layout.binary.push_back (0);
	const bool usesTransmission = std::any_of (model.materials.begin (), model.materials.end (),
	                                           [] (const Material& material) { return material.transmission > 0.0; });

	std::optional<CameraNodeTransform> cameraTransform;
	if (model.initialView.has_value ()) {
		const InitialView& view = *model.initialView;
		if (!std::isfinite (view.nearPlane) || view.nearPlane <= 0.0)
			throw std::invalid_argument ("Camera near plane must be positive");
		if (view.projection == CameraProjection::Perspective &&
		    (!std::isfinite (view.verticalFieldOfViewRadians) || view.verticalFieldOfViewRadians <= 0.0 ||
		     view.verticalFieldOfViewRadians >= 3.14159265358979323846))
			throw std::invalid_argument ("Perspective initial-view field of view is invalid");
		if (view.projection == CameraProjection::Orthographic &&
		    (!std::isfinite (view.orthographicXMag) || !std::isfinite (view.orthographicYMag) ||
		     !std::isfinite (view.farPlane) || view.orthographicXMag <= 0.0 || view.orthographicYMag <= 0.0 ||
		     view.farPlane <= view.nearPlane))
			throw std::invalid_argument ("Orthographic camera bounds are invalid");
		cameraTransform = BuildCameraNodeTransform (view);
	}

	std::ostringstream json;
	json << std::fixed << std::setprecision (6) << "{\"asset\":{\"version\":\"2.0\",\"generator\":\""
	     << EscapeJsonString (generator) << '"';
	if (model.initialView.has_value () || model.sun.has_value () || !model.windowLights.empty () ||
	    !model.designCredits.empty ()) {
		json << ",\"extras\":{\"dropView\":{\"schemaVersion\":1";
		if (!model.designCredits.empty ())
			json << ",\"designCredits\":\"" << EscapeJsonString (model.designCredits) << '"';
		json << "}}";
	}
	json << "},";
	if (usesTransmission || !model.alternateMaterialVariantName.empty ()) {
		json << "\"extensionsUsed\":[";
		if (usesTransmission)
			json << "\"KHR_materials_transmission\",\"KHR_materials_ior\"";
		if (!model.alternateMaterialVariantName.empty ()) {
			if (usesTransmission)
				json << ',';
			json << "\"KHR_materials_variants\"";
		}
		json << "],";
	}
	if (!model.alternateMaterialVariantName.empty ())
		json << "\"extensions\":{\"KHR_materials_variants\":{\"variants\":[{\"name\":\""
		     << EscapeJsonString (model.alternateMaterialVariantName) << "\"}]}},";
	json << "\"scene\":0,\"scenes\":[{\"nodes\":[";
	for (std::size_t i = 0; i < model.groups.size (); ++i) {
		if (i > 0)
			json << ',';
		json << i;
	}
	if (model.initialView.has_value ()) {
		if (!model.groups.empty ())
			json << ',';
		json << model.groups.size ();
	}
	json << ']';
	if (model.initialView.has_value () || model.sun.has_value () || !model.windowLights.empty ()) {
		json << ",\"extras\":{\"dropView\":{";
		bool wroteSceneMetadata = false;
		if (model.initialView.has_value ()) {
			const InitialView& view = *model.initialView;
			json << "\"initialView\":{\"camera\":0,\"projection\":\""
			     << (view.projection == CameraProjection::Perspective ? "perspective" : "orthographic")
			     << "\",\"position\":";
			WriteVec3 (json, view.position);
			json << ",\"target\":";
			WriteVec3 (json, view.target);
			json << ",\"up\":";
			WriteVec3 (json, view.up);
			json << ",\"archicad\":{\"viewCone\":" << view.archicadViewConeRadians
			     << ",\"rollAngle\":" << view.archicadRollAngleRadians
			     << ",\"twoPointPerspective\":" << (view.archicadTwoPointPerspective ? "true" : "false")
			     << ",\"projectionMode\":" << view.archicadProjectionMode << ",\"windowSize\":["
			     << view.archicadWindowWidth << ',' << view.archicadWindowHeight << "],\"zoomScale\":["
			     << view.archicadZoomScaleX << ',' << view.archicadZoomScaleY << "],\"zoomDisplacement\":["
			     << view.archicadZoomDisplacementX << ',' << view.archicadZoomDisplacementY
			     << "],\"projectionMatrix\":";
			WriteDoubleArray (json, view.archicadProjectionMatrix);
			json << ",\"inverseProjectionMatrix\":";
			WriteDoubleArray (json, view.archicadInverseProjectionMatrix);
			json << "}}";
			wroteSceneMetadata = true;
		}
		if (model.sun.has_value ()) {
			const SunSettings& sun = *model.sun;
			if (wroteSceneMetadata)
				json << ',';
			json << "\"sun\":{\"azimuth\":" << sun.azimuthRadians << ",\"altitude\":"
			     << sun.altitudeRadians << ",\"directionToSun\":";
			WriteVec3 (json, sun.directionToSun);
			json << ",\"lightDirection\":";
			WriteVec3 (json, sun.lightDirection);
			json << ",\"positionMode\":\"" << (sun.positionByDate ? "dateTime" : "angles")
			     << "\",\"dateTime\":{\"year\":" << sun.year << ",\"month\":" << sun.month
			     << ",\"day\":" << sun.day << ",\"hour\":" << sun.hour << ",\"minute\":" << sun.minute
			     << ",\"second\":" << sun.second << ",\"daylightSaving\":"
			     << (sun.daylightSaving ? "true" : "false") << "}}";
			wroteSceneMetadata = true;
		}
		if (!model.windowLights.empty ()) {
			if (wroteSceneMetadata)
				json << ',';
			json << "\"windowLights\":[";
			for (std::size_t i = 0; i < model.windowLights.size (); ++i) {
				if (i > 0)
					json << ',';
				const WindowLight& light = model.windowLights[i];
				json << "{\"source\":\"archicad-window-light\",\"elementGuid\":\""
				     << EscapeJsonString (light.elementGuid) << "\",\"libraryPartId\":\""
				     << EscapeJsonString (light.libraryPartId) << "\",\"libraryPartName\":\""
				     << EscapeJsonString (light.libraryPartName) << "\",\"position\":";
				WriteVec3 (json, light.position);
				json << ",\"rotationRadians\":" << light.rotationRadians
				     << ",\"reflected\":" << (light.reflected ? "true" : "false")
				     << ",\"enabled\":" << (light.enabled ? "true" : "false") << ",\"color\":";
				WriteVec3 (json, light.color);
				json << ",\"defaultSize\":[" << light.defaultA << ',' << light.defaultB << ']'
				     << ",\"sizeRatios\":[" << light.xRatio << ',' << light.yRatio << ']'
				     << ",\"parameters\":[";
				for (std::size_t p = 0; p < light.parameters.size (); ++p) {
					if (p > 0)
						json << ',';
					const LampParameter& parameter = light.parameters[p];
					json << "{\"name\":\"" << EscapeJsonString (parameter.name) << "\",\"type\":"
					     << parameter.type << ",\"value\":";
					if (parameter.isText)
						json << '"' << EscapeJsonString (parameter.text) << '"';
					else
						json << parameter.value;
					json << '}';
				}
				json << "],\"lightComponentStatus\":\"" << EscapeJsonString (light.lightComponentStatus)
				     << "\",\"lightComponents\":[";
				for (std::size_t c = 0; c < light.lightComponents.size (); ++c) {
					if (c > 0)
						json << ',';
					const ArchicadLightComponent& component = light.lightComponents[c];
					json << "{\"coordinateFrame\":\"sourceLightLocalAxisMapped\",\"type\":"
					     << component.type << ",\"shadowStatus\":" << component.shadowStatus
					     << ",\"position\":";
					WriteVec3 (json, component.position);
					json << ",\"direction\":";
					WriteVec3 (json, component.direction);
					json << ",\"color\":";
					WriteVec3 (json, component.color);
					json << ",\"radius\":" << component.radius << ",\"cosInner\":" << component.cosInner
					     << ",\"cosOuter\":" << component.cosOuter
					     << ",\"angularFalloff\":" << component.angularFalloff
					     << ",\"distanceStart\":" << component.distanceStart
					     << ",\"distanceEnd\":" << component.distanceEnd
					     << ",\"distanceFalloff\":" << component.distanceFalloff << '}';
				}
				json << ']';
				if (const auto emitter = DeriveRectangularWindowEmitter (light); emitter.has_value ()) {
					json << ",\"rectangularEmitter\":{\"coordinateFrame\":\"gltfWorld\",\"center\":";
					WriteVec3 (json, emitter->center);
					json << ",\"normal\":";
					WriteVec3 (json, emitter->normal);
					json << ",\"widthAxis\":";
					WriteVec3 (json, emitter->widthAxis);
					json << ",\"heightAxis\":";
					WriteVec3 (json, emitter->heightAxis);
					json << ",\"width\":" << emitter->width << ",\"height\":" << emitter->height << '}';
				}
				json << '}';
			}
			json << ']';
		}
		json << "}}";
	}
	json << "}],\"nodes\":[";
	for (std::size_t i = 0; i < model.groups.size (); ++i) {
		if (i > 0)
			json << ',';
		json << "{\"mesh\":" << i << ",\"name\":\"" << EscapeJsonString (model.groups[i].name) << "\"}";
	}
	if (model.initialView.has_value ()) {
		if (!model.groups.empty ())
			json << ',';
		json << "{\"camera\":0,\"name\":\"Archicad active 3D view\",\"matrix\":";
		WriteDoubleArray (json, cameraTransform->matrix);
		json << '}';
	}
	json << "],\"meshes\":[";
	for (std::size_t i = 0; i < model.groups.size (); ++i) {
		if (i > 0)
			json << ',';
		json << "{\"name\":\"" << EscapeJsonString (model.groups[i].name) << "\",\"primitives\":[";
		for (std::size_t primitiveIndex = 0; primitiveIndex < primitiveReferences[i].size (); ++primitiveIndex) {
			if (primitiveIndex > 0)
				json << ',';
			const PrimitiveReferences& references = primitiveReferences[i][primitiveIndex];
			json << "{\"attributes\":{\"POSITION\":" << references.positionAccessor
			     << ",\"NORMAL\":" << references.normalAccessor
			     << ",\"TEXCOORD_0\":" << references.textureCoordinateAccessor;
			if (references.alternateTextureCoordinateAccessor.has_value ())
				json << ",\"TEXCOORD_1\":" << *references.alternateTextureCoordinateAccessor;
			json << "},\"indices\":" << references.indexAccessor << ",\"material\":" << references.materialIndex;
			if (references.alternateMaterialIndex.has_value ())
				json << ",\"extensions\":{\"KHR_materials_variants\":{\"mappings\":[{\"material\":"
				     << *references.alternateMaterialIndex << ",\"variants\":[0]}]}}";
			json << '}';
		}
		json << "]}";
	}
	json << "],\"materials\":[";
	for (std::size_t i = 0; i < model.materials.size (); ++i) {
		if (i > 0)
			json << ',';
		const Material& material = model.materials[i];
		const bool hasBaseColorTexture = materialTextureIndices[i] >= 0;
		const double red = hasBaseColorTexture && !material.multiplyBaseColorTexture ? 1.0 : material.red;
		const double green = hasBaseColorTexture && !material.multiplyBaseColorTexture ? 1.0 : material.green;
		const double blue = hasBaseColorTexture && !material.multiplyBaseColorTexture ? 1.0 : material.blue;
		json << "{\"name\":\"" << EscapeJsonString (material.name)
		     << "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[" << red << ',' << green << ',' << blue << ','
		     << material.alpha << "],\"metallicFactor\":" << material.metallic
		     << ",\"roughnessFactor\":" << material.roughness;
		if (hasBaseColorTexture) {
			json << ",\"baseColorTexture\":{\"index\":" << materialTextureIndices[i];
			if (material.textureCoordinateSet != 0)
				json << ",\"texCoord\":" << material.textureCoordinateSet;
			json << '}';
		}
		if (materialMetallicRoughnessTextureIndices[i] >= 0) {
			json << ",\"metallicRoughnessTexture\":{\"index\":" << materialMetallicRoughnessTextureIndices[i];
			if (material.textureCoordinateSet != 0)
				json << ",\"texCoord\":" << material.textureCoordinateSet;
			json << '}';
		}
		json << '}';
		if (materialNormalTextureIndices[i] >= 0) {
			json << ",\"normalTexture\":{\"index\":" << materialNormalTextureIndices[i];
			if (material.textureCoordinateSet != 0)
				json << ",\"texCoord\":" << material.textureCoordinateSet;
			json << '}';
		}
		if (materialOcclusionTextureIndices[i] >= 0) {
			json << ",\"occlusionTexture\":{\"index\":" << materialOcclusionTextureIndices[i];
			if (material.textureCoordinateSet != 0)
				json << ",\"texCoord\":" << material.textureCoordinateSet;
			json << '}';
		}
		if (materialEmissiveTextureIndices[i] >= 0) {
			const bool hasEmissiveFactor =
			    material.emissiveRed > 0.0 || material.emissiveGreen > 0.0 || material.emissiveBlue > 0.0;
			json << ",\"emissiveTexture\":{\"index\":" << materialEmissiveTextureIndices[i];
			if (material.textureCoordinateSet != 0)
				json << ",\"texCoord\":" << material.textureCoordinateSet;
			json << "},\"emissiveFactor\":["
			     << (hasEmissiveFactor ? material.emissiveRed : 1.0) << ','
			     << (hasEmissiveFactor ? material.emissiveGreen : 1.0) << ','
			     << (hasEmissiveFactor ? material.emissiveBlue : 1.0) << ']';
		} else if (material.emissiveRed > 0.0 || material.emissiveGreen > 0.0 || material.emissiveBlue > 0.0) {
			json << ",\"emissiveFactor\":[" << material.emissiveRed << ',' << material.emissiveGreen << ','
			     << material.emissiveBlue << ']';
		}
		if (material.transmission > 0.0) {
			json << ",\"extensions\":{\"KHR_materials_transmission\":{\"transmissionFactor\":" << material.transmission
			     << "},\"KHR_materials_ior\":{\"ior\":" << material.ior << "}}";
		}
		if (material.alpha < 1.0)
			json << ",\"alphaMode\":\"BLEND\"";
		else if (material.alphaMask)
			json << ",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5";
		json << ",\"doubleSided\":true,\"extras\":{\"archicad\":{\"surfaceIndex\":" << material.sourceIndex
		     << ",\"surfaceName\":\"" << EscapeJsonString (material.name)
		     << "\",\"materialType\":" << material.sourceMaterialType
		     << ",\"transparencyPercent\":" << material.sourceTransparencyPercent
		     << ",\"specularPercent\":" << material.sourceSpecularPercent << ",\"shine\":" << material.sourceShine
		     << ",\"emissionAttenuation\":" << material.sourceEmissionAttenuation
		     << ",\"clearGlassOverride\":" << (material.clearGlassOverride ? "true" : "false") << "}}}";
	}
	json << ']';
	if (model.initialView.has_value ()) {
		const InitialView& view = *model.initialView;
		json << ",\"cameras\":[{\"name\":\"Archicad active 3D view\",\"type\":\""
		     << (view.projection == CameraProjection::Perspective ? "perspective" : "orthographic") << '\"';
		if (view.projection == CameraProjection::Perspective)
			json << ",\"perspective\":{\"yfov\":" << view.verticalFieldOfViewRadians << ",\"znear\":" << view.nearPlane
			     << '}';
		else
			json << ",\"orthographic\":{\"xmag\":" << view.orthographicXMag << ",\"ymag\":" << view.orthographicYMag
			     << ",\"znear\":" << view.nearPlane << ",\"zfar\":" << view.farPlane << '}';
		json << "}]";
	}
	if (!textureBindings.empty ()) {
		json << ",\"textures\":[";
		for (std::size_t i = 0; i < textureBindings.size (); ++i) {
			if (i > 0)
				json << ',';
			json << "{\"source\":" << textureBindings[i].imageIndex << ",\"sampler\":" << i << '}';
		}
		json << "],\"samplers\":[";
		for (std::size_t i = 0; i < textureBindings.size (); ++i) {
			if (i > 0)
				json << ',';
			const Material& material = model.materials[textureBindings[i].materialIndex];
			json << "{\"wrapS\":" << (material.texture.mirrorX ? Constant::MirroredRepeat : Constant::Repeat)
			     << ",\"wrapT\":" << (material.texture.mirrorY ? Constant::MirroredRepeat : Constant::Repeat) << '}';
		}
		json << "],\"images\":[";
		for (std::size_t i = 0; i < embeddedImages.size (); ++i) {
			if (i > 0)
				json << ',';
			const EmbeddedImage& image = embeddedImages[i];
			json << "{\"bufferView\":" << image.bufferView << ",\"mimeType\":\"" << EscapeJsonString (image.mimeType)
			     << "\"}";
		}
		json << ']';
	}
	json << ",\"buffers\":[{\"byteLength\":" << layout.binary.size () << "}],\"bufferViews\":[";
	for (std::size_t i = 0; i < layout.bufferViews.size (); ++i) {
		if (i > 0)
			json << ',';
		const BufferView& view = layout.bufferViews[i];
		json << "{\"buffer\":0,\"byteOffset\":" << view.offset << ",\"byteLength\":" << view.length;
		if (view.target.has_value ())
			json << ",\"target\":" << *view.target;
		json << '}';
	}
	json << "],\"accessors\":[";
	for (std::size_t i = 0; i < layout.accessors.size (); ++i) {
		if (i > 0)
			json << ',';
		const Accessor& accessor = layout.accessors[i];
		json << "{\"bufferView\":" << accessor.bufferView << ",\"componentType\":" << accessor.componentType
		     << ",\"count\":" << accessor.count << ",\"type\":\"" << accessor.type << '\"';
		if (accessor.minimum.has_value ()) {
			json << ",\"min\":";
			WriteAccessorBound (json, *accessor.minimum);
		}
		if (accessor.maximum.has_value ()) {
			json << ",\"max\":";
			WriteAccessorBound (json, *accessor.maximum);
		}
		json << '}';
	}
	json << "]}";

	std::string jsonData = json.str ();
	while (jsonData.size () % 4 != 0)
		jsonData.push_back (' ');

	const std::size_t totalSize = 12 + 8 + jsonData.size () + 8 + layout.binary.size ();
	std::vector<char> glb;
	glb.reserve (totalSize);
	AppendU32 (glb, Constant::GlbMagic);
	AppendU32 (glb, Constant::GlbVersion);
	AppendU32 (glb, CheckedU32 (totalSize, "GLB"));
	AppendU32 (glb, CheckedU32 (jsonData.size (), "JSON chunk"));
	AppendU32 (glb, Constant::JsonChunk);
	AppendBytes (glb, jsonData.data (), jsonData.size ());
	AppendU32 (glb, CheckedU32 (layout.binary.size (), "Binary chunk"));
	AppendU32 (glb, Constant::BinaryChunk);
	AppendBytes (glb, layout.binary.data (), layout.binary.size ());
	return glb;
}

} // namespace DropView::Glb
