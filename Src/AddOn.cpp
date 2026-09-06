#include "APIEnvir.h"
#include "ACAPinc.h"
#include "Resources.h"
#include "GlbExporter.hpp"

static GS::Optional<API_Guid> windowEventHandlerId;

static bool Is3DModelWindowActive ()
{
	API_WindowInfo windowInfo {};
	return ACAPI_Window_GetCurrentWindow (&windowInfo) == NoError && windowInfo.typeID == APIWind_3DModelID;
}

static void SetExportMenuEnabled (bool enabled)
{
	API_MenuItemRef menuItem {};
	menuItem.menuResID = ADDON_MENU;
	menuItem.itemIndex = 1;

	GSFlags flags = 0;
	if (ACAPI_MenuItem_GetMenuItemFlags (&menuItem, &flags) != NoError)
		return;

	if (enabled)
		flags &= ~API_MenuItemDisabled;
	else
		flags |= API_MenuItemDisabled;
	ACAPI_MenuItem_SetMenuItemFlags (&menuItem, &flags);
}

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
    return ACAPI_MenuItem_RegisterMenu (ADDON_MENU, 0, MenuCode_UserDef, MenuFlag_Default);
}

GSErrCode Initialize ()
{
	const GSErrCode error = ACAPI_MenuItem_InstallMenuHandler (ADDON_MENU, MenuCommandHandler);
	if (error != NoError)
		return error;

	windowEventHandlerId.New ();
	const GSErrCode notificationError = ACAPI_Notification_RegisterEventHandler (
		GS::NewOwned<ExportWindowEventHandler> (), *windowEventHandlerId);
	if (notificationError != NoError)
		windowEventHandlerId.Clear ();

	SetExportMenuEnabled (Is3DModelWindowActive ());
	return NoError;
}

GSErrCode FreeData ()
{
	if (windowEventHandlerId.HasValue ()) {
		ACAPI_Notification_UnregisterEventHandler (*windowEventHandlerId);
		windowEventHandlerId.Clear ();
	}
    return NoError;
}
