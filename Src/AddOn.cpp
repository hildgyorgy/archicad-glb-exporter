#include "APIEnvir.h"
#include "ACAPinc.h"
#include "ArchicadCompatibility.hpp"
#include "Resources.h"
#include "GlbExporter.hpp"

#if DROPVIEW_ARCHICAD_VERSION >= 28
static GS::Optional<API_Guid> windowEventHandlerId;
#else
static bool windowChangeHandlerInstalled = false;
#endif

static bool Is3DModelWindowActive ()
{
	API_WindowInfo windowInfo {};
	return ArchicadCompatibility::GetCurrentWindow (&windowInfo) == NoError && windowInfo.typeID == APIWind_3DModelID;
}

static void SetExportMenuEnabled (bool enabled)
{
	API_MenuItemRef menuItem {};
	menuItem.menuResID = ADDON_MENU;
	menuItem.itemIndex = 1;

	GSFlags flags = 0;
	if (ArchicadCompatibility::GetMenuItemFlags (&menuItem, &flags) != NoError)
		return;

	if (enabled)
		flags &= ~API_MenuItemDisabled;
	else
		flags |= API_MenuItemDisabled;
	ArchicadCompatibility::SetMenuItemFlags (&menuItem, &flags);
}

#if DROPVIEW_ARCHICAD_VERSION >= 28
class ExportWindowEventHandler final : public API_IWindowEventHandler {
public:
	void OnWindowBroughtForward (const API_WindowInfo& window) const override
	{
		SetExportMenuEnabled (window.typeID == APIWind_3DModelID);
	}

	void OnWindowSentBackward (const API_WindowInfo&) const override
	{
		SetExportMenuEnabled (false);
	}
};
#else
static GSErrCode __ACENV_CALL ExportProjectEventHandler (API_NotifyEventID notification, Int32)
{
	if (notification == APINotify_ChangeWindow)
		SetExportMenuEnabled (Is3DModelWindowActive ());
	return NoError;
}
#endif

static GSErrCode MenuCommandHandler (const API_MenuParams* params)
{
	if (params->menuItemRef.menuResID == ADDON_MENU) {
		if (params->menuItemRef.itemIndex == 1) {
			if (!Is3DModelWindowActive ()) {
				SetExportMenuEnabled (false);
				ACAPI_WriteReport ("GLB export is available only from the active 3D window.", true);
				return NoError;
			}
			ExportSelectedElementsToGlb ();
		}
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
    return ArchicadCompatibility::RegisterMenu (ADDON_MENU, 0, MenuCode_UserDef, MenuFlag_Default);
}

GSErrCode Initialize ()
{
	const GSErrCode error = ArchicadCompatibility::InstallMenuHandler (ADDON_MENU, MenuCommandHandler);
	if (error != NoError)
		return error;

#if DROPVIEW_ARCHICAD_VERSION >= 28
	windowEventHandlerId.New ();
	const GSErrCode notificationError = ACAPI_Notification_RegisterEventHandler (
		GS::NewOwned<ExportWindowEventHandler> (), *windowEventHandlerId);
	if (notificationError != NoError)
		windowEventHandlerId.Clear ();
#else
	windowChangeHandlerInstalled = ArchicadCompatibility::InstallWindowChangeHandler (ExportProjectEventHandler) == NoError;
#endif

	SetExportMenuEnabled (Is3DModelWindowActive ());
	return NoError;
}

GSErrCode FreeData ()
{
#if DROPVIEW_ARCHICAD_VERSION >= 28
	if (windowEventHandlerId.HasValue ()) {
		ACAPI_Notification_UnregisterEventHandler (*windowEventHandlerId);
		windowEventHandlerId.Clear ();
	}
#else
	if (windowChangeHandlerInstalled) {
		ArchicadCompatibility::UninstallWindowChangeHandler ();
		windowChangeHandlerInstalled = false;
	}
#endif
    return NoError;
}
