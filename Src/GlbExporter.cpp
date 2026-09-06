#include "APIEnvir.h"
#include "ACAPinc.h"
#include "DGFileDialog.hpp"
#include "File.hpp"
#include "FileTypeManager.hpp"
#include "GlbExporter.hpp"
#include "PolygonTriangulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

struct Vec3 { float x, y, z; };
struct Vec2 { float u, v; };
struct MaterialGroup {
	Int32 sourceIndex = 0;
	API_MaterialType material {};
	std::string name;
	bool alphaMask = false;
	std::vector<std::uint32_t> indices;
	std::vector<char> imageData;
	std::string imageMimeType;
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

	GSErrCode GetError () const { return error; }

private:
	void* previousSight = nullptr;
	GSErrCode error = APIERR_GENERAL;
};

template<class ElementType>
void AppendElementHeads (GS::Array<API_Elem_Head>& elements, ElementType* items)
{
    const GSSize count = BMGetPtrSize (reinterpret_cast<GSPtr> (items)) / sizeof (ElementType);
    for (GSSize i = 0; i < count; ++i)
        elements.Push (items[i].head);
}

const char* GetElementTypeName (const API_ElemType& type)
{
    if (type == API_WallID) return "Fal";
    if (type == API_SlabID) return "Födém";
    if (type == API_ColumnSegmentID) return "Oszlopszegmens";
    if (type == API_BeamSegmentID) return "Gerendaszegmens";
    if (type == API_RoofID) return "Tető";
    if (type == API_ShellID) return "Héjszerkezet";
    if (type == API_RiserID) return "Lépcső homloklap";
    if (type == API_TreadID) return "Lépcső járólap";
    if (type == API_StairStructureID) return "Lépcső tartószerkezet";
    if (type == API_RailingPostID || type == API_RailingInnerPostID || type == API_RailingRailID ||
        type == API_RailingHandrailID || type == API_RailingToprailID || type == API_RailingPanelID ||
        type == API_RailingBalusterID || type == API_RailingRailEndID || type == API_RailingHandrailEndID ||
        type == API_RailingToprailEndID || type == API_RailingRailConnectionID ||
        type == API_RailingHandrailConnectionID || type == API_RailingToprailConnectionID)
        return "Korlátelem";
    if (type == API_ObjectID) return "Tárgy";
    if (type == API_LampID) return "Lámpa";
    if (type == API_MorphID) return "MORPH";
    if (type == API_MeshID) return "Háló/terep";
    if (type == API_CurtainWallFrameID) return "Függönyfal keret";
    if (type == API_CurtainWallPanelID) return "Függönyfal panel";
    if (type == API_CurtainWallJunctionID) return "Függönyfal csomópont";
    if (type == API_CurtainWallAccessoryID) return "Függönyfal tartozék";
    if (type == API_WindowID) return "Ablak";
    if (type == API_DoorID) return "Ajtó";
    if (type == API_SkylightID) return "Tetősíkablak";
    return "3D elem";
}

Vec2 ApplyArchicadTextureTransform (const API_UVCoord& uv, const API_Texture& texture)
{
	// Equivalent to ModelerAPI::TextureCoordinate::ApplyMaterialParameters:
	// rotate in texture space, then convert model-space distances to image repeats.
	// Archicad 29's API_Umat 3D model component returns this value in degrees
	// (for example 90.0 for a quarter turn), despite the API_Texture field docs.
	const double rotation = texture.rotAng * M_PI / 180.0;
	const double cosine = std::cos (rotation);
	const double sine = std::sin (rotation);
	const double rotatedU = cosine * uv.u - sine * uv.v;
	const double rotatedV = sine * uv.u + cosine * uv.v;
	const double u = std::abs (texture.xSize) > 1.0e-9 ? rotatedU / texture.xSize : rotatedU;
	const double v = std::abs (texture.ySize) > 1.0e-9 ? rotatedV / texture.ySize : rotatedV;
	return { static_cast<float> (u), static_cast<float> (1.0 - v) };
}

void LoadTextureImage (const IO::Location* location, MaterialGroup& group)
{
	if (location == nullptr)
		return;
	IO::File file (*location);
	USize size = 0;
	if (file.Open (IO::File::ReadMode) != NoError || file.GetDataLength (&size) != NoError || size < 3)
		return;
	group.imageData.resize (size);
	USize bytesRead = 0;
	if (file.ReadBin (group.imageData.data (), size, &bytesRead) != NoError || bytesRead != size) {
		group.imageData.clear ();
		return;
	}
	const unsigned char* bytes = reinterpret_cast<const unsigned char*> (group.imageData.data ());
	if (size >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
		group.imageMimeType = "image/png";
	else if (bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff)
		group.imageMimeType = "image/jpeg";
	else
		group.imageData.clear ();

	// PNG color types 4 (gray + alpha) and 6 (RGBA) contain an alpha channel.
	// Archicad uses these for cutout surfaces such as foliage and chain-link mesh.
	if (group.imageMimeType == "image/png" && size > 25 && (bytes[25] == 4 || bytes[25] == 6))
		group.alphaMask = true;
}

std::string EscapeJsonString (const std::string& value)
{
	std::ostringstream escaped;
	for (const unsigned char character : value) {
		switch (character) {
			case '\"': escaped << "\\\""; break;
			case '\\': escaped << "\\\\"; break;
			case '\b': escaped << "\\b"; break;
			case '\f': escaped << "\\f"; break;
			case '\n': escaped << "\\n"; break;
			case '\r': escaped << "\\r"; break;
			case '\t': escaped << "\\t"; break;
			default:
				if (character < 0x20)
					escaped << "\\u" << std::hex << std::setw (4) << std::setfill ('0') << static_cast<int> (character) << std::dec;
				else
					escaped << static_cast<char> (character);
		}
	}
	return escaped.str ();
}

void AppendU32 (std::vector<char>& data, std::uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8)
		data.push_back (static_cast<char> ((value >> shift) & 0xff));
}

void AppendBytes (std::vector<char>& data, const void* source, std::size_t size)
{
	const char* bytes = static_cast<const char*> (source);
	data.insert (data.end (), bytes, bytes + size);
}

Vec3 ConvertPosition (const API_Tranmat& transform, const API_VertType& vertex)
{
	const double x = transform.tmx[0] * vertex.x + transform.tmx[1] * vertex.y + transform.tmx[2] * vertex.z + transform.tmx[3];
	const double y = transform.tmx[4] * vertex.x + transform.tmx[5] * vertex.y + transform.tmx[6] * vertex.z + transform.tmx[7];
	const double z = transform.tmx[8] * vertex.x + transform.tmx[9] * vertex.y + transform.tmx[10] * vertex.z + transform.tmx[11];
	return { static_cast<float> (x), static_cast<float> (z), static_cast<float> (-y) };
}

Vec3 ConvertNormal (const API_Tranmat& transform, API_VectType normal, bool reverse)
{
	const double sign = reverse ? -1.0 : 1.0;
	const double x = sign * (transform.tmx[0] * normal.x + transform.tmx[1] * normal.y + transform.tmx[2] * normal.z);
	const double y = sign * (transform.tmx[4] * normal.x + transform.tmx[5] * normal.y + transform.tmx[6] * normal.z);
	const double z = sign * (transform.tmx[8] * normal.x + transform.tmx[9] * normal.y + transform.tmx[10] * normal.z);
	const double length = std::sqrt (x * x + y * y + z * z);
	return { static_cast<float> (x / length), static_cast<float> (z / length), static_cast<float> (-y / length) };
}

bool GetSelectedExportElements (GS::Array<API_Elem_Head>& elements, Int32& wallCount, Int32& slabCount, Int32& columnCount, Int32& beamCount, Int32& roofCount, Int32& shellCount, Int32& stairCount, Int32& railingCount, Int32& objectCount, Int32& lampCount, Int32& morphCount, Int32& meshCount, Int32& curtainWallCount, Int32& windowCount, Int32& doorCount, Int32& skylightCount)
{
    wallCount = slabCount = columnCount = beamCount = roofCount = shellCount = stairCount = railingCount = objectCount = lampCount = morphCount = meshCount = curtainWallCount = windowCount = doorCount = skylightCount = 0;
    API_SelectionInfo selectionInfo {};
    GS::Array<API_Neig> selection;
    const GSErrCode error = ACAPI_Selection_Get (&selectionInfo, &selection, false);
    BMKillHandle (reinterpret_cast<GSHandle*> (&selectionInfo.marquee.coords));
    if (error != NoError) return false;
    GS::HashSet<API_Guid> seen;
    auto addElement = [&] (const API_Guid& guid) {
        if (seen.Contains (guid)) return;
        API_Element element {};
        element.header.guid = guid;
        if (ACAPI_Element_Get (&element) != NoError) return;
        if (element.header.type == API_WallID) ++wallCount;
        else if (element.header.type == API_SlabID) ++slabCount;
        else if (element.header.type == API_ColumnID) ++columnCount;
        else if (element.header.type == API_BeamID) ++beamCount;
        else if (element.header.type == API_RoofID) ++roofCount;
        else if (element.header.type == API_ShellID) ++shellCount;
        else if (element.header.type == API_StairID) ++stairCount;
        else if (element.header.type == API_RailingID) ++railingCount;
        else if (element.header.type == API_ObjectID) ++objectCount;
        else if (element.header.type == API_LampID) ++lampCount;
        else if (element.header.type == API_MorphID) ++morphCount;
        else if (element.header.type == API_MeshID) ++meshCount;
        else if (element.header.type == API_CurtainWallID) ++curtainWallCount;
        else if (element.header.type == API_WindowID) ++windowCount;
        else if (element.header.type == API_DoorID) ++doorCount;
        else if (element.header.type == API_SkylightID) ++skylightCount;
        else return;
        seen.Add (guid);
        elements.Push (element.header);
    };
    for (const auto& selected : selection) addElement (selected.guid);
    // Copy the initial selection: appending connected openings may reallocate elements.
    const auto selectedElements = elements;
    for (const auto& element : selectedElements) {
        if (element.type == API_WallID) {
            GS::Array<API_Guid> windows;
            if (ACAPI_Grouping_GetConnectedElements (element.guid, API_WindowID, &windows) != NoError) {
                ACAPI_WriteReport ("A falhoz tartozó ablakok listája nem olvasható. Az export megszakadt.", true);
                return false;
            }
            for (const auto& guid : windows) addElement (guid);
            GS::Array<API_Guid> doors;
            if (ACAPI_Grouping_GetConnectedElements (element.guid, API_DoorID, &doors) != NoError) {
                ACAPI_WriteReport ("A falhoz tartozó ajtók listája nem olvasható. Az export megszakadt.", true);
                return false;
            }
            for (const auto& guid : doors) addElement (guid);
        }
        if (element.type == API_RoofID || element.type == API_ShellID) {
            GS::Array<API_Guid> skylights;
            if (ACAPI_Grouping_GetConnectedElements (element.guid, API_SkylightID, &skylights) != NoError) {
                ACAPI_WriteReport ("A tetőhöz vagy héjszerkezethez tartozó tetősíkablakok listája nem olvasható. Az export megszakadt.", true);
                return false;
            }
            for (const auto& guid : skylights) addElement (guid);
        }
    }

    // Composite elements keep their actual 3D bodies on subelements.
    GS::Array<API_Elem_Head> modelElements;
    for (const auto& element : elements) {
        if (element.type != API_ColumnID && element.type != API_BeamID && element.type != API_StairID &&
            element.type != API_RailingID && element.type != API_CurtainWallID) {
            modelElements.Push (element);
            continue;
        }
		// Depending on the element family, a body in the 3D sight can refer either
		// to the parent or to one of its physical subelements. Keep both GUIDs.
		modelElements.Push (element);

        API_ElementMemo memo {};
        const UInt64 memoMask = element.type == API_ColumnID ? APIMemoMask_ColumnSegment :
            element.type == API_BeamID ? APIMemoMask_BeamSegment :
            element.type == API_StairID ? APIMemoMask_StairRiser | APIMemoMask_StairTread | APIMemoMask_StairStructure :
            element.type == API_CurtainWallID ? APIMemoMask_CWallFrames | APIMemoMask_CWallPanels |
                APIMemoMask_CWallJunctions | APIMemoMask_CWallAccessories :
            APIMemoMask_RailingPost | APIMemoMask_RailingInnerPost | APIMemoMask_RailingRail |
            APIMemoMask_RailingHandrail | APIMemoMask_RailingToprail | APIMemoMask_RailingPanel |
            APIMemoMask_RailingBaluster | APIMemoMask_RailingRailEnd | APIMemoMask_RailingHandrailEnd |
            APIMemoMask_RailingToprailEnd | APIMemoMask_RailingRailConnection |
            APIMemoMask_RailingHandrailConnection | APIMemoMask_RailingToprailConnection;
        const GSErrCode memoError = ACAPI_Element_GetMemo (element.guid, &memo, memoMask);
        if (memoError != NoError) {
            ACAPI_WriteReport (GS::UniString::Printf ("A kijelölt %s szegmensei nem olvashatók (hiba: %d). Az export megszakadt.",
                element.type == API_ColumnID ? "oszlop" : element.type == API_BeamID ? "gerenda" : element.type == API_StairID ? "lépcső" : element.type == API_CurtainWallID ? "függönyfal" : "korlát", memoError), true);
            ACAPI_DisposeElemMemoHdls (&memo);
            return false;
        }

        if (element.type == API_ColumnID) {
            const GSSize segmentCount = BMGetPtrSize (reinterpret_cast<GSPtr> (memo.columnSegments)) / sizeof (API_ColumnSegmentType);
            for (GSSize i = 0; i < segmentCount; ++i)
                modelElements.Push (memo.columnSegments[i].head);
        } else if (element.type == API_BeamID) {
            const GSSize segmentCount = BMGetPtrSize (reinterpret_cast<GSPtr> (memo.beamSegments)) / sizeof (API_BeamSegmentType);
            for (GSSize i = 0; i < segmentCount; ++i)
                modelElements.Push (memo.beamSegments[i].head);
        } else if (element.type == API_StairID) {
            const GSSize riserCount = BMGetPtrSize (reinterpret_cast<GSPtr> (memo.stairRisers)) / sizeof (API_StairRiserType);
            for (GSSize i = 0; i < riserCount; ++i)
                modelElements.Push (memo.stairRisers[i].head);
            const GSSize treadCount = BMGetPtrSize (reinterpret_cast<GSPtr> (memo.stairTreads)) / sizeof (API_StairTreadType);
            for (GSSize i = 0; i < treadCount; ++i)
                modelElements.Push (memo.stairTreads[i].head);
            const GSSize structureCount = BMGetPtrSize (reinterpret_cast<GSPtr> (memo.stairStructures)) / sizeof (API_StairStructureType);
            for (GSSize i = 0; i < structureCount; ++i)
                modelElements.Push (memo.stairStructures[i].head);
        } else if (element.type == API_RailingID) {
            AppendElementHeads (modelElements, memo.railingPosts);
            AppendElementHeads (modelElements, memo.railingInnerPosts);
            AppendElementHeads (modelElements, memo.railingRails);
            AppendElementHeads (modelElements, memo.railingHandrails);
            AppendElementHeads (modelElements, memo.railingToprails);
            AppendElementHeads (modelElements, memo.railingPanels);
            AppendElementHeads (modelElements, memo.railingBalusters);
            AppendElementHeads (modelElements, memo.railingRailEnds);
            AppendElementHeads (modelElements, memo.railingHandrailEnds);
            AppendElementHeads (modelElements, memo.railingToprailEnds);
            AppendElementHeads (modelElements, memo.railingRailConnections);
            AppendElementHeads (modelElements, memo.railingHandrailConnections);
            AppendElementHeads (modelElements, memo.railingToprailConnections);
        } else {
            AppendElementHeads (modelElements, memo.cWallFrames);
            const GSSize panelCount = BMGetPtrSize (reinterpret_cast<GSPtr> (memo.cWallPanels)) / sizeof (API_CWPanelType);
            for (GSSize i = 0; i < panelCount; ++i) {
                bool isDegenerate = false;
                if (ACAPI_CurtainWall_IsCWPanelDegenerate (&memo.cWallPanels[i].head.guid, &isDegenerate) == NoError && !isDegenerate)
                    modelElements.Push (memo.cWallPanels[i].head);
            }
            AppendElementHeads (modelElements, memo.cWallJunctions);
            AppendElementHeads (modelElements, memo.cWallAccessories);
        }
        ACAPI_DisposeElemMemoHdls (&memo);
    }
    elements = std::move (modelElements);
    return !elements.IsEmpty ();
}

std::vector<std::vector<Int32>> GetPolygonContours (const API_PgonType& polygon, Int32 bodyVertexCount)
{
    std::vector<std::vector<Int32>> rings (1);
    for (Int32 i = polygon.fpedg; i <= polygon.lpedg; ++i) {
        API_Component3D c {};
        c.header.typeID = API_PedgID;
        c.header.index = i;
        if (ACAPI_ModelAccess_GetComponent (&c) != NoError)
            throw std::runtime_error ("Cannot read polygon contour");
        const Int32 edge = c.pedg.pedg;
        if (edge == 0) {
            if (rings.back ().size () < 3) throw std::runtime_error ("Incomplete contour");
            rings.emplace_back ();
            continue;
        }
        c.header.typeID = API_EdgeID;
        c.header.index = std::abs (edge);
        if (ACAPI_ModelAccess_GetComponent (&c) != NoError)
            throw std::runtime_error ("Cannot read edge");
        const Int32 vertex = edge > 0 ? c.edge.vert1 : c.edge.vert2;
        if (vertex <= 0 || vertex > bodyVertexCount)
            throw std::runtime_error ("Invalid contour vertex");
        rings.back ().push_back (vertex);
    }
    if (rings.back ().empty ()) rings.pop_back ();
    return rings;
}

bool CollectMesh (const GS::Array<API_Elem_Head>& elements, std::vector<Vec3>& positions, std::vector<Vec3>& normals,
	std::vector<Vec2>& textureCoordinates, std::vector<MaterialGroup>& materialGroups, Int32& emptyElementCount,
    Int32& failedElementCount, GS::UniString& elementReport)
{
    emptyElementCount = 0;
	failedElementCount = 0;
	std::map<Int32, std::size_t> materialToGroup;
	Int32 visibleBodyCount = 0;
	if (ACAPI_ModelAccess_GetNum (API_BodyID, &visibleBodyCount) != NoError)
		return false;
	GS::HashSet<API_Guid> exportGuids;
	for (const API_Elem_Head& element : elements)
		exportGuids.Add (element.guid);
	struct VisibleBodyGroup {
		API_Elem_Head parent {};
		std::vector<Int32> bodyIndices;
	};
	std::vector<VisibleBodyGroup> visibleBodyGroups;
	for (Int32 bodyIndex = 1; bodyIndex <= visibleBodyCount; ++bodyIndex) {
		API_Component3D bodyComponent {};
		bodyComponent.header.typeID = API_BodyID;
		bodyComponent.header.index = bodyIndex;
		if (ACAPI_ModelAccess_GetComponent (&bodyComponent) != NoError || !exportGuids.Contains (bodyComponent.body.parent.guid))
			continue;
		auto group = std::find_if (visibleBodyGroups.begin (), visibleBodyGroups.end (), [&] (const VisibleBodyGroup& candidate) {
			return candidate.parent.guid == bodyComponent.body.parent.guid;
		});
		if (group == visibleBodyGroups.end ()) {
			visibleBodyGroups.push_back ({bodyComponent.body.parent, {bodyIndex}});
		} else {
			group->bodyIndices.push_back (bodyIndex);
		}
	}
	for (const VisibleBodyGroup& visibleBodyGroup : visibleBodyGroups) {
	const API_Elem_Head& element = visibleBodyGroup.parent;
    const char* typeName = GetElementTypeName (element.type);
    const GS::UniString elementGuid = APIGuid2GSGuid (element.guid).ToUniString ();
    const std::size_t positionsBefore = positions.size ();
    const std::size_t normalsBefore = normals.size ();
    const std::size_t textureCoordinatesBefore = textureCoordinates.size ();
    const std::size_t materialGroupCountBefore = materialGroups.size ();
    std::vector<std::size_t> groupIndexCountsBefore;
    groupIndexCountsBefore.reserve (materialGroupCountBefore);
    for (const auto& group : materialGroups)
        groupIndexCountsBefore.push_back (group.indices.size ());
    try {
    Int32 skippedPolygonCount = 0;
    std::string lastPolygonError;
    const auto trianglesBefore = [&] () { std::size_t n = 0; for (const auto& g : materialGroups) n += g.indices.size () / 3; return n; } ();
	for (Int32 bodyIndex : visibleBodyGroup.bodyIndices) {
		API_Component3D component {};
		component.header.typeID = API_BodyID;
		component.header.index = bodyIndex;
		if (ACAPI_ModelAccess_GetComponent (&component) != NoError)
			continue;
		const API_Tranmat transform = component.body.tranmat;
		const Int32 elementIndex = component.body.head.elemIndex - 1;
		const Int32 localBodyIndex = component.body.head.bodyIndex - 1;
		const Int32 polygonCount = component.body.nPgon;
		const Int32 bodyVertexCount = component.body.nVert;
		for (Int32 polygonIndex = 1; polygonIndex <= polygonCount; ++polygonIndex) {
			component.header.typeID = API_PgonID;
			component.header.index = polygonIndex;
			if (ACAPI_ModelAccess_GetComponent (&component) != NoError || component.pgon.fpedg > component.pgon.lpedg)
				continue;
			const API_PgonType polygon = component.pgon;
			try {
			const auto polygonContours = GetPolygonContours (polygon, bodyVertexCount);
			if (polygonContours.empty ())
				continue;
			component.header.typeID = API_VectID;
			component.header.index = std::abs (polygon.ivect);
			if (ACAPI_ModelAccess_GetComponent (&component) != NoError)
				continue;
			const Vec3 normal = ConvertNormal (transform, component.vect, polygon.ivect < 0);
			const double normalSign = polygon.ivect < 0 ? -1.0 : 1.0;
            const GlbGeometry::Point localNormal {normalSign * component.vect.x, normalSign * component.vect.y, normalSign * component.vect.z};
            GlbGeometry::Rings localRings;
            std::vector<API_VertType> vertices;
            for (const auto& ring : polygonContours) {
                localRings.emplace_back ();
                for (Int32 vertexIndex : ring) {
                    component.header.typeID = API_VertID;
                    component.header.index = vertexIndex;
                    if (ACAPI_ModelAccess_GetComponent (&component) != NoError)
                        throw std::runtime_error ("Cannot read contour vertex");
                    const auto vertex = component.vert;
                    vertices.push_back (vertex);
                    localRings.back ().push_back ({vertex.x, vertex.y, vertex.z});
                }
            }
            const auto triangles = GlbGeometry::Triangulate (localRings, localNormal);
			std::size_t groupIndex;
			const auto existingGroup = materialToGroup.find (polygon.iumat);
			if (existingGroup == materialToGroup.end ()) {
				API_Component3D materialComponent {};
				materialComponent.header.typeID = API_UmatID;
				materialComponent.header.index = polygon.iumat;
				if (ACAPI_ModelAccess_GetComponent (&materialComponent) == NoError) {
					MaterialGroup group;
					group.sourceIndex = polygon.iumat;
					group.material = materialComponent.umat.mater;
					group.name = group.material.head.name;
					if (group.name.empty ())
						group.name = "Archicad Surface " + std::to_string (polygon.iumat);
					IO::Location* textureLocation = materialComponent.umat.mater.texture.fileLoc;
					group.material.texture.fileLoc = nullptr;
					LoadTextureImage (textureLocation, group);
					delete textureLocation;
					groupIndex = materialGroups.size ();
					materialGroups.push_back (group);
					materialToGroup[polygon.iumat] = groupIndex;
				} else {
					throw std::runtime_error ("Cannot read polygon material");
				}
			} else {
				groupIndex = existingGroup->second;
			}
            const std::uint32_t base = static_cast<std::uint32_t> (positions.size ());
            for (const auto& vertex : vertices) {
                positions.push_back (ConvertPosition (transform, vertex));
                normals.push_back (normal);
                API_TexCoordPars parameters {};
                parameters.elemIdx = elementIndex;
                parameters.bodyIdx = localBodyIndex;
                parameters.pgonIndex = polygonIndex;
                parameters.surfacePoint = {vertex.x, vertex.y, vertex.z};
                API_UVCoord uv {};
                if (elementIndex >= 0 && localBodyIndex >= 0 && ACAPI_ModelAccess_GetTextureCoord (&parameters, &uv) == NoError)
                    textureCoordinates.push_back (ApplyArchicadTextureTransform (uv, materialGroups[groupIndex].material.texture));
                else textureCoordinates.push_back ({0.0f, 0.0f});
            }
            for (auto index : triangles) materialGroups[groupIndex].indices.push_back (base + index);
			} catch (const GlbGeometry::DegeneratePolygon&) {
				// GDL objects commonly contain intentional zero-area helper polygons.
				// They have no visible surface and can be omitted without data loss.
			} catch (const std::exception& error) {
				++skippedPolygonCount;
				lastPolygonError = error.what ();
			}
		}
	}
    std::size_t trianglesAfter = 0;
    for (const auto& group : materialGroups) trianglesAfter += group.indices.size () / 3;
    const auto added = trianglesAfter - trianglesBefore;
    if (added == 0) ++emptyElementCount;
	if (skippedPolygonCount > 0) {
		++failedElementCount;
		elementReport += GS::UniString::Printf ("\nHIBA – %s, GUID: %s: %d hibás poligon kimaradt (utolsó hiba: %s). Az elem többi része exportálva.",
			typeName, elementGuid.ToCStr ().Get (), skippedPolygonCount, lastPolygonError.c_str ());
	}
	} catch (const std::exception& error) {
		positions.resize (positionsBefore);
		normals.resize (normalsBefore);
		textureCoordinates.resize (textureCoordinatesBefore);
		for (std::size_t i = 0; i < materialGroupCountBefore; ++i)
			materialGroups[i].indices.resize (groupIndexCountsBefore[i]);
		while (materialGroups.size () > materialGroupCountBefore) {
			materialToGroup.erase (materialGroups.back ().sourceIndex);
			materialGroups.pop_back ();
		}
		++failedElementCount;
		elementReport += GS::UniString::Printf ("\nHIBA – %s, GUID: %s: %s. Az elem kimaradt, az export folytatódott.",
			typeName, elementGuid.ToCStr ().Get (), error.what ());
	}
	}
	return !positions.empty () && !materialGroups.empty ();
}

bool WriteGlb (const IO::Location& location, const std::vector<Vec3>& positions,
	const std::vector<Vec3>& normals, const std::vector<Vec2>& textureCoordinates, const std::vector<MaterialGroup>& materialGroups)
{
	struct PackedGeometry {
		std::vector<Vec3> positions;
		std::vector<Vec3> normals;
		std::vector<Vec2> textureCoordinates;
		std::vector<std::uint32_t> indices;
		Vec3 minimum { std::numeric_limits<float>::max (), std::numeric_limits<float>::max (), std::numeric_limits<float>::max () };
		Vec3 maximum { -minimum.x, -minimum.y, -minimum.z };
	};
	std::vector<PackedGeometry> packedGeometry (materialGroups.size ());
	for (std::size_t groupIndex = 0; groupIndex < materialGroups.size (); ++groupIndex) {
		auto& packed = packedGeometry[groupIndex];
		std::unordered_map<std::uint32_t, std::uint32_t> remappedIndices;
		for (const std::uint32_t sourceIndex : materialGroups[groupIndex].indices) {
			auto remapped = remappedIndices.find (sourceIndex);
			if (remapped == remappedIndices.end ()) {
				const std::uint32_t targetIndex = static_cast<std::uint32_t> (packed.positions.size ());
				remappedIndices[sourceIndex] = targetIndex;
				packed.positions.push_back (positions.at (sourceIndex));
				packed.normals.push_back (normals.at (sourceIndex));
				packed.textureCoordinates.push_back (textureCoordinates.at (sourceIndex));
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
	}

	std::vector<char> binary;
	std::vector<std::uint32_t> positionOffsets, normalOffsets, textureCoordinateOffsets, indexOffsets;
	for (const PackedGeometry& packed : packedGeometry) {
		positionOffsets.push_back (static_cast<std::uint32_t> (binary.size ()));
		AppendBytes (binary, packed.positions.data (), packed.positions.size () * sizeof (Vec3));
		normalOffsets.push_back (static_cast<std::uint32_t> (binary.size ()));
		AppendBytes (binary, packed.normals.data (), packed.normals.size () * sizeof (Vec3));
		textureCoordinateOffsets.push_back (static_cast<std::uint32_t> (binary.size ()));
		AppendBytes (binary, packed.textureCoordinates.data (), packed.textureCoordinates.size () * sizeof (Vec2));
		indexOffsets.push_back (static_cast<std::uint32_t> (binary.size ())); 
		AppendBytes (binary, packed.indices.data (), packed.indices.size () * sizeof (std::uint32_t));
	}
	std::vector<int> imageBufferViews (materialGroups.size (), -1);
	std::vector<int> materialTextureIndices (materialGroups.size (), -1);
	std::vector<std::uint32_t> imageOffsets (materialGroups.size (), 0);
	int textureCount = 0;
	for (std::size_t i = 0; i < materialGroups.size (); ++i) {
		if (materialGroups[i].imageData.empty ()) continue;
		while (binary.size () % 4 != 0) binary.push_back (0);
		imageBufferViews[i] = static_cast<int> (4 * materialGroups.size () + textureCount);
		materialTextureIndices[i] = textureCount++;
		imageOffsets[i] = static_cast<std::uint32_t> (binary.size ());
		AppendBytes (binary, materialGroups[i].imageData.data (), materialGroups[i].imageData.size ());
	}
	while (binary.size () % 4 != 0) binary.push_back (0);
	std::ostringstream json;
	json << std::fixed << std::setprecision (6)
		<< "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Archicad GLB Exporter v41\"},"
		<< "\"scene\":0,\"scenes\":[{\"nodes\":[";
	for (std::size_t i = 0; i < materialGroups.size (); ++i) {
		if (i > 0) json << ',';
		json << i;
	}
	json << "]}],\"nodes\":[";
	for (std::size_t i = 0; i < materialGroups.size (); ++i) {
		if (i > 0) json << ',';
		json << "{\"mesh\":" << i << ",\"name\":\"" << EscapeJsonString (materialGroups[i].name) << "\"}";
	}
	json << "],\"meshes\":[";
	for (std::size_t i = 0; i < materialGroups.size (); ++i) {
		if (i > 0) json << ',';
		json << "{\"name\":\"" << EscapeJsonString (materialGroups[i].name)
			<< "\",\"primitives\":[{\"attributes\":{\"POSITION\":" << (4 * i)
			<< ",\"NORMAL\":" << (4 * i + 1) << ",\"TEXCOORD_0\":" << (4 * i + 2)
			<< "},\"indices\":" << (4 * i + 3) << ",\"material\":" << i << "}]}";
	}
	json << "],\"materials\":[";
	for (std::size_t i = 0; i < materialGroups.size (); ++i) {
		if (i > 0) json << ',';
		const API_MaterialType& material = materialGroups[i].material;
		const double alpha = 1.0 - material.transpPc / 100.0;
		json << "{\"name\":\"" << EscapeJsonString (materialGroups[i].name) << "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":["
			<< material.surfaceRGB.f_red << ',' << material.surfaceRGB.f_green << ',' << material.surfaceRGB.f_blue << ',' << alpha
			<< "],\"metallicFactor\":0,\"roughnessFactor\":1";
		if (materialTextureIndices[i] >= 0) json << ",\"baseColorTexture\":{\"index\":" << materialTextureIndices[i] << '}';
		json << '}';
		if (alpha < 1.0)
			json << ",\"alphaMode\":\"BLEND\"";
		else if (materialGroups[i].alphaMask)
			json << ",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5";
		json << ",\"doubleSided\":true";
		json << '}';
	}
	json << ']';
	if (textureCount > 0) {
		json << ",\"textures\":[";
		int textureIndex = 0;
		for (std::size_t i = 0; i < materialGroups.size (); ++i) if (materialTextureIndices[i] >= 0) {
			if (textureIndex > 0) json << ',';
			json << "{\"source\":" << textureIndex << ",\"sampler\":" << textureIndex << '}';
			++textureIndex;
		}
		json << "],\"samplers\":[";
		textureIndex = 0;
		for (std::size_t i = 0; i < materialGroups.size (); ++i) if (materialTextureIndices[i] >= 0) {
			if (textureIndex++ > 0) json << ',';
			const short status = materialGroups[i].material.texture.status;
			const int wrapS = (status & APITxtr_MirrorX) != 0 ? 33648 : 10497;
			const int wrapT = (status & APITxtr_MirrorY) != 0 ? 33648 : 10497;
			json << "{\"wrapS\":" << wrapS << ",\"wrapT\":" << wrapT << '}';
		}
		json << "],\"images\":[";
		int imageIndex = 0;
		for (std::size_t i = 0; i < materialGroups.size (); ++i) if (imageBufferViews[i] >= 0) {
			if (imageIndex++ > 0) json << ',';
			json << "{\"bufferView\":" << imageBufferViews[i] << ",\"mimeType\":\"" << materialGroups[i].imageMimeType << "\"}";
		}
		json << ']';
	}
	json << ",\"buffers\":[{\"byteLength\":" << binary.size () << "}],\"bufferViews\":[";
	bool firstBufferView = true;
	for (std::size_t i = 0; i < packedGeometry.size (); ++i) {
		if (!firstBufferView) json << ',';
		firstBufferView = false;
		json << "{\"buffer\":0,\"byteOffset\":" << positionOffsets[i] << ",\"byteLength\":" << packedGeometry[i].positions.size () * sizeof (Vec3) << ",\"target\":34962},"
			<< "{\"buffer\":0,\"byteOffset\":" << normalOffsets[i] << ",\"byteLength\":" << packedGeometry[i].normals.size () * sizeof (Vec3) << ",\"target\":34962},"
			<< "{\"buffer\":0,\"byteOffset\":" << textureCoordinateOffsets[i] << ",\"byteLength\":" << packedGeometry[i].textureCoordinates.size () * sizeof (Vec2) << ",\"target\":34962},"
			<< "{\"buffer\":0,\"byteOffset\":" << indexOffsets[i] << ",\"byteLength\":" << packedGeometry[i].indices.size () * sizeof (std::uint32_t) << ",\"target\":34963}";
	}
	for (std::size_t i = 0; i < materialGroups.size (); ++i) if (imageBufferViews[i] >= 0) {
		if (!firstBufferView) json << ',';
		firstBufferView = false;
		json << "{\"buffer\":0,\"byteOffset\":" << imageOffsets[i] << ",\"byteLength\":" << materialGroups[i].imageData.size () << '}';
	}
	json << "],\"accessors\":[";
	for (std::size_t i = 0; i < packedGeometry.size (); ++i) {
		if (i > 0) json << ',';
		const PackedGeometry& packed = packedGeometry[i];
		json << "{\"bufferView\":" << (4 * i) << ",\"componentType\":5126,\"count\":" << packed.positions.size ()
			<< ",\"type\":\"VEC3\",\"min\":[" << packed.minimum.x << ',' << packed.minimum.y << ',' << packed.minimum.z
			<< "],\"max\":[" << packed.maximum.x << ',' << packed.maximum.y << ',' << packed.maximum.z << "]},"
			<< "{\"bufferView\":" << (4 * i + 1) << ",\"componentType\":5126,\"count\":" << packed.normals.size () << ",\"type\":\"VEC3\"},"
			<< "{\"bufferView\":" << (4 * i + 2) << ",\"componentType\":5126,\"count\":" << packed.textureCoordinates.size () << ",\"type\":\"VEC2\"},"
			<< "{\"bufferView\":" << (4 * i + 3) << ",\"componentType\":5125,\"count\":" << packed.indices.size () << ",\"type\":\"SCALAR\"}";
	}
	json << "]}";
	std::string jsonData = json.str ();
	while (jsonData.size () % 4 != 0) jsonData.push_back (' ');

	std::vector<char> glb;
	AppendU32 (glb, 0x46546C67); AppendU32 (glb, 2);
	AppendU32 (glb, static_cast<std::uint32_t> (12 + 8 + jsonData.size () + 8 + binary.size ()));
	AppendU32 (glb, static_cast<std::uint32_t> (jsonData.size ())); AppendU32 (glb, 0x4E4F534A);
	AppendBytes (glb, jsonData.data (), jsonData.size ());
	AppendU32 (glb, static_cast<std::uint32_t> (binary.size ())); AppendU32 (glb, 0x004E4942);
	AppendBytes (glb, binary.data (), binary.size ());

	IO::File file (location, IO::File::Create);
	if (file.Open (IO::File::WriteEmptyMode) != NoError)
		return false;
	return file.WriteBin (glb.data (), glb.size ()) == NoError;
}

} // namespace

void ExportSelectedElementsToGlb ()
{
	GS::Array<API_Elem_Head> elements;
	Int32 wallCount = 0;
	Int32 slabCount = 0;
    Int32 columnCount = 0;
    Int32 beamCount = 0;
    Int32 roofCount = 0;
    Int32 shellCount = 0;
    Int32 stairCount = 0;
    Int32 railingCount = 0;
    Int32 objectCount = 0;
    Int32 lampCount = 0;
    Int32 morphCount = 0;
    Int32 meshCount = 0;
    Int32 curtainWallCount = 0;
    Int32 windowCount = 0;
    Int32 doorCount = 0;
    Int32 skylightCount = 0;
	if (!GetSelectedExportElements (elements, wallCount, slabCount, columnCount, beamCount, roofCount, shellCount, stairCount, railingCount, objectCount, lampCount, morphCount, meshCount, curtainWallCount, windowCount, doorCount, skylightCount)) {
		ACAPI_WriteReport ("Jelölj ki legalább egy támogatott 3D épületelemet.", true);
		return;
	}

	// Ask for the destination before potentially expensive stair/railing mesh processing.
	DG::FileDialog dialog (DG::FileDialog::Save);
	dialog.SetTitle ("Kijelölt épületelemek exportálása GLB-be");
	FTM::FileTypeManager manager ("GLBExporterFileTypes");
	const FTM::TypeID glbType = manager.AddType (FTM::FileType ("glTF Binary", "glb", 'GLB ', 'GLB ', -1));
	dialog.AddFilter (glbType);
	if (!dialog.Invoke ())
		return;
	const IO::Location location = dialog.GetSelectedFile ();

	std::vector<Vec3> positions, normals;
	std::vector<Vec2> textureCoordinates;
	std::vector<MaterialGroup> materialGroups;
    Int32 emptyElementCount = 0;
    Int32 failedElementCount = 0;
    GS::UniString elementReport;
    try {
		Scoped3DWindowSight sight;
		if (sight.GetError () != NoError) {
			ACAPI_WriteReport (GS::UniString::Printf ("Az aktuális 3D ablak modellje nem érhető el (hiba: %d). Nem készült GLB.", sight.GetError ()), true);
			return;
		}
        if (!CollectMesh (elements, positions, normals, textureCoordinates, materialGroups, emptyElementCount, failedElementCount, elementReport)) {
            ACAPI_WriteReport ("A kijelölt elemek 3D hálója nem exportálható.", true);
            return;
        }
    } catch (const std::exception& error) {
        ACAPI_WriteReport (GS::UniString::Printf ("A geometria feldolgozása sikertelen: %s. Nem készült GLB.", error.what ()), true);
        return;
    }

	if (!WriteGlb (location, positions, normals, textureCoordinates, materialGroups)) {
		ACAPI_WriteReport ("A GLB-fájl írása sikertelen.", true);
		return;
	}
	const Int32 problemCount = emptyElementCount + failedElementCount;
	if (problemCount == 0)
		ACAPI_WriteReport ("GLB export kész.\nHibás vagy kihagyott elem: 0", true);
	else
		ACAPI_WriteReport (GS::UniString::Printf ("GLB export kész.\nHibás vagy kihagyott elemek: %d", problemCount) + elementReport, true);
}
