// Pilon Map Downloader - Archicad 23 add-on
// Map palette for downloading Slovak geodata (UGKK SR): cadastre, orthophoto, terrain DMR 5.0.

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "MapPalette.hpp"
#include "ResourceIds.hpp"

static void ShowOrHidePalette ()
{
	if (MapPalette::HasInstance () && MapPalette::GetInstance ().IsVisible ()) {
		MapPalette::GetInstance ().Hide ();
	} else {
		if (!MapPalette::HasInstance ())
			MapPalette::CreateInstance ();
		MapPalette::GetInstance ().Show ();
	}
}


GSErrCode __ACENV_CALL MenuCommandHandler (const API_MenuParams* menuParams)
{
	if (menuParams->menuItemRef.menuResID == ID_MENU_STRINGS && menuParams->menuItemRef.itemIndex == 1)
		ShowOrHidePalette ();
	return NoError;
}


API_AddonType __ACDLL_CALL CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, ID_ADDON_INFO, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, ID_ADDON_INFO, 2, ACAPI_GetOwnResModule ());
	return APIAddon_Preload;
}


GSErrCode __ACDLL_CALL RegisterInterface (void)
{
	GSErrCode err = ACAPI_Register_SupportedService (MapPalette::CommandId, 1L);
	if (err != NoError)
		return err;
	return ACAPI_Register_Menu (ID_MENU_STRINGS, ID_MENU_PROMPT, MenuCode_UserDef, MenuFlag_Default);
}


GSErrCode __ACENV_CALL Initialize (void)
{
	GSErrCode err = ACAPI_Install_ModulCommandHandler (MapPalette::CommandId, 1L, MapPalette::RequestCommandHandler);
	if (err != NoError)
		return err;
	err = ACAPI_Install_MenuHandler (ID_MENU_STRINGS, MenuCommandHandler);
	if (err != NoError)
		return err;
	return MapPalette::RegisterPaletteControlCallBack ();
}


GSErrCode __ACENV_CALL FreeData (void)
{
	return NoError;
}
