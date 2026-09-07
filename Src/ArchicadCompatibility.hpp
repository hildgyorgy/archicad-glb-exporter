#pragma once

#include "ACAPinc.h"

#ifndef DROPVIEW_ARCHICAD_VERSION
	#error "DROPVIEW_ARCHICAD_VERSION must be provided by the build system."
#endif

namespace ArchicadCompatibility {

inline GSErrCode SelectSight (void* sight, void** previousSight)
{
#if DROPVIEW_ARCHICAD_VERSION >= 27
	return ACAPI_Sight_SelectSight (sight, previousSight);
#else
	return ACAPI_3D_SelectSight (sight, previousSight);
#endif
}

inline GSErrCode GetConnectedElements (const API_Guid& guid, API_ElemTypeID type, GS::Array<API_Guid>* connectedElements)
{
#if DROPVIEW_ARCHICAD_VERSION >= 27
	return ACAPI_Grouping_GetConnectedElements (guid, type, connectedElements);
#else
	return ACAPI_Element_GetConnectedElements (guid, type, connectedElements);
#endif
}

inline GSErrCode IsCurtainWallPanelDegenerate (API_Guid* guid, bool* isDegenerate)
{
#if DROPVIEW_ARCHICAD_VERSION >= 27
	return ACAPI_CurtainWall_IsCWPanelDegenerate (guid, isDegenerate);
#else
	return ACAPI_Database (APIDb_IsCWPanelDegenerateID, guid, isDegenerate);
#endif
}

inline GSErrCode GetModelComponent (API_Component3D* component)
{
#if DROPVIEW_ARCHICAD_VERSION >= 27
	return ACAPI_ModelAccess_GetComponent (component);
#else
	return ACAPI_3D_GetComponent (component);
#endif
}

inline GSErrCode GetModelComponentCount (API_3DTypeID type, Int32* count)
{
#if DROPVIEW_ARCHICAD_VERSION >= 27
	return ACAPI_ModelAccess_GetNum (type, count);
#else
	return ACAPI_3D_GetNum (type, count);
#endif
}

inline GSErrCode GetTextureCoordinate (API_TexCoordPars* parameters, API_UVCoord* coordinate)
{
#if DROPVIEW_ARCHICAD_VERSION >= 27
	return ACAPI_ModelAccess_GetTextureCoord (parameters, coordinate);
#else
	return ACAPI_Goodies (APIAny_GetTextureCoordID, parameters, coordinate);
#endif
}

inline GSErrCode GetCurrentWindow (API_WindowInfo* windowInfo)
{
#if DROPVIEW_ARCHICAD_VERSION >= 27
	return ACAPI_Window_GetCurrentWindow (windowInfo);
#else
	return ACAPI_Database (APIDb_GetCurrentWindowID, windowInfo, nullptr);
#endif
}

inline GSErrCode GetMenuItemFlags (API_MenuItemRef* menuItem, GSFlags* flags)
{
#if DROPVIEW_ARCHICAD_VERSION >= 27
	return ACAPI_MenuItem_GetMenuItemFlags (menuItem, flags);
#else
	return ACAPI_Interface (APIIo_GetMenuItemFlagsID, menuItem, flags);
#endif
}

inline GSErrCode SetMenuItemFlags (API_MenuItemRef* menuItem, GSFlags* flags)
{
#if DROPVIEW_ARCHICAD_VERSION >= 27
	return ACAPI_MenuItem_SetMenuItemFlags (menuItem, flags);
#else
	return ACAPI_Interface (APIIo_SetMenuItemFlagsID, menuItem, flags);
#endif
}

inline GSErrCode RegisterMenu (short menuResourceId, short promptResourceId, APIMenuCodeID position, GSFlags flags)
{
#if DROPVIEW_ARCHICAD_VERSION >= 27
	return ACAPI_MenuItem_RegisterMenu (menuResourceId, promptResourceId, position, flags);
#else
	return ACAPI_Register_Menu (menuResourceId, promptResourceId, position, flags);
#endif
}

inline GSErrCode InstallMenuHandler (short menuResourceId, APIMenuCommandProc* handler)
{
#if DROPVIEW_ARCHICAD_VERSION >= 27
	return ACAPI_MenuItem_InstallMenuHandler (menuResourceId, handler);
#else
	return ACAPI_Install_MenuHandler (menuResourceId, handler);
#endif
}

#if DROPVIEW_ARCHICAD_VERSION < 28
inline GSErrCode InstallWindowChangeHandler (APIProjectEventHandlerProc* handler)
{
#if DROPVIEW_ARCHICAD_VERSION >= 27
	return ACAPI_ProjectOperation_CatchProjectEvent (APINotify_ChangeWindow, handler);
#else
	return ACAPI_Notify_CatchProjectEvent (APINotify_ChangeWindow, handler);
#endif
}

inline void UninstallWindowChangeHandler ()
{
#if DROPVIEW_ARCHICAD_VERSION >= 27
	ACAPI_ProjectOperation_CatchProjectEvent (APINotify_ChangeWindow, nullptr);
#else
	ACAPI_Notify_CatchProjectEvent (APINotify_ChangeWindow, nullptr);
#endif
}
#endif

} // namespace ArchicadCompatibility
