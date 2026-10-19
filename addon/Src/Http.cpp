#include "Http.hpp"
#include "Util.hpp"

#include <windows.h>
#include <winhttp.h>

#include <vector>

namespace Http {

static std::string LastErrorText (const char* where)
{
	DWORD code = GetLastError ();
	char buf[160];
	snprintf (buf, sizeof (buf), "%s zlyhalo (WinHTTP chyba %lu)", where, (unsigned long) code);
	return buf;
}


Response Get (const std::string& url, int timeoutSec)
{
	Response r;
	std::wstring wurl = Util::Utf8ToWide (url);

	URL_COMPONENTS uc = {};
	uc.dwStructSize = sizeof (uc);
	wchar_t host[512] = {};
	wchar_t path[8192] = {};
	uc.lpszHostName = (LPWSTR) host;
	uc.dwHostNameLength = 511;
	uc.lpszUrlPath = (LPWSTR) path;
	uc.dwUrlPathLength = 8191;
	wchar_t extra[16384] = {};
	uc.lpszExtraInfo = (LPWSTR) extra;
	uc.dwExtraInfoLength = 16383;

	if (!WinHttpCrackUrl ((LPCWSTR) wurl.c_str (), 0, 0, &uc)) {
		r.error = LastErrorText ("WinHttpCrackUrl");
		return r;
	}

	std::wstring object = std::wstring (path) + std::wstring (extra);
	bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

	HINTERNET session = WinHttpOpen ((LPCWSTR) L"PilonMapDownloader/1.0 (Archicad 23)",
									 WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (session == nullptr) {
		r.error = LastErrorText ("WinHttpOpen");
		return r;
	}

	// Try to pick up IE / system proxy configuration
	WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ieCfg = {};
	if (WinHttpGetIEProxyConfigForCurrentUser (&ieCfg)) {
		if (ieCfg.lpszProxy != nullptr) {
			WINHTTP_PROXY_INFO pi = {};
			pi.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
			pi.lpszProxy = ieCfg.lpszProxy;
			pi.lpszProxyBypass = ieCfg.lpszProxyBypass;
			WinHttpSetOption (session, WINHTTP_OPTION_PROXY, &pi, sizeof (pi));
		}
		if (ieCfg.lpszAutoConfigUrl)	GlobalFree (ieCfg.lpszAutoConfigUrl);
		if (ieCfg.lpszProxy)			GlobalFree (ieCfg.lpszProxy);
		if (ieCfg.lpszProxyBypass)		GlobalFree (ieCfg.lpszProxyBypass);
	}

	int ms = timeoutSec * 1000;
	WinHttpSetTimeouts (session, 15000, 15000, ms, ms);

	// Enable TLS 1.2 explicitly (older Windows 7/8 defaults)
	DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
	WinHttpSetOption (session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof (protocols));

	HINTERNET connect = WinHttpConnect (session, (LPCWSTR) host, uc.nPort, 0);
	if (connect == nullptr) {
		r.error = LastErrorText ("WinHttpConnect");
		WinHttpCloseHandle (session);
		return r;
	}

	HINTERNET request = WinHttpOpenRequest (connect, (LPCWSTR) L"GET", (LPCWSTR) object.c_str (), nullptr,
											WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
	if (request == nullptr) {
		r.error = LastErrorText ("WinHttpOpenRequest");
		WinHttpCloseHandle (connect);
		WinHttpCloseHandle (session);
		return r;
	}

	DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
	WinHttpSetOption (request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof (redirectPolicy));

	BOOL sent = WinHttpSendRequest (request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
	if (!sent || !WinHttpReceiveResponse (request, nullptr)) {
		r.error = LastErrorText ("Spojenie so serverom");
		WinHttpCloseHandle (request);
		WinHttpCloseHandle (connect);
		WinHttpCloseHandle (session);
		return r;
	}

	DWORD status = 0;
	DWORD size = sizeof (status);
	WinHttpQueryHeaders (request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
	r.status = (int) status;

	wchar_t ctype[512] = {};
	DWORD ctypeSize = sizeof (ctype);
	if (WinHttpQueryHeaders (request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, ctype, &ctypeSize, WINHTTP_NO_HEADER_INDEX))
		r.contentType = Util::WideToUtf8 (std::wstring (ctype));

	std::vector<char> buffer;
	while (true) {
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable (request, &avail)) {
			r.error = LastErrorText ("Citanie odpovede");
			break;
		}
		if (avail == 0)
			break;
		buffer.resize (avail);
		DWORD read = 0;
		if (!WinHttpReadData (request, buffer.data (), avail, &read)) {
			r.error = LastErrorText ("Citanie odpovede");
			break;
		}
		r.body.append (buffer.data (), read);
	}

	WinHttpCloseHandle (request);
	WinHttpCloseHandle (connect);
	WinHttpCloseHandle (session);

	r.ok = r.error.empty () && r.status >= 200 && r.status < 300;
	return r;
}


std::string Describe (const Response& r, size_t bodyPreview)
{
	std::string s;
	if (!r.error.empty ())
		s += r.error + "; ";
	s += "HTTP " + std::to_string (r.status);
	if (!r.contentType.empty ())
		s += ", " + r.contentType;
	s += ", " + std::to_string (r.body.size ()) + " B";
	bool looksText = r.contentType.find ("xml") != std::string::npos || r.contentType.find ("text") != std::string::npos ||
					 r.contentType.find ("json") != std::string::npos;
	if (looksText && !r.body.empty ()) {
		std::string preview = r.body.substr (0, bodyPreview);
		for (auto& c : preview)
			if (c == '\r' || c == '\n' || c == '\t')
				c = ' ';
		s += ": " + preview;
	}
	return s;
}

}	// namespace Http
