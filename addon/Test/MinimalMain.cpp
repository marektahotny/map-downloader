// Minimal diagnostic add-on: one menu item showing an alert. No palette, no browser.
#include "APIEnvir.h"
#include "ACAPinc.h"

#include <windows.h>
#include <stdio.h>

// Probe: appends every step Archicad performs with this add-on to C:\Users\Public\pilon_probe.log
static void Probe (const char* step)
{
	FILE* f = fopen ("C:\\Users\\Public\\pilon_probe.log", "ab");
	if (f == nullptr)
		return;
	char exe[MAX_PATH] = {};
	GetModuleFileNameA (nullptr, exe, MAX_PATH);
	SYSTEMTIME t;
	GetLocalTime (&t);
	fprintf (f, "%02d:%02d:%02d.%03d pid=%lu %s :: %s\r\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
			 (unsigned long) GetCurrentProcessId (), exe, step);
	fclose (f);
}

struct ProbeOnLoad {
	ProbeOnLoad ()	{ Probe ("DLL loaded (static init)"); }
	~ProbeOnLoad ()	{ Probe ("DLL unloaded"); }
};
static ProbeOnLoad gProbeOnLoad;

GSErrCode __ACENV_CALL MenuCommandHandler (const API_MenuParams*)
{
	Probe ("menu command");
	ACAPI_WriteReport ("Pilon test add-on works.", true);
	return NoError;
}

API_AddonType __ACDLL_CALL CheckEnvironment (API_EnvirParams* envir)
{
	char buf[200];
	sprintf (buf, "CheckEnvironment called, serverInfo mainVersion=%d releaseVersion=%d buildNum=%d", (int) envir->serverInfo.mainVersion,
			 (int) envir->serverInfo.releaseVersion, (int) envir->serverInfo.buildNum);
	Probe (buf);
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());
	return APIAddon_Normal;
}

GSErrCode __ACDLL_CALL RegisterInterface (void)
{
	Probe ("RegisterInterface called");
	return ACAPI_Register_Menu (32500, 0, MenuCode_UserDef, MenuFlag_Default);
}

GSErrCode __ACENV_CALL Initialize (void)
{
	Probe ("Initialize called");
	return ACAPI_Install_MenuHandler (32500, MenuCommandHandler);
}

GSErrCode __ACENV_CALL FreeData (void)
{
	return NoError;
}
