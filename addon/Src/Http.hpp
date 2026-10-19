#ifndef PILON_HTTP_HPP
#define PILON_HTTP_HPP

#include <string>

namespace Http {

	struct Response {
		bool			ok = false;			// transport succeeded and status 200..299
		int				status = 0;
		std::string		contentType;
		std::string		body;
		std::string		error;				// transport error description
	};

	// Blocking HTTPS/HTTP GET using WinHTTP (system proxy settings are respected).
	Response Get (const std::string& url, int timeoutSec = 120);

	// Short human readable summary of a response (for logs and the "Test sluzieb" button)
	std::string Describe (const Response& r, size_t bodyPreview = 300);
}

#endif
