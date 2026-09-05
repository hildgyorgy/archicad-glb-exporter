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
#include <vector>

namespace {

struct Vec3 { float x, y, z; };
struct Vec2 { float u, v; };
struct MaterialGroup {
	Int32 sourceIndex = 0;
	API_MaterialType material {};
	std::vector<std::uint32_t> indices;
	std::vector<char> imageData;
	std::string imageMimeType;
};

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

bool GetSelectedExportElements (GS::Array<API_Elem_Head>& elements, Int32& wallCount, Int32& slabCount, Int32& columnCount, Int32& beamCount, Int32& roofCount, Int32& shellCount, Int32& windowCount, Int32& doorCount)
{
    wallCount = slabCount = columnCount = beamCount = roofCount = shellCount = windowCount = doorCount = 0;
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
        else if (element.header.type == API_WindowID) ++windowCount;
        else if (element.header.type == API_DoorID) ++doorCount;
        else return;
        seen.Add (guid);
        elements.Push (element.header);
    };
    for (const auto& selected : selection) addElement (selected.guid);
    // Copy the initial selection: appending connected openings may reallocate elements.
    const auto selectedElements = elements;
    for (const auto& element : selectedElements) {
        if (element.type != API_WallID) continue;
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

    // Columns and beams keep their actual 3D bodies on segment subelements.
    // Their parent elements affect intersections, but have no directly readable body.
    GS::Array<API_Elem_Head> modelElements;
    for (const auto& element : elements) {
        if (element.type != API_ColumnID && element.type != API_BeamID) {
            modelElements.Push (element);
            continue;
        }

        API_ElementMemo memo {};
        const UInt64 memoMask = element.type == API_ColumnID ? APIMemoMask_ColumnSegment : APIMemoMask_BeamSegment;
        const GSErrCode memoError = ACAPI_Element_GetMemo (element.guid, &memo, memoMask);
        if (memoError != NoError) {
            ACAPI_WriteReport (GS::UniString::Printf ("A kijelölt %s szegmensei nem olvashatók (hiba: %d). Az export megszakadt.",
                element.type == API_ColumnID ? "oszlop" : "gerenda", memoError), true);
            ACAPI_DisposeElemMemoHdls (&memo);
            return false;
        }

        if (element.type == API_ColumnID) {
            const GSSize segmentCount = BMGetPtrSize (reinterpret_cast<GSPtr> (memo.columnSegments)) / sizeof (API_ColumnSegmentType);
            for (GSSize i = 0; i < segmentCount; ++i)
                modelElements.Push (memo.columnSegments[i].head);
        } else {
            const GSSize segmentCount = BMGetPtrSize (reinterpret_cast<GSPtr> (memo.beamSegments)) / sizeof (API_BeamSegmentType);
            for (GSSize i = 0; i < segmentCount; ++i)
                modelElements.Push (memo.beamSegments[i].head);
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
	std::vector<Vec2>& textureCoordinates, std::vector<MaterialGroup>& materialGroups, Int32& emptyElementCount, GS::UniString& elementReport)
{
    emptyElementCount = 0;
	std::map<Int32, std::size_t> materialToGroup;
	for (const API_Elem_Head& element : elements) {
    const char* typeName = element.type == API_WallID ? "Fal" : element.type == API_SlabID ? "Födém" : element.type == API_ColumnSegmentID ? "Oszlopszegmens" : element.type == API_BeamSegmentID ? "Gerendaszegmens" : element.type == API_RoofID ? "Tető" : element.type == API_ShellID ? "Héjszerkezet" : element.type == API_WindowID ? "Ablak" : "Ajtó";
    const auto trianglesBefore = [&] () { std::size_t n = 0; for (const auto& g : materialGroups) n += g.indices.size () / 3; return n; } ();
    API_ElemInfo3D info {};
    const GSErrCode modelError = ACAPI_ModelAccess_Get3DInfo (element, &info);
    if (modelError != NoError || info.lbody < info.fbody) {
        elementReport += GS::UniString::Printf ("\n%s: nincs elérhető 3D test (hiba: %d).", typeName, modelError);
        ++emptyElementCount;
        continue;
    }
	for (Int32 bodyIndex = info.fbody; bodyIndex <= info.lbody; ++bodyIndex) {
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
			const auto polygonContours = GetPolygonContours (polygon, bodyVertexCount);
			if (polygonContours.empty ())
				continue;
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
					IO::Location* textureLocation = materialComponent.umat.mater.texture.fileLoc;
					group.material.texture.fileLoc = nullptr;
					LoadTextureImage (textureLocation, group);
					delete textureLocation;
					groupIndex = materialGroups.size ();
					materialGroups.push_back (group);
					materialToGroup[polygon.iumat] = groupIndex;
				}
				else continue;
			} else groupIndex = existingGroup->second;
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
		}
	}
    std::size_t trianglesAfter = 0;
    for (const auto& group : materialGroups) trianglesAfter += group.indices.size () / 3;
    const auto added = trianglesAfter - trianglesBefore;
    if (added == 0) ++emptyElementCount;
    elementReport += GS::UniString::Printf ("\n%s: %d háromszög (testindexek: %d–%d).", typeName, static_cast<Int32> (added), info.fbody, info.lbody);
	}
	return !positions.empty () && !materialGroups.empty ();
}

bool WriteGlb (const IO::Location& location, const std::vector<Vec3>& positions,
	const std::vector<Vec3>& normals, const std::vector<Vec2>& textureCoordinates, const std::vector<MaterialGroup>& materialGroups)
{
	std::vector<char> binary;
	const std::uint32_t positionOffset = 0;
	AppendBytes (binary, positions.data (), positions.size () * sizeof (Vec3));
	const std::uint32_t normalOffset = static_cast<std::uint32_t> (binary.size ());
	AppendBytes (binary, normals.data (), normals.size () * sizeof (Vec3));
	const std::uint32_t textureCoordinateOffset = static_cast<std::uint32_t> (binary.size ());
	AppendBytes (binary, textureCoordinates.data (), textureCoordinates.size () * sizeof (Vec2));
	std::vector<std::uint32_t> indexOffsets;
	for (const MaterialGroup& group : materialGroups) {
		indexOffsets.push_back (static_cast<std::uint32_t> (binary.size ())); 
		AppendBytes (binary, group.indices.data (), group.indices.size () * sizeof (std::uint32_t));
	}
	std::vector<int> imageBufferViews (materialGroups.size (), -1);
	std::vector<int> materialTextureIndices (materialGroups.size (), -1);
	int textureCount = 0;
	for (std::size_t i = 0; i < materialGroups.size (); ++i) {
		if (materialGroups[i].imageData.empty ()) continue;
		while (binary.size () % 4 != 0) binary.push_back (0);
		imageBufferViews[i] = static_cast<int> (3 + materialGroups.size () + textureCount);
		materialTextureIndices[i] = textureCount++;
		AppendBytes (binary, materialGroups[i].imageData.data (), materialGroups[i].imageData.size ());
	}
	while (binary.size () % 4 != 0) binary.push_back (0);

	Vec3 minimum { std::numeric_limits<float>::max (), std::numeric_limits<float>::max (), std::numeric_limits<float>::max () };
	Vec3 maximum { -minimum.x, -minimum.y, -minimum.z };
	for (const Vec3& p : positions) {
		minimum.x = std::min (minimum.x, p.x); minimum.y = std::min (minimum.y, p.y); minimum.z = std::min (minimum.z, p.z);
		maximum.x = std::max (maximum.x, p.x); maximum.y = std::max (maximum.y, p.y); maximum.z = std::max (maximum.z, p.z);
	}
	std::ostringstream json;
	json << std::fixed << std::setprecision (6)
		<< "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Archicad GLB Exporter v26\"},"
		<< "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0,\"name\":\"Archicad Elements\"}],"
		<< "\"meshes\":[{\"primitives\":[";
	for (std::size_t i = 0; i < materialGroups.size (); ++i) {
		if (i > 0) json << ',';
		json << "{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":" << (3 + i) << ",\"material\":" << i << '}';
	}
	json << "]}],\"materials\":[";
	for (std::size_t i = 0; i < materialGroups.size (); ++i) {
		if (i > 0) json << ',';
		const API_MaterialType& material = materialGroups[i].material;
		const double alpha = 1.0 - material.transpPc / 100.0;
		json << "{\"name\":\"Archicad Surface " << materialGroups[i].sourceIndex << "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":["
			<< material.surfaceRGB.f_red << ',' << material.surfaceRGB.f_green << ',' << material.surfaceRGB.f_blue << ',' << alpha
			<< "],\"metallicFactor\":0,\"roughnessFactor\":1";
		if (materialTextureIndices[i] >= 0) json << ",\"baseColorTexture\":{\"index\":" << materialTextureIndices[i] << '}';
		json << '}';
		if (alpha < 1.0) json << ",\"alphaMode\":\"BLEND\"";
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
	json << ','
		<< "\"buffers\":[{\"byteLength\":" << binary.size () << "}],"
		<< "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":" << positionOffset << ",\"byteLength\":" << positions.size () * sizeof (Vec3) << ",\"target\":34962},"
		<< "{\"buffer\":0,\"byteOffset\":" << normalOffset << ",\"byteLength\":" << normals.size () * sizeof (Vec3) << ",\"target\":34962},"
		<< "{\"buffer\":0,\"byteOffset\":" << textureCoordinateOffset << ",\"byteLength\":" << textureCoordinates.size () * sizeof (Vec2) << ",\"target\":34962}";
	for (std::size_t i = 0; i < materialGroups.size (); ++i)
		json << ", {\"buffer\":0,\"byteOffset\":" << indexOffsets[i] << ",\"byteLength\":" << materialGroups[i].indices.size () * sizeof (std::uint32_t) << ",\"target\":34963}";
	std::uint32_t imageOffset = indexOffsets.empty () ? textureCoordinateOffset : indexOffsets.back () + static_cast<std::uint32_t> (materialGroups.back ().indices.size () * sizeof (std::uint32_t));
	for (std::size_t i = 0; i < materialGroups.size (); ++i) if (imageBufferViews[i] >= 0) {
		while (imageOffset % 4 != 0) ++imageOffset;
		json << ", {\"buffer\":0,\"byteOffset\":" << imageOffset << ",\"byteLength\":" << materialGroups[i].imageData.size () << '}';
		imageOffset += static_cast<std::uint32_t> (materialGroups[i].imageData.size ());
	}
	json << "],"
		<< "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":" << positions.size () << ",\"type\":\"VEC3\",\"min\":[" << minimum.x << ',' << minimum.y << ',' << minimum.z << "],\"max\":[" << maximum.x << ',' << maximum.y << ',' << maximum.z << "]},"
		<< "{\"bufferView\":1,\"componentType\":5126,\"count\":" << normals.size () << ",\"type\":\"VEC3\"},"
		<< "{\"bufferView\":2,\"componentType\":5126,\"count\":" << textureCoordinates.size () << ",\"type\":\"VEC2\"}";
	for (std::size_t i = 0; i < materialGroups.size (); ++i)
		json << ", {\"bufferView\":" << (3 + i) << ",\"componentType\":5125,\"count\":" << materialGroups[i].indices.size () << ",\"type\":\"SCALAR\"}";
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
    Int32 windowCount = 0;
    Int32 doorCount = 0;
	if (!GetSelectedExportElements (elements, wallCount, slabCount, columnCount, beamCount, roofCount, shellCount, windowCount, doorCount)) {
		ACAPI_WriteReport ("Jelölj ki legalább egy falat, födémet, oszlopot, gerendát, tetőt, héjszerkezetet, ablakot vagy ajtót.", true);
		return;
	}
	std::vector<Vec3> positions, normals;
	std::vector<Vec2> textureCoordinates;
	std::vector<MaterialGroup> materialGroups;
    Int32 emptyElementCount = 0;
    GS::UniString elementReport;
    try {
        if (!CollectMesh (elements, positions, normals, textureCoordinates, materialGroups, emptyElementCount, elementReport)) {
            ACAPI_WriteReport ("A kijelölt elemek 3D hálója nem exportálható.", true);
            return;
        }
    } catch (const std::exception& error) {
        ACAPI_WriteReport (GS::UniString::Printf ("A geometria feldolgozása sikertelen: %s. Nem készült GLB.", error.what ()), true);
        return;
    }

	DG::FileDialog dialog (DG::FileDialog::Save);
	dialog.SetTitle ("Kijelölt épületelemek exportálása GLB-be");
	FTM::FileTypeManager manager ("GLBExporterFileTypes");
	const FTM::TypeID glbType = manager.AddType (FTM::FileType ("glTF Binary", "glb", 'GLB ', 'GLB ', -1));
	dialog.AddFilter (glbType);
	if (!dialog.Invoke ())
		return;
	const IO::Location location = dialog.GetSelectedFile ();
	if (!WriteGlb (location, positions, normals, textureCoordinates, materialGroups)) {
		ACAPI_WriteReport ("A GLB-fájl írása sikertelen.", true);
		return;
	}
	std::size_t indexCount = 0;
	for (const MaterialGroup& group : materialGroups) indexCount += group.indices.size ();
	Int32 texturedMaterialCount = 0;
	for (const MaterialGroup& group : materialGroups) if (!group.imageData.empty ()) ++texturedMaterialCount;
	ACAPI_WriteReport (GS::UniString::Printf ("GLB export kész.\nFalak: %d\nFödémek: %d\nOszlopok: %d\nGerendák: %d\nTetők: %d\nHéjszerkezetek: %d\nAblakok (kapcsolódókkal együtt): %d\nAjtók (kapcsolódókkal együtt): %d\n3D test nélküli / nem elérhető elemek: %d\nCsúcsok: %d\nHáromszögek: %d\nAnyagok: %d\nBeágyazott textúrák: %d", wallCount, slabCount, columnCount, beamCount, roofCount, shellCount, windowCount, doorCount, emptyElementCount, static_cast<Int32> (positions.size ()), static_cast<Int32> (indexCount / 3), static_cast<Int32> (materialGroups.size ()), texturedMaterialCount) + elementReport, true);
}
