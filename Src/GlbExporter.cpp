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
#include "MaterialConversion.hpp"
#include "PolygonTriangulation.hpp"
#include "SurfaceNormals.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <vector>

#ifndef DROPVIEW_VERSION
#define DROPVIEW_VERSION "development"
#endif

namespace {

using DropView::Glb::CameraProjection;
using DropView::Glb::InitialView;
using DropView::Glb::Material;
using DropView::Glb::Model;
using DropView::Glb::Primitive;
using DropView::Glb::Vec2;
using DropView::Glb::Vec3;
using GlbGeometry::CornerNormals;

enum class GroupingMode { Surface, Layer, ElementType };

struct ClearGlassCandidate {
	Int32 sourceIndex = 0;
	std::string name;
	std::string details;
	bool selected = false;
};

constexpr Int32 ExportPreferencesVersion = 1;

std::string LoadDesignCreditsPreference ()
{
	Int32 version = 0;
	GSSize size = 0;
	if (ACAPI_GetPreferences (&version, &size, nullptr) != NoError || version != ExportPreferencesVersion ||
	    size <= 0 || size > 4096)
		return {};
	std::vector<char> bytes (static_cast<std::size_t> (size));
	if (ACAPI_GetPreferences (&version, &size, bytes.data ()) != NoError || bytes.empty ())
		return {};
	const auto terminator = std::find (bytes.begin (), bytes.end (), '\0');
	return std::string (bytes.begin (), terminator);
}

void SaveDesignCreditsPreference (const std::string& designCredits)
{
	std::vector<char> bytes (designCredits.begin (), designCredits.end ());
	bytes.push_back ('\0');
	ACAPI_SetPreferences (ExportPreferencesVersion, static_cast<GSSize> (bytes.size ()), bytes.data ());
}

class ExportOptionsDialog final : public DG::ModalDialog, public DG::ButtonItemObserver, public DG::ListBoxObserver {
public:
	explicit ExportOptionsDialog (std::vector<ClearGlassCandidate> candidates, const std::string& savedDesignCredits)
	    : DG::ModalDialog (DG::NativePoint (), 760, 384, GS::Guid ()),
	      groupingPrompt (GetReference (), DG::Rect (16, 16, 744, 34)),
	      groupingMode (GetReference (), DG::Rect (16, 38, 744, 60), 8, 4),
	      creditsPrompt (GetReference (), DG::Rect (16, 76, 744, 94)),
	      creditsEdit (GetReference (), DG::Rect (16, 96, 744, 120), 512),
	      glassHeading (GetReference (), DG::Rect (16, 138, 744, 158)),
	      glassDescription (GetReference (), DG::Rect (16, 160, 744, 198)),
	      glassList (GetReference (), DG::Rect (16, 202, 744, 332), DG::ListBox::VScroll, DG::ListBox::PartialItems,
	                 DG::ListBox::NoHeader, 0, DG::ListBox::Frame),
	      cancelButton (GetReference (), DG::Rect (592, 344, 664, 368)),
	      okButton (GetReference (), DG::Rect (672, 344, 744, 368)), clearGlassCandidates (std::move (candidates))
	{
		SetTitle ("GLB export settings");
		groupingPrompt.SetText ("Group exported model by:");
		for (const char* item : {"Surface / Texture", "Layer", "Element type"}) {
			groupingMode.AppendItem ();
			groupingMode.SetItemText (groupingMode.GetItemCount (), item);
		}
		groupingMode.SelectItem (1);
		creditsPrompt.SetText ("Design credits (optional; stored in the GLB):");
		creditsEdit.SetText (GS::UniString (savedDesignCredits.c_str (), CC_UTF8));
		glassHeading.SetText ("Clear glass surfaces");
		glassDescription.SetText (
		    "Select the surfaces to export as clear, physically transparent glass. Other transparent surfaces "
		    "will retain their original Archicad appearance. Click a row to check or uncheck it.");
		glassList.SetTabFieldCount (2);
		glassList.SetTabFieldProperties (1, 0, 42, DG::ListBox::Center, DG::ListBox::NoTruncate);
		glassList.SetTabFieldProperties (2, 44, 714, DG::ListBox::Left, DG::ListBox::EndTruncate);
		glassList.SetItemHeight (22);
		for (std::size_t index = 0; index < clearGlassCandidates.size (); ++index) {
			glassList.AppendItem ();
			const short item = static_cast<short> (index + 1);
			glassList.SetTabItemText (item, 2, clearGlassCandidates[index].details.c_str ());
			UpdateGlassCheckMarker (item);
		}
		if (clearGlassCandidates.empty ()) {
			glassList.AppendItem ();
			glassList.SetTabItemText (
			    1, 2, "No non-cutout surfaces with at least 50% transparency were found in the active 3D view.");
			glassList.DisableItem (1);
		}
		cancelButton.SetText ("Cancel");
		cancelButton.SetAsCancel ();
		okButton.SetText ("Export");
		okButton.SetAsDefault ();
		cancelButton.Attach (*this);
		okButton.Attach (*this);
		glassList.Attach (*this);
		ShowItems ();
	}

	std::string GetDesignCredits () const
	{
		const GS::UniString value = creditsEdit.GetText ();
		return value.ToCStr (CC_UTF8).Get ();
	}

	~ExportOptionsDialog ()
	{
		cancelButton.Detach (*this);
		okButton.Detach (*this);
		glassList.Detach (*this);
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

	std::set<Int32> GetClearGlassSurfaceIndices () const
	{
		std::set<Int32> indices;
		for (const ClearGlassCandidate& candidate : clearGlassCandidates)
			if (candidate.selected)
				indices.insert (candidate.sourceIndex);
		return indices;
	}

private:
	void ButtonClicked (const DG::ButtonClickEvent& event) override
	{
		PostCloseRequest (event.GetSource () == &okButton ? Accept : Cancel);
	}

	void ListBoxSelectionChanged (const DG::ListBoxSelectionEvent& event) override
	{
		if (event.GetSource () != &glassList)
			return;
		const short item = glassList.GetSelectedItem ();
		if (item <= 0 || static_cast<std::size_t> (item) > clearGlassCandidates.size ())
			return;
		ClearGlassCandidate& candidate = clearGlassCandidates[static_cast<std::size_t> (item - 1)];
		candidate.selected = !candidate.selected;
		UpdateGlassCheckMarker (item);
		glassList.DeselectItem (item);
	}

	void UpdateGlassCheckMarker (short item)
	{
		const bool selected = clearGlassCandidates[static_cast<std::size_t> (item - 1)].selected;
		glassList.SetTabItemText (item, 1, selected ? "[x]" : "[ ]");
	}

	DG::LeftText groupingPrompt;
	DG::PopUp groupingMode;
	DG::LeftText creditsPrompt;
	DG::TextEdit creditsEdit;
	DG::LeftText glassHeading;
	DG::LeftText glassDescription;
	DG::SingleSelListBox glassList;
	DG::Button cancelButton;
	DG::Button okButton;
	std::vector<ClearGlassCandidate> clearGlassCandidates;
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

bool PngHasAlphaChannel (const std::vector<char>& imageData)
{
	// The PNG IHDR color type is byte 25. Types 4 and 6 contain alpha.
	return imageData.size () > 25 && static_cast<unsigned char> (imageData[0]) == 0x89 && imageData[1] == 'P' &&
	       imageData[2] == 'N' && imageData[3] == 'G' &&
	       (static_cast<unsigned char> (imageData[25]) == 4 || static_cast<unsigned char> (imageData[25]) == 6);
}

bool TextureFileHasPngAlpha (const IO::Location* location)
{
	if (location == nullptr)
		return false;
	IO::File file (*location);
	USize size = 0;
	constexpr USize PngHeaderSize = 26;
	if (file.Open (IO::File::ReadMode) != NoError || file.GetDataLength (&size) != NoError || size < PngHeaderSize)
		return false;
	std::vector<char> header (PngHeaderSize);
	USize bytesRead = 0;
	return file.ReadBin (header.data (), PngHeaderSize, &bytesRead) == NoError && bytesRead == PngHeaderSize &&
	       PngHasAlphaChannel (header);
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

	// An alpha channel alone is not enough: some opaque textures contain
	// luminance-like alpha data. Respect Archicad's explicit cutout flags, and
	// also preserve RGBA coverage on transparent library surfaces whose 3D
	// material omits those flags (for example chain-link fencing).
	const bool pngHasAlpha = material.imageMimeType == "image/png" && PngHasAlphaChannel (material.imageData);
	material.alphaMask = DropView::MaterialConversion::UsesAlphaCutout (pngHasAlpha, material.texture.useAlpha,
	                                                                    material.texture.transparencyPattern,
	                                                                    material.sourceTransparencyPercent);
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

Vec3 ConvertArchicadPoint (double x, double y, double z)
{
	return {static_cast<float> (x), static_cast<float> (z), static_cast<float> (-y)};
}

Vec3 Add (const Vec3& left, const Vec3& right)
{
	return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 Subtract (const Vec3& left, const Vec3& right)
{
	return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 Scale (const Vec3& value, double scale)
{
	return {static_cast<float> (value.x * scale), static_cast<float> (value.y * scale),
	        static_cast<float> (value.z * scale)};
}

double Dot (const Vec3& left, const Vec3& right)
{
	return static_cast<double> (left.x) * right.x + static_cast<double> (left.y) * right.y +
	       static_cast<double> (left.z) * right.z;
}

Vec3 Cross (const Vec3& left, const Vec3& right)
{
	return {left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z,
	        left.x * right.y - left.y * right.x};
}

double Length (const Vec3& value)
{
	return std::sqrt (Dot (value, value));
}

Vec3 Normalize (const Vec3& value)
{
	const double length = Length (value);
	if (!std::isfinite (length) || length <= 1.0e-9)
		throw std::runtime_error ("The active 3D view has an invalid camera direction");
	return Scale (value, 1.0 / length);
}

Vec3 CameraUpVector (const Vec3& position, const Vec3& target, double rollAngle)
{
	const Vec3 forward = Normalize (Subtract (target, position));
	Vec3 referenceUp {0.0f, 1.0f, 0.0f};
	if (std::abs (Dot (forward, referenceUp)) > 0.999)
		referenceUp = {0.0f, 0.0f, 1.0f};
	const Vec3 right = Normalize (Cross (forward, referenceUp));
	const Vec3 upright = Normalize (Cross (right, forward));
	return Normalize (Add (Scale (upright, std::cos (rollAngle)), Scale (right, std::sin (rollAngle))));
}

void SetArchicadWindowMetadata (const API_3DWindowInfo& window, InitialView& view)
{
	view.archicadWindowWidth = window.hSize;
	view.archicadWindowHeight = window.vSize;
	view.archicadZoomScaleX = window.zoomScaleX;
	view.archicadZoomScaleY = window.zoomScaleY;
	view.archicadZoomDisplacementX = window.zoomDispX;
	view.archicadZoomDisplacementY = window.zoomDispY;
}

InitialView CollectInitialView (const Model& model)
{
	API_3DProjectionInfo projection {};
	if (ACAPI_View_Get3DProjectionSets (&projection) != NoError)
		throw std::runtime_error ("Cannot read the active 3D projection");
	API_3DWindowInfo window {};
	if (ACAPI_View_Get3DWindowSets (&window) != NoError)
		throw std::runtime_error ("Cannot read the active 3D window settings");

	InitialView view;
	SetArchicadWindowMetadata (window, view);
	if (projection.isPersp) {
		const API_PerspPars& perspective = projection.u.persp;
		view.projection = CameraProjection::Perspective;
		view.position = ConvertArchicadPoint (perspective.pos.x, perspective.pos.y, perspective.cameraZ);
		view.target = ConvertArchicadPoint (perspective.target.x, perspective.target.y, perspective.targetZ);
		view.up = CameraUpVector (view.position, view.target, perspective.rollAngle);
		view.archicadViewConeRadians = perspective.viewCone;
		view.archicadRollAngleRadians = perspective.rollAngle;
		view.archicadTwoPointPerspective = perspective.isTwoPointPersp;
		const double aspect = window.hSize > 0 && window.vSize > 0
		                          ? static_cast<double> (window.hSize) / static_cast<double> (window.vSize)
		                          : 1.0;
		view.verticalFieldOfViewRadians =
		    2.0 * std::atan (std::tan (perspective.viewCone * 0.5) / std::max (aspect, 1.0e-6));
		view.nearPlane = std::max (0.01, Length (Subtract (view.target, view.position)) * 0.001);
		return view;
	}

	const API_AxonoPars& axonometry = projection.u.axono;
	view.projection = CameraProjection::Orthographic;
	view.archicadProjectionMode = axonometry.projMod;
	for (std::size_t index = 0; index < view.archicadProjectionMatrix.size (); ++index) {
		view.archicadProjectionMatrix[index] = axonometry.tranmat.tmx[index];
		view.archicadInverseProjectionMatrix[index] = axonometry.invtranmat.tmx[index];
	}
	const Vec3 right = Normalize (ConvertArchicadPoint (axonometry.invtranmat.tmx[0], axonometry.invtranmat.tmx[4],
	                                                    axonometry.invtranmat.tmx[8]));
	const Vec3 up = Normalize (ConvertArchicadPoint (axonometry.invtranmat.tmx[1], axonometry.invtranmat.tmx[5],
	                                                 axonometry.invtranmat.tmx[9]));
	const Vec3 backward = Normalize (ConvertArchicadPoint (axonometry.invtranmat.tmx[2], axonometry.invtranmat.tmx[6],
	                                                       axonometry.invtranmat.tmx[10]));
	Vec3 minimum = model.positions.front ();
	Vec3 maximum = minimum;
	for (const Vec3& point : model.positions) {
		minimum.x = std::min (minimum.x, point.x);
		minimum.y = std::min (minimum.y, point.y);
		minimum.z = std::min (minimum.z, point.z);
		maximum.x = std::max (maximum.x, point.x);
		maximum.y = std::max (maximum.y, point.y);
		maximum.z = std::max (maximum.z, point.z);
	}
	view.target = Scale (Add (minimum, maximum), 0.5);
	const double diagonal = std::max (Length (Subtract (maximum, minimum)), 1.0);
	view.position = Add (view.target, Scale (backward, diagonal * 2.0));
	view.up = up;
	double halfWidth = 0.0;
	double halfHeight = 0.0;
	for (const Vec3& point : model.positions) {
		const Vec3 offset = Subtract (point, view.target);
		halfWidth = std::max (halfWidth, std::abs (Dot (offset, right)));
		halfHeight = std::max (halfHeight, std::abs (Dot (offset, up)));
	}
	view.orthographicXMag = std::max (halfWidth * 1.05, 0.5);
	view.orthographicYMag = std::max (halfHeight * 1.05, 0.5);
	view.nearPlane = 0.01;
	view.farPlane = diagonal * 5.0;
	return view;
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

DropView::MaterialConversion::ArchicadMaterialProperties GetMaterialProperties (const API_MaterialType& material)
{
	return {
	    material.mtype == APIMater_GlassID,
	    static_cast<double> (material.transpPc),
	    static_cast<double> (material.specularPc),
	    static_cast<double> (material.shine),
	    static_cast<double> (material.emissionAtt),
	    (material.texture.status & APITxtr_UseAlpha) != 0 && (material.texture.status & APITxtr_TransPattern) != 0,
	};
}

const char* GetMaterialTypeName (API_MaterTypeID type)
{
	switch (type) {
		case APIMater_GeneralID:
			return "General";
		case APIMater_SimpleID:
			return "Simple";
		case APIMater_MatteID:
			return "Matte";
		case APIMater_MetalID:
			return "Metal";
		case APIMater_PlasticID:
			return "Plastic";
		case APIMater_GlassID:
			return "Glass";
		case APIMater_GlowingID:
			return "Glowing";
		case APIMater_ConstID:
			return "Constant";
		default:
			return "Unknown";
	}
}

std::string GetClearGlassCandidateDetails (const std::string& name, const API_MaterialType& material,
                                           const DropView::MaterialConversion::ArchicadMaterialProperties& properties)
{
	return name + "  |  Type: " + GetMaterialTypeName (material.mtype) +
	       "  |  Transparency: " + std::to_string (static_cast<int> (std::lround (properties.transparencyPercent))) +
	       "%  |  Specular: " + std::to_string (static_cast<int> (std::lround (properties.specularPercent))) +
	       "%  |  Shine: " + std::to_string (static_cast<int> (std::lround (properties.shine))) +
	       "  |  Emission: " + std::to_string (static_cast<int> (std::lround (properties.emissionAttenuation))) +
	       "%  |  Alpha cutout: " + (properties.usesAlphaCutout ? "yes" : "no");
}

std::vector<ClearGlassCandidate> CollectClearGlassCandidates ()
{
	Int32 visibleBodyCount = 0;
	if (ACAPI_ModelAccess_GetNum (API_BodyID, &visibleBodyCount) != NoError)
		throw std::runtime_error ("Cannot read the active 3D model");

	std::set<Int32> visibleMaterialIndices;
	API_Component3D component {};
	for (Int32 bodyIndex = 1; bodyIndex <= visibleBodyCount; ++bodyIndex) {
		component.header.typeID = API_BodyID;
		component.header.index = bodyIndex;
		if (ACAPI_ModelAccess_GetComponent (&component) != NoError)
			continue;
		const Int32 polygonCount = component.body.nPgon;
		for (Int32 polygonIndex = 1; polygonIndex <= polygonCount; ++polygonIndex) {
			component.header.typeID = API_PgonID;
			component.header.index = polygonIndex;
			if (ACAPI_ModelAccess_GetComponent (&component) == NoError && (component.pgon.status & APIPgon_Invis) == 0)
				visibleMaterialIndices.insert (component.pgon.iumat);
		}
	}

	std::vector<ClearGlassCandidate> candidates;
	for (Int32 sourceIndex : visibleMaterialIndices) {
		component.header.typeID = API_UmatID;
		component.header.index = sourceIndex;
		if (ACAPI_ModelAccess_GetComponent (&component) != NoError)
			continue;
		const API_MaterialType& sourceMaterial = component.umat.mater;
		auto properties = GetMaterialProperties (sourceMaterial);
		std::unique_ptr<IO::Location> textureLocation (sourceMaterial.texture.fileLoc);
		properties.usesAlphaCutout = DropView::MaterialConversion::UsesAlphaCutout (
		    TextureFileHasPngAlpha (textureLocation.get ()), (sourceMaterial.texture.status & APITxtr_UseAlpha) != 0,
		    (sourceMaterial.texture.status & APITxtr_TransPattern) != 0, static_cast<double> (sourceMaterial.transpPc));
		if (!DropView::MaterialConversion::IsClearGlassCandidate (properties))
			continue;
		std::string name = sourceMaterial.head.name;
		if (name.empty ())
			name = "Archicad Surface " + std::to_string (sourceIndex);
		const std::string details = GetClearGlassCandidateDetails (name, sourceMaterial, properties);
		candidates.push_back ({sourceIndex, std::move (name), details,
		                       DropView::MaterialConversion::IsHighConfidenceClearGlass (properties)});
	}
	std::sort (candidates.begin (), candidates.end (),
	           [] (const auto& left, const auto& right) { return left.name < right.name; });
	return candidates;
}

Material ReadMaterial (Int32 sourceIndex, bool exportAsClearGlass)
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
	DropView::MaterialConversion::ApplySurfaceColor (sourceMaterial.surfaceRGB.f_red, sourceMaterial.surfaceRGB.f_green,
	                                                 sourceMaterial.surfaceRGB.f_blue, material);
	material.sourceMaterialType = static_cast<std::int32_t> (sourceMaterial.mtype);
	material.sourceTransparencyPercent = sourceMaterial.transpPc;
	material.sourceSpecularPercent = sourceMaterial.specularPc;
	material.sourceShine = sourceMaterial.shine;
	material.sourceEmissionAttenuation = sourceMaterial.emissionAtt;
	DropView::MaterialConversion::ApplyEmissionColor (
	    sourceMaterial.emissionRGB.f_red, sourceMaterial.emissionRGB.f_green, sourceMaterial.emissionRGB.f_blue,
	    sourceMaterial.emissionAtt, material);
	material.texture.xSize = sourceMaterial.texture.xSize;
	material.texture.ySize = sourceMaterial.texture.ySize;
	material.texture.rotationDegrees = sourceMaterial.texture.rotAng;
	material.texture.mirrorX = (sourceMaterial.texture.status & APITxtr_MirrorX) != 0;
	material.texture.mirrorY = (sourceMaterial.texture.status & APITxtr_MirrorY) != 0;
	material.texture.useAlpha = (sourceMaterial.texture.status & APITxtr_UseAlpha) != 0;
	material.texture.transparencyPattern = (sourceMaterial.texture.status & APITxtr_TransPattern) != 0;
	std::unique_ptr<IO::Location> textureLocation (sourceMaterial.texture.fileLoc);
	LoadTextureImage (textureLocation.get (), material);
	auto properties = GetMaterialProperties (sourceMaterial);
	properties.usesAlphaCutout = material.alphaMask;
	DropView::MaterialConversion::ApplyTransparency (properties, exportAsClearGlass, material);
	return material;
}

std::size_t GetOrCreateMaterial (Int32 sourceIndex, const std::set<Int32>& clearGlassSurfaceIndices, Model& model,
                                 MaterialIndexMap& materialIndices)
{
	const auto existingMaterial = materialIndices.find (sourceIndex);
	if (existingMaterial != materialIndices.end ())
		return existingMaterial->second;

	const std::size_t materialIndex = model.materials.size ();
	model.materials.push_back (ReadMaterial (sourceIndex, clearGlassSurfaceIndices.contains (sourceIndex)));
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
                     const std::set<Int32>& clearGlassSurfaceIndices, const GroupDescriptor& elementGroup,
                     const CornerNormals& cornerNormals)
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
	constexpr std::uint64_t maxVertexCount =
	    static_cast<std::uint64_t> (std::numeric_limits<std::uint32_t>::max ()) + 1;
	const std::uint64_t existingVertexCount = static_cast<std::uint64_t> (model.positions.size ());
	if (existingVertexCount > maxVertexCount ||
	    static_cast<std::uint64_t> (vertices.size ()) > maxVertexCount - existingVertexCount)
		throw std::length_error ("Vertex count exceeds the GLB 32-bit index limit");
	const std::size_t materialIndex =
	    GetOrCreateMaterial (polygon.iumat, clearGlassSurfaceIndices, model, materialIndices);
	const Material& material = model.materials[materialIndex];
	const GroupDescriptor groupDescriptor =
	    groupingMode == GroupingMode::Surface
	        ? GroupDescriptor {"surface:" + std::to_string (material.sourceIndex), material.name}
	        : elementGroup;
	DropView::Glb::Group& group = model.groups[GetOrCreateGroup (groupDescriptor, model, groupIndices)];
	Primitive& primitive = GetOrCreatePrimitive (group, materialIndex);
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
                  GroupingMode groupingMode, const GroupDescriptor& elementGroup,
                  const std::set<Int32>& clearGlassSurfaceIndices, ExportStatistics& statistics,
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
			                materialIndices, groupIndices, groupingMode, clearGlassSurfaceIndices, elementGroup,
			                cornerNormals);
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
                     GroupIndexMap& groupIndices, GroupingMode groupingMode,
                     const std::set<Int32>& clearGlassSurfaceIndices, ExportStatistics& statistics)
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
			CollectBody (bodyIndex, model, materialIndices, groupIndices, groupingMode, elementGroup,
			             clearGlassSurfaceIndices, statistics, skippedPolygonCount, lastPolygonError);
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

bool CollectMesh (Model& model, GroupingMode groupingMode, const std::set<Int32>& clearGlassSurfaceIndices,
                  ExportStatistics& statistics)
{
	Int32 visibleBodyCount = 0;
	if (ACAPI_ModelAccess_GetNum (API_BodyID, &visibleBodyCount) != NoError)
		return false;

	MaterialIndexMap materialIndices;
	GroupIndexMap groupIndices;
	const std::vector<VisibleBodyGroup> visibleBodyGroups = GetVisibleBodyGroups (visibleBodyCount);
	for (const VisibleBodyGroup& group : visibleBodyGroups)
		CollectElement (group, model, materialIndices, groupIndices, groupingMode, clearGlassSurfaceIndices,
		                statistics);
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
	std::vector<ClearGlassCandidate> clearGlassCandidates;
	try {
		Scoped3DWindowSight sight;
		if (sight.GetError () != NoError) {
			ACAPI_WriteReport (
			    GS::UniString::Printf ("The active 3D window model is unavailable (error: %d). No GLB was created.",
			                           sight.GetError ()),
			    true);
			return;
		}
		clearGlassCandidates = CollectClearGlassCandidates ();
	} catch (const std::exception& error) {
		ACAPI_WriteReport (
		    GS::UniString::Printf ("Reading surfaces from the active 3D window failed: %s. No GLB was created.",
		                           error.what ()),
		    true);
		return;
	}

	ExportOptionsDialog optionsDialog (std::move (clearGlassCandidates), LoadDesignCreditsPreference ());
	if (!optionsDialog.Invoke ())
		return;
	const GroupingMode groupingMode = optionsDialog.GetGroupingMode ();
	const std::set<Int32> clearGlassSurfaceIndices = optionsDialog.GetClearGlassSurfaceIndices ();
	const std::string designCredits = optionsDialog.GetDesignCredits ();
	SaveDesignCreditsPreference (designCredits);

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
	model.designCredits = designCredits;
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
		if (!CollectMesh (model, groupingMode, clearGlassSurfaceIndices, statistics)) {
			ACAPI_WriteReport ("The active 3D window does not contain exportable geometry.", true);
			return;
		}
		try {
			model.initialView = CollectInitialView (model);
		} catch (const std::exception& error) {
			ACAPI_WriteReport (
			    GS::UniString::Printf (
			        "The active 3D viewpoint could not be stored (%s). Geometry export will continue without a camera.",
			        error.what ()),
			    false);
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
