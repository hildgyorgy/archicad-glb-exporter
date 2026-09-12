#include "GlbWriter.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
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
constexpr int UnsignedIntComponent = 5125;
constexpr int ArrayBuffer = 34962;
constexpr int ElementArrayBuffer = 34963;
constexpr int Repeat = 10497;
constexpr int MirroredRepeat = 33648;
} // namespace Constant

struct PackedGeometry {
	std::vector<Vec3> positions;
	std::vector<Vec3> normals;
	std::vector<Vec2> textureCoordinates;
	std::vector<std::uint32_t> indices;
	Vec3 minimum {std::numeric_limits<float>::max (), std::numeric_limits<float>::max (),
	              std::numeric_limits<float>::max ()};
	Vec3 maximum {-minimum.x, -minimum.y, -minimum.z};
};

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
	std::size_t positionAccessor;
	std::size_t normalAccessor;
	std::size_t textureCoordinateAccessor;
	std::size_t indexAccessor;
};

struct PackedPrimitive {
	std::size_t materialIndex;
	PackedGeometry geometry;
};

struct PackedGroup {
	std::string name;
	std::vector<PackedPrimitive> primitives;
};

struct EmbeddedImage {
	std::size_t materialIndex;
	std::size_t bufferView;
};

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
	if (primitive.indices.empty () || primitive.indices.size () % 3 != 0)
		throw std::invalid_argument ("Every primitive must contain complete triangles");

	PackedGeometry packed;
	std::unordered_map<std::uint32_t, std::uint32_t> remappedIndices;
	for (const std::uint32_t sourceIndex : primitive.indices) {
		if (sourceIndex >= model.positions.size ())
			throw std::out_of_range ("Primitive index exceeds the vertex arrays");
		auto remapped = remappedIndices.find (sourceIndex);
		if (remapped == remappedIndices.end ()) {
			const std::uint32_t targetIndex = CheckedU32 (packed.positions.size (), "Vertex count");
			remappedIndices[sourceIndex] = targetIndex;
			packed.positions.push_back (model.positions[sourceIndex]);
			packed.normals.push_back (model.normals[sourceIndex]);
			packed.textureCoordinates.push_back (model.textureCoordinates[sourceIndex]);
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
			packedGroup.primitives.push_back ({primitive.materialIndex, PackGeometry (model, primitive)});
		result.push_back (std::move (packedGroup));
	}
	return result;
}

} // namespace

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
			const std::size_t indexView = layout.AddTypedBufferView (packed.indices, Constant::ElementArrayBuffer);
			primitiveReferences.back ().push_back (
			    {primitive.materialIndex,
				 layout.AddAccessor (positionView, Constant::FloatComponent, packed.positions.size (), "VEC3",
				                     packed.minimum, packed.maximum),
				 layout.AddAccessor (normalView, Constant::FloatComponent, packed.normals.size (), "VEC3"),
				 layout.AddAccessor (textureCoordinateView, Constant::FloatComponent, packed.textureCoordinates.size (),
				                     "VEC2"),
				 layout.AddAccessor (indexView, Constant::UnsignedIntComponent, packed.indices.size (), "SCALAR")});
		}
	}

	std::vector<EmbeddedImage> embeddedImages;
	std::vector<int> materialImageIndices (model.materials.size (), -1);
	std::vector<int> materialTextureIndices (model.materials.size (), -1);
	int textureCount = 0;
	for (std::size_t i = 0; i < model.materials.size (); ++i) {
		if (model.materials[i].imageData.empty ())
			continue;
		auto existingImage =
		    std::find_if (embeddedImages.begin (), embeddedImages.end (), [&] (const EmbeddedImage& image) {
			    const Material& existingMaterial = model.materials[image.materialIndex];
			    return existingMaterial.imageMimeType == model.materials[i].imageMimeType &&
				       existingMaterial.imageData == model.materials[i].imageData;
		    });
		if (existingImage == embeddedImages.end ()) {
			const int imageIndex = static_cast<int> (embeddedImages.size ());
			embeddedImages.push_back ({i, layout.AddImageBufferView (model.materials[i].imageData)});
			materialImageIndices[i] = imageIndex;
		} else {
			materialImageIndices[i] = static_cast<int> (std::distance (embeddedImages.begin (), existingImage));
		}
		materialTextureIndices[i] = textureCount++;
	}
	while (layout.binary.size () % 4 != 0)
		layout.binary.push_back (0);

	std::ostringstream json;
	json << std::fixed << std::setprecision (6) << "{\"asset\":{\"version\":\"2.0\",\"generator\":\""
	     << EscapeJsonString (generator) << "\"}," << "\"scene\":0,\"scenes\":[{\"nodes\":[";
	for (std::size_t i = 0; i < model.groups.size (); ++i) {
		if (i > 0)
			json << ',';
		json << i;
	}
	json << "]}],\"nodes\":[";
	for (std::size_t i = 0; i < model.groups.size (); ++i) {
		if (i > 0)
			json << ',';
		json << "{\"mesh\":" << i << ",\"name\":\"" << EscapeJsonString (model.groups[i].name) << "\"}";
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
			     << ",\"TEXCOORD_0\":" << references.textureCoordinateAccessor
			     << "},\"indices\":" << references.indexAccessor << ",\"material\":" << references.materialIndex << '}';
		}
		json << "]}";
	}
	json << "],\"materials\":[";
	for (std::size_t i = 0; i < model.materials.size (); ++i) {
		if (i > 0)
			json << ',';
		const Material& material = model.materials[i];
		const bool hasBaseColorTexture = materialTextureIndices[i] >= 0;
		const double red = hasBaseColorTexture ? 1.0 : material.red;
		const double green = hasBaseColorTexture ? 1.0 : material.green;
		const double blue = hasBaseColorTexture ? 1.0 : material.blue;
		json << "{\"name\":\"" << EscapeJsonString (material.name)
		     << "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[" << red << ',' << green << ',' << blue << ','
		     << material.alpha << "],\"metallicFactor\":0,\"roughnessFactor\":1";
		if (hasBaseColorTexture)
			json << ",\"baseColorTexture\":{\"index\":" << materialTextureIndices[i] << '}';
		json << '}';
		if (material.alpha < 1.0)
			json << ",\"alphaMode\":\"BLEND\"";
		else if (material.alphaMask)
			json << ",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5";
		json << ",\"doubleSided\":true}";
	}
	json << ']';
	if (textureCount > 0) {
		json << ",\"textures\":[";
		int textureIndex = 0;
		for (std::size_t i = 0; i < model.materials.size (); ++i) {
			if (materialTextureIndices[i] < 0)
				continue;
			if (textureIndex > 0)
				json << ',';
			json << "{\"source\":" << materialImageIndices[i] << ",\"sampler\":" << textureIndex << '}';
			++textureIndex;
		}
		json << "],\"samplers\":[";
		textureIndex = 0;
		for (std::size_t i = 0; i < model.materials.size (); ++i) {
			if (materialTextureIndices[i] < 0)
				continue;
			if (textureIndex++ > 0)
				json << ',';
			json << "{\"wrapS\":" << (model.materials[i].texture.mirrorX ? Constant::MirroredRepeat : Constant::Repeat)
			     << ",\"wrapT\":" << (model.materials[i].texture.mirrorY ? Constant::MirroredRepeat : Constant::Repeat)
			     << '}';
		}
		json << "],\"images\":[";
		for (std::size_t i = 0; i < embeddedImages.size (); ++i) {
			if (i > 0)
				json << ',';
			const EmbeddedImage& image = embeddedImages[i];
			json << "{\"bufferView\":" << image.bufferView << ",\"mimeType\":\""
			     << EscapeJsonString (model.materials[image.materialIndex].imageMimeType) << "\"}";
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
		if (accessor.minimum.has_value ())
			json << ",\"min\":[" << accessor.minimum->x << ',' << accessor.minimum->y << ',' << accessor.minimum->z
			     << ']';
		if (accessor.maximum.has_value ())
			json << ",\"max\":[" << accessor.maximum->x << ',' << accessor.maximum->y << ',' << accessor.maximum->z
			     << ']';
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
