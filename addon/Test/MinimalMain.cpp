// Minimal diagnostic add-on: one menu item showing an alert. No palette, no browser.
#include "APIEnvir.h"
#include "ACAPinc.h"

#ifndef TEST_RES_BASE
#define TEST_RES_BASE 32000
#endif

GSErrCode __ACENV_CALL MenuCommandHandler (const API_MenuParams*)
{
	ACAPI_WriteReport ("Pilon test add-on works.", true);
	return NoError;
}

API_AddonType __ACDLL_CALL CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());
	return APIAddon_Normal;
}

GSErrCode __ACDLL_CALL RegisterInterface (void)
{
	return ACAPI_Register_Menu (32500, 0, MenuCode_UserDef, MenuFlag_Default);
}

GSErrCode __ACENV_CALL Initialize (void)
{
	return ACAPI_Install_MenuHandler (32500, MenuCommandHandler);
}

GSErrCode __ACENV_CALL FreeData (void)
{
	return NoError;
}
