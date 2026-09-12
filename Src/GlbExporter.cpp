#include "APIEnvir.h"
#include "ACAPinc.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverloaded-virtual"
#endif
#include "DGModule.hpp"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "DGFileDialog.hpp"
#include "File.hpp"
#include "FileTypeManager.hpp"
#include "GlbExporter.hpp"
#include "GlbWriter.hpp"
#include "PolygonTriangulation.hpp"
#include "SurfaceNormals.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <vector>

#ifndef DROPVIEW_VERSION
#define DROPVIEW_VERSION "development"
#endif

namespace {

using DropView::Glb::Material;
using DropView::Glb::Model;
using DropView::Glb::Primitive;
using DropView::Glb::Vec2;
using DropView::Glb::Vec3;
using GlbGeometry::CornerNormals;

enum class GroupingMode { Surface, Layer, ElementType };

class ExportOptionsDialog final : public DG::ModalDialog, public DG::ButtonItemObserver {
public:
	ExportOptionsDialog ()
	    : DG::ModalDialog (DG::NativePoint (), 340, 118, GS::Guid ()),
	      prompt (GetReference (), DG::Rect (16, 16, 324, 34)),
	      groupingMode (GetReference (), DG::Rect (16, 38, 324, 60), 8, 4),
	      cancelButton (GetReference (), DG::Rect (172, 78, 244, 102)),
	      okButton (GetReference (), DG::Rect (252, 78, 324, 102))
	{
		SetTitle ("GLB export settings");
		prompt.SetText ("Group exported model by:");
		for (const char* item : {"Surface / Texture", "Layer", "Element type"}) {
			groupingMode.AppendItem ();
			groupingMode.SetItemText (groupingMode.GetItemCount (), item);
		}
		groupingMode.SelectItem (1);
		cancelButton.SetText ("Cancel");
		cancelButton.SetAsCancel ();
		okButton.SetText ("Export");
		okButton.SetAsDefault ();
		cancelButton.Attach (*this);
		okButton.Attach (*this);
		ShowItems ();
	}

	~ExportOptionsDialog ()
	{
		cancelButton.Detach (*this);
		okButton.Detach (*this);
	}

	GroupingMode GetGroupingMode () const
	{
		switch (groupingMode.GetSelectedItem ()) {
			case 2:
				return GroupingMode::Layer;
			case 3:
				return GroupingMode::ElementType;
			default:
				return GroupingMode::Surface;
		}
	}

private:
	void ButtonClicked (const DG::ButtonClickEvent& event) override
	{
		PostCloseRequest (event.GetSource () == &okButton ? Accept : Cancel);
	}

	DG::LeftText prompt;
	DG::PopUp groupingMode;
	DG::Button cancelButton;
	DG::Button okButton;
};

struct ExportStatistics {
	Int32 emptyElementCount = 0;
	Int32 failedElementCount = 0;
	Int32 invisiblePolygonCount = 0;
	GS::UniString elementReport;
};

class Scoped3DWindowSight {
public:
	Scoped3DWindowSight () : error (ACAPI_Sight_SelectSight (nullptr, &previousSight)) {}

	~Scoped3DWindowSight ()
	{
		if (error == NoError) {
			void* ignoredSight = nullptr;
			ACAPI_Sight_SelectSight (previousSight, &ignoredSight);
		}
	}

	GSErrCode GetError () const
	{
		return error;
	}

private:
	void* previousSight = nullptr;
	GSErrCode error = APIERR_GENERAL;
};

const char* GetElementTypeName (const API_ElemType& type)
{
	switch (type.typeID) {
		case API_WallID:
			return "Wall";
		case API_SlabID:
			return "Slab";
		case API_ColumnID:
		case API_ColumnSegmentID:
			return "Column";
		case API_BeamID:
		case API_BeamSegmentID:
			return "Beam";
		case API_RoofID:
			return "Roof";
		case API_ShellID:
			return "Shell";
		case API_StairID:
		case API_RiserID:
		case API_TreadID:
		case API_StairStructureID:
			return "Stair";
		case API_RailingID:
		case API_RailingPostID:
		case API_RailingInnerPostID:
		case API_RailingRailID:
		case API_RailingHandrailID:
		case API_RailingToprailID:
		case API_RailingPanelID:
		case API_RailingBalusterID:
		case API_RailingRailEndID:
		case API_RailingHandrailEndID:
		case API_RailingToprailEndID:
		case API_RailingRailConnectionID:
		case API_RailingHandrailConnectionID:
		case API_RailingToprailConnectionID:
			return "Railing";
		case API_ObjectID:
			return "Object";
		case API_LampID:
			return "Lamp";
		case API_MorphID:
			return "MORPH";
		case API_MeshID:
			return "Mesh/terrain";
		case API_CurtainWallID:
		case API_CurtainWallFrameID:
		case API_CurtainWallPanelID:
		case API_CurtainWallJunctionID:
		case API_CurtainWallAccessoryID:
			return "Curtain wall";
		case API_WindowID:
			return "Window";
		case API_DoorID:
			return "Door";
		case API_SkylightID:
			return "Skylight";
		default:
			return "3D element";
	}
}

Vec2 ApplyArchicadTextureTransform (const API_UVCoord& uv, const DropView::Glb::TextureParameters& texture)
{
	// Equivalent to ModelerAPI::TextureCoordinate::ApplyMaterialParameters:
	// rotate in texture space, then convert model-space distances to image repeats.
	// Archicad 29's API_Umat 3D model component returns this value in degrees
	// (for example 90.0 for a quarter turn), despite the API_Texture field docs.
	constexpr double DegreesToRadians = 0.01745329251994329576923690768489;
	const double rotation = texture.rotationDegrees * DegreesToRadians;
	const double cosine = std::cos (rotation);
	const double sine = std::sin (rotation);
	const double rotatedU = cosine * uv.u - sine * uv.v;
	const double rotatedV = sine * uv.u + cosine * uv.v;
	const double u = std::abs (texture.xSize) > 1.0e-9 ? rotatedU / texture.xSize : rotatedU;
	const double v = std::abs (texture.ySize) > 1.0e-9 ? rotatedV / texture.ySize : rotatedV;
	return {static_cast<float> (u), static_cast<float> (1.0 - v)};
}

bool IsTiffImage (const std::vector<char>& imageData)
{
	if (imageData.size () < 4)
		return false;

	const auto* bytes = reinterpret_cast<const unsigned char*> (imageData.data ());
	const bool littleEndianTiff =
	    bytes[0] == 'I' && bytes[1] == 'I' && ((bytes[2] == 42 && bytes[3] == 0) || (bytes[2] == 43 && bytes[3] == 0));
	const bool bigEndianTiff =
	    bytes[0] == 'M' && bytes[1] == 'M' && ((bytes[2] == 0 && bytes[3] == 42) || (bytes[2] == 0 && bytes[3] == 43));
	return littleEndianTiff || bigEndianTiff;
}

bool ConvertTiffToPng (const std::vector<char>& tiffData, std::vector<char>& pngData)
{
	GSHandle inputHandle = BMhAll (static_cast<GSSize> (tiffData.size ()));
	if (inputHandle == nullptr)
		return false;

	std::memcpy (*inputHandle, tiffData.data (), tiffData.size ());

	API_MimePicture conversion {};
	conversion.mimeIn = "image/tiff";
	conversion.inputHdl = inputHandle;
	conversion.mimeOut = "image/png";
	conversion.inContainsMime = false;
	conversion.outDepth = APIColorDepth_FromSourceImage;

	const GSErrCode error = ACAPI_Conversion_ConvertMimePicture (&conversion);
	BMhKill (&inputHandle);
	if (error != NoError || conversion.outputHdl == nullptr) {
		BMhKill (&conversion.outputHdl);
		return false;
	}

	const GSSize outputSize = BMhGetSize (conversion.outputHdl);
	if (outputSize <= 0) {
		BMhKill (&conversion.outputHdl);
		return false;
	}

	const auto* outputBegin = reinterpret_cast<const char*> (*conversion.outputHdl);
	pngData.assign (outputBegin, outputBegin + outputSize);
	BMhKill (&conversion.outputHdl);
	return true;
}

void LoadTextureImage (const IO::Location* location, Material& material)
{
	if (location == nullptr)
		return;
	IO::File file (*location);
	USize size = 0;
	if (file.Open (IO::File::ReadMode) != NoError || file.GetDataLength (&size) != NoError || size < 3)
		return;
	material.imageData.resize (size);
	USize bytesRead = 0;
	if (file.ReadBin (material.imageData.data (), size, &bytesRead) != NoError || bytesRead != size) {
		material.imageData.clear ();
		return;
	}
	const unsigned char* bytes = reinterpret_cast<const unsigned char*> (material.imageData.data ());
	if (size >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
		material.imageMimeType = "image/png";
	else if (bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff)
		material.imageMimeType = "image/jpeg";
	else if (IsTiffImage (material.imageData)) {
		std::vector<char> pngData;
		if (ConvertTiffToPng (material.imageData, pngData)) {
			material.imageData = std::move (pngData);
			material.imageMimeType = "image/png";
		} else {
			material.imageData.clear ();
		}
	} else {
		material.imageData.clear ();
	}

	// An alpha channel in the source image is not enough to make the Archicad
	// surface transparent. Some opaque textures contain luminance-like alpha
	// data. Use it as a cutout only when Archicad explicitly enables both alpha
	// use and transparency-pattern handling for the texture.
	bytes = reinterpret_cast<const unsigned char*> (material.imageData.data ());
	const bool pngHasAlpha =
	    material.imageMimeType == "image/png" && material.imageData.size () > 25 && (bytes[25] == 4 || bytes[25] == 6);
	material.alphaMask = pngHasAlpha && material.texture.useAlpha && material.texture.transparencyPattern;
}

Vec3 ConvertPosition (const API_Tranmat& transform, const API_VertType& vertex)
{
	const double x =
	    transform.tmx[0] * vertex.x + transform.tmx[1] * vertex.y + transform.tmx[2] * vertex.z + transform.tmx[3];
	const double y =
	    transform.tmx[4] * vertex.x + transform.tmx[5] * vertex.y + transform.tmx[6] * vertex.z + transform.tmx[7];
	const double z =
	    transform.tmx[8] * vertex.x + transform.tmx[9] * vertex.y + transform.tmx[10] * vertex.z + transform.tmx[11];
	return {static_cast<float> (x), static_cast<float> (z), static_cast<float> (-y)};
}

Vec3 ConvertNormal (const API_Tranmat& transform, API_VectType normal, bool reverse)
{
	const double sign = reverse ? -1.0 : 1.0;
	const double x = sign * (transform.tmx[0] * normal.x + transform.tmx[1] * normal.y + transform.tmx[2] * normal.z);
	const double y = sign * (transform.tmx[4] * normal.x + transform.tmx[5] * normal.y + transform.tmx[6] * normal.z);
	const double z = sign * (transform.tmx[8] * normal.x + transform.tmx[9] * normal.y + transform.tmx[10] * normal.z);
	const double length = std::sqrt (x * x + y * y + z * z);
	return {static_cast<float> (x / length), static_cast<float> (z / length), static_cast<float> (-y / length)};
}

std::vector<std::vector<Int32>> GetPolygonContours (const API_PgonType& polygon, Int32 bodyVertexCount)
{
	std::vector<std::vector<Int32>> rings (1);
	for (Int32 i = polygon.fpedg; i <= polygon.lpedg; ++i) {
		API_Component3D component {};
		component.header.typeID = API_PedgID;
		component.header.index = i;
		if (ACAPI_ModelAccess_GetComponent (&component) != NoError)
			throw std::runtime_error ("Cannot read polygon contour");
		const Int32 edge = component.pedg.pedg;
		if (edge == 0) {
			if (rings.back ().size () < 3)
				throw std::runtime_error ("Incomplete contour");
			rings.emplace_back ();
			continue;
		}
		component.header.typeID = API_EdgeID;
		component.header.index = std::abs (edge);
		if (ACAPI_ModelAccess_GetComponent (&component) != NoError)
			throw std::runtime_error ("Cannot read edge");
		const Int32 vertex = edge > 0 ? component.edge.vert1 : component.edge.vert2;
		if (vertex <= 0 || vertex > bodyVertexCount)
			throw std::runtime_error ("Invalid contour vertex");
		rings.back ().push_back (vertex);
	}
	if (rings.back ().empty ())
		rings.pop_back ();
	return rings;
}

CornerNormals CalculateBodyCornerNormals (Int32 bodyVertexCount, Int32 polygonCount, Int32 edgeCount)
{
	std::vector<GlbGeometry::Point> vertices (static_cast<std::size_t> (bodyVertexCount) + 1);
	API_Component3D component {};
	for (Int32 vertexIndex = 1; vertexIndex <= bodyVertexCount; ++vertexIndex) {
		component.header.typeID = API_VertID;
		component.header.index = vertexIndex;
		if (ACAPI_ModelAccess_GetComponent (&component) != NoError)
			return {};
		vertices[vertexIndex] = {component.vert.x, component.vert.y, component.vert.z};
	}

	std::vector<GlbGeometry::SmoothingFace> faces (static_cast<std::size_t> (polygonCount));
	for (Int32 polygonIndex = 1; polygonIndex <= polygonCount; ++polygonIndex) {
		component.header.typeID = API_PgonID;
		component.header.index = polygonIndex;
		if (ACAPI_ModelAccess_GetComponent (&component) != NoError || component.pgon.fpedg > component.pgon.lpedg ||
		    (component.pgon.status & APIPgon_Invis) != 0)
			continue;
		const API_PgonType polygon = component.pgon;
		try {
			const auto contours = GetPolygonContours (polygon, bodyVertexCount);
			component.header.typeID = API_VectID;
			component.header.index = std::abs (polygon.ivect);
			if (contours.empty () || ACAPI_ModelAccess_GetComponent (&component) != NoError)
				continue;
			GlbGeometry::SmoothingFace& face = faces[static_cast<std::size_t> (polygonIndex - 1)];
			face.valid = true;
			face.normal = {component.vect.x, component.vect.y, component.vect.z};
			if (polygon.ivect < 0)
				for (double& coordinate : face.normal)
					coordinate = -coordinate;
			for (const auto& contour : contours)
				face.contours.emplace_back (contour.begin (), contour.end ());
		} catch (const std::exception&) {
			// The normal calculation is optional. CollectPolygon reports or skips
			// invalid source polygons using the established export path.
		}
	}

	std::vector<GlbGeometry::SmoothingEdge> edges;
	for (Int32 edgeIndex = 1; edgeIndex <= edgeCount; ++edgeIndex) {
		component.header.typeID = API_EdgeID;
		component.header.index = edgeIndex;
		if (ACAPI_ModelAccess_GetComponent (&component) != NoError || (component.edge.status & APIEdge_Curved) == 0 ||
		    component.edge.pgon1 <= 0 || component.edge.pgon1 > polygonCount || component.edge.pgon2 <= 0 ||
		    component.edge.pgon2 > polygonCount)
			continue;
		edges.push_back ({static_cast<std::uint32_t> (component.edge.vert1),
		                  static_cast<std::uint32_t> (component.edge.vert2),
		                  static_cast<std::size_t> (component.edge.pgon1 - 1),
		                  static_cast<std::size_t> (component.edge.pgon2 - 1), true});
	}
	return GlbGeometry::CalculateCornerNormals (vertices, faces, edges);
}

using MaterialIndexMap = std::map<Int32, std::size_t>;
using GroupIndexMap = std::map<std::string, std::size_t>;

struct GroupDescriptor {
	std::string key;
	std::string name;
};

struct VisibleBodyGroup {
	API_Elem_Head parent {};
	std::vector<Int32> bodyIndices;
};

struct CollectionCheckpoint {
	explicit CollectionCheckpoint (const Model& model)
	    : positionCount (model.positions.size ()), normalCount (model.normals.size ()),
	      textureCoordinateCount (model.textureCoordinates.size ()), materialCount (model.materials.size ()),
	      groupCount (model.groups.size ())
	{
		groupPrimitiveIndexCounts.reserve (groupCount);
		for (const DropView::Glb::Group& group : model.groups) {
			groupPrimitiveIndexCounts.emplace_back ();
			for (const Primitive& primitive : group.primitives)
				groupPrimitiveIndexCounts.back ().push_back (primitive.indices.size ());
		}
	}

	void RollBack (Model& model, MaterialIndexMap& materialIndices, GroupIndexMap& groupIndices) const
	{
		model.positions.resize (positionCount);
		model.normals.resize (normalCount);
		model.textureCoordinates.resize (textureCoordinateCount);
		while (model.materials.size () > materialCount) {
			materialIndices.erase (model.materials.back ().sourceIndex);
			model.materials.pop_back ();
		}
		for (std::size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
			model.groups[groupIndex].primitives.resize (groupPrimitiveIndexCounts[groupIndex].size ());
			for (std::size_t primitiveIndex = 0; primitiveIndex < groupPrimitiveIndexCounts[groupIndex].size ();
			     ++primitiveIndex)
				model.groups[groupIndex].primitives[primitiveIndex].indices.resize (
				    groupPrimitiveIndexCounts[groupIndex][primitiveIndex]);
		}
		while (model.groups.size () > groupCount) {
			groupIndices.erase (model.groups.back ().key);
			model.groups.pop_back ();
		}
	}

	std::size_t positionCount;
	std::size_t normalCount;
	std::size_t textureCoordinateCount;
	std::size_t materialCount;
	std::size_t groupCount;
	std::vector<std::vector<std::size_t>> groupPrimitiveIndexCounts;
};

std::vector<VisibleBodyGroup> GetVisibleBodyGroups (Int32 visibleBodyCount)
{
	std::vector<VisibleBodyGroup> groups;
	for (Int32 bodyIndex = 1; bodyIndex <= visibleBodyCount; ++bodyIndex) {
		API_Component3D bodyComponent {};
		bodyComponent.header.typeID = API_BodyID;
		bodyComponent.header.index = bodyIndex;
		if (ACAPI_ModelAccess_GetComponent (&bodyComponent) != NoError)
			continue;
		auto group = std::find_if (groups.begin (), groups.end (), [&] (const VisibleBodyGroup& candidate) {
			return candidate.parent.guid == bodyComponent.body.parent.guid;
		});
		if (group == groups.end ())
			groups.push_back ({bodyComponent.body.parent, {bodyIndex}});
		else
			group->bodyIndices.push_back (bodyIndex);
	}
	return groups;
}

Material ReadMaterial (Int32 sourceIndex)
{
	API_Component3D materialComponent {};
	materialComponent.header.typeID = API_UmatID;
	materialComponent.header.index = sourceIndex;
	if (ACAPI_ModelAccess_GetComponent (&materialComponent) != NoError)
		throw std::runtime_error ("Cannot read polygon material");

	const API_MaterialType& sourceMaterial = materialComponent.umat.mater;
	Material material;
	material.sourceIndex = sourceIndex;
	material.name = sourceMaterial.head.name;
	if (material.name.empty ())
		material.name = "Archicad Surface " + std::to_string (sourceIndex);
	material.red = sourceMaterial.surfaceRGB.f_red;
	material.green = sourceMaterial.surfaceRGB.f_green;
	material.blue = sourceMaterial.surfaceRGB.f_blue;
	material.alpha = 1.0 - sourceMaterial.transpPc / 100.0;
	material.texture.xSize = sourceMaterial.texture.xSize;
	material.texture.ySize = sourceMaterial.texture.ySize;
	material.texture.rotationDegrees = sourceMaterial.texture.rotAng;
	material.texture.mirrorX = (sourceMaterial.texture.status & APITxtr_MirrorX) != 0;
	material.texture.mirrorY = (sourceMaterial.texture.status & APITxtr_MirrorY) != 0;
	material.texture.useAlpha = (sourceMaterial.texture.status & APITxtr_UseAlpha) != 0;
	material.texture.transparencyPattern = (sourceMaterial.texture.status & APITxtr_TransPattern) != 0;
	std::unique_ptr<IO::Location> textureLocation (sourceMaterial.texture.fileLoc);
	LoadTextureImage (textureLocation.get (), material);
	return material;
}

std::size_t GetOrCreateMaterial (Int32 sourceIndex, Model& model, MaterialIndexMap& materialIndices)
{
	const auto existingMaterial = materialIndices.find (sourceIndex);
	if (existingMaterial != materialIndices.end ())
		return existingMaterial->second;

	const std::size_t materialIndex = model.materials.size ();
	model.materials.push_back (ReadMaterial (sourceIndex));
	materialIndices[sourceIndex] = materialIndex;
	return materialIndex;
}

std::string GetLayerName (API_AttributeIndex layerIndex)
{
	API_Attribute layer {};
	layer.header.typeID = API_LayerID;
	layer.header.index = layerIndex;
	if (ACAPI_Attribute_Get (&layer) == NoError && layer.header.name[0] != '\0')
		return layer.header.name;
	return "Layer " + std::to_string (layerIndex.ToInt32_Deprecated ());
}

GroupDescriptor GetElementGroup (GroupingMode groupingMode, const API_Elem_Head& element)
{
	if (groupingMode == GroupingMode::Layer) {
		const std::string layerIndex = std::to_string (element.layer.ToInt32_Deprecated ());
		return {"layer:" + layerIndex, GetLayerName (element.layer)};
	}
	if (groupingMode == GroupingMode::ElementType) {
		const std::string typeName = GetElementTypeName (element.type);
		return {"element-type:" + typeName, typeName};
	}
	return {};
}

std::size_t GetOrCreateGroup (const GroupDescriptor& descriptor, Model& model, GroupIndexMap& groupIndices)
{
	const auto existingGroup = groupIndices.find (descriptor.key);
	if (existingGroup != groupIndices.end ())
		return existingGroup->second;

	const std::size_t groupIndex = model.groups.size ();
	model.groups.push_back ({descriptor.key, descriptor.name, {}});
	groupIndices[descriptor.key] = groupIndex;
	return groupIndex;
}

Primitive& GetOrCreatePrimitive (DropView::Glb::Group& group, std::size_t materialIndex)
{
	auto primitive =
	    std::find_if (group.primitives.begin (), group.primitives.end (), [materialIndex] (const Primitive& candidate) {
		    return candidate.materialIndex == materialIndex;
	    });
	if (primitive == group.primitives.end ()) {
		group.primitives.push_back ({materialIndex, {}});
		return group.primitives.back ();
	}
	return *primitive;
}

void CollectPolygon (const API_PgonType& polygon, Int32 polygonIndex, Int32 bodyVertexCount,
                     const API_Tranmat& transform, Int32 elementIndex, Int32 localBodyIndex, Model& model,
                     MaterialIndexMap& materialIndices, GroupIndexMap& groupIndices, GroupingMode groupingMode,
                     const GroupDescriptor& elementGroup, const CornerNormals& cornerNormals)
{
	const auto polygonContours = GetPolygonContours (polygon, bodyVertexCount);
	if (polygonContours.empty ())
		return;

	API_Component3D component {};
	component.header.typeID = API_VectID;
	component.header.index = std::abs (polygon.ivect);
	if (ACAPI_ModelAccess_GetComponent (&component) != NoError)
		return;

	const Vec3 normal = ConvertNormal (transform, component.vect, polygon.ivect < 0);
	const double normalSign = polygon.ivect < 0 ? -1.0 : 1.0;
	const GlbGeometry::Point localNormal {normalSign * component.vect.x, normalSign * component.vect.y,
	                                      normalSign * component.vect.z};
	GlbGeometry::Rings localRings;
	std::vector<API_VertType> vertices;
	for (const auto& ring : polygonContours) {
		localRings.emplace_back ();
		for (Int32 vertexIndex : ring) {
			component.header.typeID = API_VertID;
			component.header.index = vertexIndex;
			if (ACAPI_ModelAccess_GetComponent (&component) != NoError)
				throw std::runtime_error ("Cannot read contour vertex");
			const API_VertType vertex = component.vert;
			vertices.push_back (vertex);
			localRings.back ().push_back ({vertex.x, vertex.y, vertex.z});
		}
	}
	const auto triangles = GlbGeometry::Triangulate (localRings, localNormal);
	const std::size_t materialIndex = GetOrCreateMaterial (polygon.iumat, model, materialIndices);
	const Material& material = model.materials[materialIndex];
	const GroupDescriptor groupDescriptor =
	    groupingMode == GroupingMode::Surface
	        ? GroupDescriptor {"surface:" + std::to_string (material.sourceIndex), material.name}
	        : elementGroup;
	DropView::Glb::Group& group = model.groups[GetOrCreateGroup (groupDescriptor, model, groupIndices)];
	Primitive& primitive = GetOrCreatePrimitive (group, materialIndex);
	if (model.positions.size () > std::numeric_limits<std::uint32_t>::max ())
		throw std::length_error ("Vertex count exceeds the GLB 32-bit index limit");
	const std::uint32_t base = static_cast<std::uint32_t> (model.positions.size ());
	std::size_t flattenedVertexIndex = 0;
	for (const auto& ring : polygonContours) {
		for (Int32 vertexIndex : ring) {
			const API_VertType& vertex = vertices[flattenedVertexIndex++];
			model.positions.push_back (ConvertPosition (transform, vertex));
			Vec3 cornerNormal = normal;
			if (polygonIndex > 0 && static_cast<std::size_t> (polygonIndex) <= cornerNormals.size ()) {
				const auto found = cornerNormals[static_cast<std::size_t> (polygonIndex - 1)].find (
				    static_cast<std::uint32_t> (vertexIndex));
				if (found != cornerNormals[static_cast<std::size_t> (polygonIndex - 1)].end ()) {
					API_VectType localCornerNormal {};
					localCornerNormal.x = found->second[0];
					localCornerNormal.y = found->second[1];
					localCornerNormal.z = found->second[2];
					cornerNormal = ConvertNormal (transform, localCornerNormal, false);
				}
			}
			model.normals.push_back (cornerNormal);
			API_TexCoordPars parameters {};
			parameters.elemIdx = elementIndex;
			parameters.bodyIdx = localBodyIndex;
			parameters.pgonIndex = polygonIndex;
			parameters.surfacePoint = {vertex.x, vertex.y, vertex.z};
			API_UVCoord uv {};
			if (elementIndex >= 0 && localBodyIndex >= 0 &&
			    ACAPI_ModelAccess_GetTextureCoord (&parameters, &uv) == NoError)
				model.textureCoordinates.push_back (
				    ApplyArchicadTextureTransform (uv, model.materials[materialIndex].texture));
			else
				model.textureCoordinates.push_back ({0.0f, 0.0f});
		}
	}
	for (std::uint32_t index : triangles)
		primitive.indices.push_back (base + index);
}

void CollectBody (Int32 bodyIndex, Model& model, MaterialIndexMap& materialIndices, GroupIndexMap& groupIndices,
                  GroupingMode groupingMode, const GroupDescriptor& elementGroup, ExportStatistics& statistics,
                  Int32& skippedPolygonCount, std::string& lastPolygonError)
{
	API_Component3D component {};
	component.header.typeID = API_BodyID;
	component.header.index = bodyIndex;
	if (ACAPI_ModelAccess_GetComponent (&component) != NoError)
		return;

	const API_Tranmat transform = component.body.tranmat;
	const Int32 elementIndex = component.body.head.elemIndex - 1;
	const Int32 localBodyIndex = component.body.head.bodyIndex - 1;
	const Int32 polygonCount = component.body.nPgon;
	const Int32 bodyVertexCount = component.body.nVert;
	CornerNormals cornerNormals;
	if ((component.body.status & APIBody_Curved) != 0)
		cornerNormals = CalculateBodyCornerNormals (bodyVertexCount, polygonCount, component.body.nEdge);
	for (Int32 polygonIndex = 1; polygonIndex <= polygonCount; ++polygonIndex) {
		component.header.typeID = API_PgonID;
		component.header.index = polygonIndex;
		if (ACAPI_ModelAccess_GetComponent (&component) != NoError || component.pgon.fpedg > component.pgon.lpedg)
			continue;
		const API_PgonType polygon = component.pgon;
		if ((polygon.status & APIPgon_Invis) != 0) {
			++statistics.invisiblePolygonCount;
			continue;
		}
		try {
			CollectPolygon (polygon, polygonIndex, bodyVertexCount, transform, elementIndex, localBodyIndex, model,
			                materialIndices, groupIndices, groupingMode, elementGroup, cornerNormals);
		} catch (const GlbGeometry::DegeneratePolygon&) {
			// GDL objects commonly contain intentional zero-area helper polygons.
			// They have no visible surface and can be omitted without data loss.
		} catch (const std::exception& error) {
			++skippedPolygonCount;
			lastPolygonError = error.what ();
		}
	}
}

std::size_t CountTriangles (const Model& model)
{
	std::size_t count = 0;
	for (const DropView::Glb::Group& group : model.groups)
		for (const Primitive& primitive : group.primitives)
			count += primitive.indices.size () / 3;
	return count;
}

void CollectElement (const VisibleBodyGroup& visibleBodyGroup, Model& model, MaterialIndexMap& materialIndices,
                     GroupIndexMap& groupIndices, GroupingMode groupingMode, ExportStatistics& statistics)
{
	const API_Elem_Head& element = visibleBodyGroup.parent;
	const char* typeName = GetElementTypeName (element.type);
	const GS::UniString elementGuid = APIGuid2GSGuid (element.guid).ToUniString ();
	const GroupDescriptor elementGroup = GetElementGroup (groupingMode, element);
	const CollectionCheckpoint checkpoint (model);
	try {
		Int32 skippedPolygonCount = 0;
		std::string lastPolygonError;
		const std::size_t trianglesBefore = CountTriangles (model);
		for (Int32 bodyIndex : visibleBodyGroup.bodyIndices)
			CollectBody (bodyIndex, model, materialIndices, groupIndices, groupingMode, elementGroup, statistics,
			             skippedPolygonCount, lastPolygonError);
		if (CountTriangles (model) == trianglesBefore)
			++statistics.emptyElementCount;
		if (skippedPolygonCount > 0) {
			++statistics.failedElementCount;
			statistics.elementReport += GS::UniString::Printf (
			    "\nERROR – %s, GUID: %s: %d invalid polygons skipped (last error: %s). The rest of the element was "
			    "exported.",
			    typeName, elementGuid.ToCStr ().Get (), skippedPolygonCount, lastPolygonError.c_str ());
		}
	} catch (const std::exception& error) {
		checkpoint.RollBack (model, materialIndices, groupIndices);
		++statistics.failedElementCount;
		statistics.elementReport +=
		    GS::UniString::Printf ("\nERROR – %s, GUID: %s: %s. The element was skipped and export continued.",
			                       typeName, elementGuid.ToCStr ().Get (), error.what ());
	}
}

bool CollectMesh (Model& model, GroupingMode groupingMode, ExportStatistics& statistics)
{
	Int32 visibleBodyCount = 0;
	if (ACAPI_ModelAccess_GetNum (API_BodyID, &visibleBodyCount) != NoError)
		return false;

	MaterialIndexMap materialIndices;
	GroupIndexMap groupIndices;
	const std::vector<VisibleBodyGroup> visibleBodyGroups = GetVisibleBodyGroups (visibleBodyCount);
	for (const VisibleBodyGroup& group : visibleBodyGroups)
		CollectElement (group, model, materialIndices, groupIndices, groupingMode, statistics);
	return !model.positions.empty () && !model.materials.empty () && !model.groups.empty ();
}

bool WriteGlb (const IO::Location& location, const Model& model)
{
	std::vector<char> glb;
	try {
		glb = DropView::Glb::BuildBinary (model, "Drop & View GLB Exporter v" DROPVIEW_VERSION);
	} catch (const std::exception&) {
		return false;
	}

	IO::File file (location, IO::File::Create);
	if (file.Open (IO::File::WriteEmptyMode) != NoError)
		return false;
	if (glb.size () > std::numeric_limits<USize>::max ())
		return false;
	return file.WriteBin (glb.data (), static_cast<USize> (glb.size ())) == NoError;
}

} // namespace

void ExportActive3DWindowToGlb ()
{
	ExportOptionsDialog optionsDialog;
	if (!optionsDialog.Invoke ())
		return;
	const GroupingMode groupingMode = optionsDialog.GetGroupingMode ();

	// Ask for the destination before potentially expensive stair/railing mesh processing.
	DG::FileDialog dialog (DG::FileDialog::Save);
	dialog.SetTitle ("Export active 3D window to GLB");
	FTM::FileTypeManager manager ("DropViewGLBExporterFileTypes");
	const FTM::TypeID glbType = manager.AddType (FTM::FileType ("glTF Binary", "glb", 'GLB ', 'GLB ', -1));
	dialog.AddFilter (glbType);
	if (!dialog.Invoke ())
		return;
	const IO::Location location = dialog.GetSelectedFile ();

	Model model;
	ExportStatistics statistics;
	try {
		Scoped3DWindowSight sight;
		if (sight.GetError () != NoError) {
			ACAPI_WriteReport (
			    GS::UniString::Printf ("The active 3D window model is unavailable (error: %d). No GLB was created.",
				                       sight.GetError ()),
			    true);
			return;
		}
		if (!CollectMesh (model, groupingMode, statistics)) {
			ACAPI_WriteReport ("The active 3D window does not contain exportable geometry.", true);
			return;
		}
	} catch (const std::exception& error) {
		ACAPI_WriteReport (GS::UniString::Printf ("Geometry processing failed: %s. No GLB was created.", error.what ()),
		                   true);
		return;
	}

	if (!WriteGlb (location, model)) {
		ACAPI_WriteReport ("Writing the GLB file failed.", true);
		return;
	}
	const Int32 problemCount = statistics.emptyElementCount + statistics.failedElementCount;
	if (problemCount == 0)
		ACAPI_WriteReport (
		    GS::UniString::Printf (
		        "GLB export complete.\nFailed or skipped elements: 0\nInvisible Archicad polygons omitted: %d",
		        statistics.invisiblePolygonCount),
		    true);
	else
		ACAPI_WriteReport (
		    GS::UniString::Printf (
		        "GLB export complete.\nFailed or skipped elements: %d\nInvisible Archicad polygons omitted: %d",
		        problemCount, statistics.invisiblePolygonCount) +
		        statistics.elementReport,
		    true);
}
