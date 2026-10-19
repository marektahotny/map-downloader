#ifndef PILON_UTIL_HPP
#define PILON_UTIL_HPP

#include <string>
#include <vector>

#include "APIEnvir.h"
#include "ACAPinc.h"

namespace Util {

	// UTF-8 <-> GS::UniString
	GS::UniString	ToUni (const std::string& utf8);
	std::string		ToUtf8 (const GS::UniString& uni);

	// UTF-8 <-> UTF-16 (Windows)
	std::wstring	Utf8ToWide (const std::string& utf8);
	std::string		WideToUtf8 (const std::wstring& wide);

	std::string		FormatDouble (double v, int decimals);
	std::string		UrlEncode (const std::string& s);
	std::string		ReplaceAll (std::string s, const std::string& from, const std::string& to);
	std::string		Trim (const std::string& s);
	bool			StartsWith (const std::string& s, const std::string& prefix);
	std::string		ToLower (std::string s);

	// Files (UTF-8 paths)
	bool			ReadFile (const std::wstring& path, std::string& out);
	bool			WriteFile (const std::wstring& path, const std::string& data);
	std::wstring	AppDataFolder ();			// %APPDATA%\PilonMapDownloader (created if missing)
	std::wstring	DirName (const std::wstring& path);
	std::wstring	StripExtension (const std::wstring& path);

	// Native Windows file dialogs; return empty string if cancelled
	std::wstring	SaveFileDialog (const std::wstring& title, const std::wstring& defName, const std::wstring& filter, const std::wstring& defExt);
	std::wstring	OpenFileDialog (const std::wstring& title, const std::wstring& filter);

	void			Log (const std::string& msg);	// appends to %APPDATA%\PilonMapDownloader\log.txt
}

#endif
