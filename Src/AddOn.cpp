#include "APIEnvir.h"
#include "ACAPinc.h"
#include "Resources.h"
#include "GlbExporter.hpp"

#include <cmath>
#include <set>

[[maybe_unused]] static void ReportSelectedWall3D ()
{
	API_SelectionInfo selectionInfo {};
	GS::Array<API_Neig> selectedElements;
	const GSErrCode selectionError = ACAPI_Selection_Get (&selectionInfo, &selectedElements, false);
	BMKillHandle (reinterpret_cast<GSHandle*> (&selectionInfo.marquee.coords));
	if (selectionError == APIERR_NOSEL || selectionInfo.typeID == API_SelEmpty) {
		ACAPI_WriteReport ("Jelölj ki pontosan egy falat.", true);
		return;
	}

	API_Elem_Head selectedWall {};
	Int32 wallCount = 0;
	for (const API_Neig& selectedElement : selectedElements) {
		API_Element element {};
		element.header.guid = selectedElement.guid;
		if (ACAPI_Element_Get (&element) == NoError && element.header.type == API_WallID) {
			selectedWall = element.header;
			++wallCount;
		}
	}
	if (wallCount != 1) {
		ACAPI_WriteReport (GS::UniString::Printf ("Pontosan egy falat jelölj ki. Kijelölt falak: %d", wallCount), true);
		return;
	}

	API_ElemInfo3D info3D {};
	const GSErrCode modelError = ACAPI_ModelAccess_Get3DInfo (selectedWall, &info3D);
	if (modelError != NoError) {
		ACAPI_WriteReport (GS::UniString::Printf ("A fal 3D modellje nem olvasható (hibakód: %d).", modelError), true);
		return;
	}

	Int32 vertexCount = 0;
	Int32 polygonCount = 0;
	Int32 bodyCount = 0;
	std::set<Int32> materialIndices;
	for (Int32 bodyIndex = info3D.fbody; bodyIndex <= info3D.lbody; ++bodyIndex) {
		API_Component3D component {};
		component.header.typeID = API_BodyID;
		component.header.index = bodyIndex;
		if (ACAPI_ModelAccess_GetComponent (&component) != NoError)
			continue;
		++bodyCount;
		vertexCount += component.body.nVert;
		polygonCount += component.body.nPgon;
		for (Int32 polygonIndex = 1; polygonIndex <= component.body.nPgon; ++polygonIndex) {
			component.header.typeID = API_PgonID;
			component.header.index = polygonIndex;
			if (ACAPI_ModelAccess_GetComponent (&component) == NoError)
				materialIndices.insert (component.pgon.iumat);
		}
	}

	GS::UniString report = GS::UniString::Printf (
		"Fal 3D modellje\nGUID: %T\nTestek: %d\nCsúcsok: %d\nPoligonok: %d\nFelületanyagok: %d\n",
		APIGuidToString (selectedWall.guid).ToPrintf (), bodyCount, vertexCount, polygonCount,
		static_cast<Int32> (materialIndices.size ()));
	for (const Int32 materialIndex : materialIndices) {
		API_Component3D materialComponent {};
		materialComponent.header.typeID = API_UmatID;
		materialComponent.header.index = materialIndex;
		if (ACAPI_ModelAccess_GetComponent (&materialComponent) != NoError)
			continue;
		const API_MaterialType& material = materialComponent.umat.mater;
		report += GS::UniString::Printf ("\n%s [%d]\n  RGB: %.3f, %.3f, %.3f\n  Átlátszóság: %d%%\n",
			material.head.name, materialIndex, material.surfaceRGB.f_red,
			material.surfaceRGB.f_green, material.surfaceRGB.f_blue, material.transpPc);
		delete materialComponent.umat.mater.texture.fileLoc;
	}
	ACAPI_WriteReport (report, true);
}

[[maybe_unused]] static void ReportSelectedWalls ()
{
	API_SelectionInfo selectionInfo {};
	GS::Array<API_Neig> selectedElements;
	const GSErrCode selectionError = ACAPI_Selection_Get (&selectionInfo, &selectedElements, false);
	BMKillHandle (reinterpret_cast<GSHandle*> (&selectionInfo.marquee.coords));

	if (selectionError == APIERR_NOSEL || selectionInfo.typeID == API_SelEmpty) {
		ACAPI_WriteReport ("Nincs kijelölt elem. Jelöld ki a két falat, majd próbáld újra.", true);
		return;
	}
	if (selectionError != NoError) {
		ACAPI_WriteReport (GS::UniString::Printf ("A kijelölés nem olvasható (hibakód: %d).", selectionError), true);
		return;
	}

	GS::UniString report = GS::UniString::Printf ("Kijelölt elemek: %d\n", selectionInfo.sel_nElem);
	Int32 wallCount = 0;
	for (const API_Neig& selectedElement : selectedElements) {
		API_Element element {};
		element.header.guid = selectedElement.guid;
		if (ACAPI_Element_Get (&element) != NoError || element.header.type != API_WallID)
			continue;

		++wallCount;
		const double deltaX = element.wall.endC.x - element.wall.begC.x;
		const double deltaY = element.wall.endC.y - element.wall.begC.y;
		const double length = std::hypot (deltaX, deltaY);
		report += GS::UniString::Printf (
			"\nFal %d\n"
			"  Kezdőpont: %.3f, %.3f m\n"
			"  Végpont: %.3f, %.3f m\n"
			"  Referenciavonal hossza: %.3f m\n"
			"  Magasság: %.3f m\n"
			"  Vastagság: %.3f m\n"
			"  GUID: %T\n",
			wallCount,
			element.wall.begC.x, element.wall.begC.y,
			element.wall.endC.x, element.wall.endC.y,
			length, element.wall.height, element.wall.thickness,
			APIGuidToString (element.header.guid).ToPrintf ());
	}

	if (wallCount == 0)
		report += "\nA kijelölésben nincs fal.";
	else
		report += GS::UniString::Printf ("\nKiolvasott falak: %d", wallCount);

	ACAPI_WriteReport (report, true);
}

static GSErrCode MenuCommandHandler (const API_MenuParams* params)
{
	if (params->menuItemRef.menuResID == ADDON_MENU) {
		if (params->menuItemRef.itemIndex == 1)
			ExportSelectedElementsToGlb ();
	}
    return NoError;
}

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
    RSGetIndString (&envir->addOnInfo.name, ADDON_INFO, 1, ACAPI_GetOwnResModule ());
    RSGetIndString (&envir->addOnInfo.description, ADDON_INFO, 2, ACAPI_GetOwnResModule ());
    return APIAddon_Normal;
}

GSErrCode RegisterInterface ()
{
    return ACAPI_MenuItem_RegisterMenu (ADDON_MENU, 0, MenuCode_UserDef, MenuFlag_Default);
}

GSErrCode Initialize ()
{
	const GSErrCode error = ACAPI_MenuItem_InstallMenuHandler (ADDON_MENU, MenuCommandHandler);
	if (error != NoError)
		return error;

	API_MenuItemRef menuItem {};
	menuItem.menuResID = ADDON_MENU;
	menuItem.itemIndex = 1;
	GSFlags flags = 0;
	if (ACAPI_MenuItem_GetMenuItemFlags (&menuItem, &flags) == NoError) {
		flags &= ~API_MenuItemDisabled;
		ACAPI_MenuItem_SetMenuItemFlags (&menuItem, &flags);
	}
	return NoError;
}

GSErrCode FreeData ()
{
    return NoError;
}
