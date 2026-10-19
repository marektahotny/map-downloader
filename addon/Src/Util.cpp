#include "Util.hpp"

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include <cstdio>
#include <ctime>
#include <algorithm>

namespace Util {

GS::UniString ToUni (const std::string& utf8)
{
	return GS::UniString (utf8.c_str (), CC_UTF8);
}


std::string ToUtf8 (const GS::UniString& uni)
{
	return std::string (uni.ToCStr (0, MaxUSize, CC_UTF8).Get ());
}


std::wstring Utf8ToWide (const std::string& utf8)
{
	if (utf8.empty ())
		return std::wstring ();
	int n = MultiByteToWideChar (CP_UTF8, 0, utf8.data (), (int) utf8.size (), nullptr, 0);
	std::wstring w ((size_t) n, 0);
	MultiByteToWideChar (CP_UTF8, 0, utf8.data (), (int) utf8.size (), (LPWSTR) &w[0], n);
	return w;
}


std::string WideToUtf8 (const std::wstring& wide)
{
	if (wide.empty ())
		return std::string ();
	int n = WideCharToMultiByte (CP_UTF8, 0, (LPCWSTR) wide.data (), (int) wide.size (), nullptr, 0, nullptr, nullptr);
	std::string s ((size_t) n, 0);
	WideCharToMultiByte (CP_UTF8, 0, (LPCWSTR) wide.data (), (int) wide.size (), &s[0], n, nullptr, nullptr);
	return s;
}


std::string FormatDouble (double v, int decimals)
{
	char buf[64];
	snprintf (buf, sizeof (buf), "%.*f", decimals, v);
	return std::string (buf);
}


std::string UrlEncode (const std::string& s)
{
	static const char* hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : s) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == ',' || c == ':') {
			out += (char) c;
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}


std::string ReplaceAll (std::string s, const std::string& from, const std::string& to)
{
	if (from.empty ())
		return s;
	size_t pos = 0;
	while ((pos = s.find (from, pos)) != std::string::npos) {
		s.replace (pos, from.size (), to);
		pos += to.size ();
	}
	return s;
}


std::string Trim (const std::string& s)
{
	size_t b = s.find_first_not_of (" \t\r\n");
	if (b == std::string::npos)
		return std::string ();
	size_t e = s.find_last_not_of (" \t\r\n");
	return s.substr (b, e - b + 1);
}


bool StartsWith (const std::string& s, const std::string& prefix)
{
	return s.size () >= prefix.size () && s.compare (0, prefix.size (), prefix) == 0;
}


std::string ToLower (std::string s)
{
	std::transform (s.begin (), s.end (), s.begin (), [] (unsigned char c) { return (char) tolower (c); });
	return s;
}


bool ReadFile (const std::wstring& path, std::string& out)
{
	FILE* f = _wfopen ((const wchar_t*) path.c_str (), (const wchar_t*) L"rb");
	if (f == nullptr)
		return false;
	out.clear ();
	char buf[65536];
	size_t n;
	while ((n = fread (buf, 1, sizeof (buf), f)) > 0)
		out.append (buf, n);
	fclose (f);
	return true;
}


bool WriteFile (const std::wstring& path, const std::string& data)
{
	FILE* f = _wfopen ((const wchar_t*) path.c_str (), (const wchar_t*) L"wb");
	if (f == nullptr)
		return false;
	size_t written = fwrite (data.data (), 1, data.size (), f);
	fclose (f);
	return written == data.size ();
}


std::wstring AppDataFolder ()
{
	wchar_t path[MAX_PATH] = {};
	if (SHGetFolderPathW (nullptr, CSIDL_APPDATA, nullptr, 0, (LPWSTR) path) != S_OK)
		return std::wstring ();
	std::wstring dir = std::wstring (path) + std::wstring ((const wchar_t*) L"\\PilonMapDownloader");
	CreateDirectoryW ((LPCWSTR) dir.c_str (), nullptr);
	return dir;
}


std::wstring DirName (const std::wstring& path)
{
	size_t p = path.find_last_of ((const wchar_t*) L"\\/");
	return p == std::wstring::npos ? std::wstring () : path.substr (0, p);
}


std::wstring StripExtension (const std::wstring& path)
{
	size_t slash = path.find_last_of ((const wchar_t*) L"\\/");
	size_t dot = path.find_last_of ((wchar_t) '.');
	if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
		return path;
	return path.substr (0, dot);
}


static std::wstring FilterWithNulls (const std::wstring& filter)
{
	// filter is given as "Popis|*.ext|Popis2|*.ext2" -> replace | with \0
	std::wstring f = filter;
	for (auto& ch : f)
		if (ch == (wchar_t) '|')
			ch = 0;
	f.push_back (0);
	f.push_back (0);
	return f;
}


std::wstring SaveFileDialog (const std::wstring& title, const std::wstring& defName, const std::wstring& filter, const std::wstring& defExt)
{
	wchar_t fileName[MAX_PATH * 4] = {};
	size_t n = std::min (defName.size (), (size_t) (MAX_PATH * 4 - 1));
	memcpy (fileName, defName.c_str (), n * sizeof (wchar_t));

	std::wstring filt = FilterWithNulls (filter);

	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof (ofn);
	ofn.hwndOwner = GetActiveWindow ();
	ofn.lpstrFilter = (LPCWSTR) filt.c_str ();
	ofn.lpstrFile = (LPWSTR) fileName;
	ofn.nMaxFile = MAX_PATH * 4;
	ofn.lpstrTitle = (LPCWSTR) title.c_str ();
	ofn.lpstrDefExt = (LPCWSTR) defExt.c_str ();
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetSaveFileNameW (&ofn))
		return std::wstring ();
	return std::wstring (fileName);
}


std::wstring OpenFileDialog (const std::wstring& title, const std::wstring& filter)
{
	wchar_t fileName[MAX_PATH * 4] = {};
	std::wstring filt = FilterWithNulls (filter);

	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof (ofn);
	ofn.hwndOwner = GetActiveWindow ();
	ofn.lpstrFilter = (LPCWSTR) filt.c_str ();
	ofn.lpstrFile = (LPWSTR) fileName;
	ofn.nMaxFile = MAX_PATH * 4;
	ofn.lpstrTitle = (LPCWSTR) title.c_str ();
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW (&ofn))
		return std::wstring ();
	return std::wstring (fileName);
}


void Log (const std::string& msg)
{
	std::wstring dir = AppDataFolder ();
	if (dir.empty ())
		return;
	std::wstring path = dir + std::wstring ((const wchar_t*) L"\\log.txt");
	FILE* f = _wfopen ((const wchar_t*) path.c_str (), (const wchar_t*) L"ab");
	if (f == nullptr)
		return;
	time_t t = time (nullptr);
	char ts[32];
	strftime (ts, sizeof (ts), "%Y-%m-%d %H:%M:%S", localtime (&t));
	fprintf (f, "[%s] %s\r\n", ts, msg.c_str ());
	fclose (f);
}

}	// namespace Util
