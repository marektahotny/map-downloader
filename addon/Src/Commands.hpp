#ifndef PILON_COMMANDS_HPP
#define PILON_COMMANDS_HPP

#include <functional>
#include <string>

#include "Json.hpp"

namespace Commands {

	typedef std::function<void (const std::string& message)> ProgressFn;

	// Executes one request coming from the palette (JSON object with "cmd").
	// Runs on the Archicad main thread (called from the module command handler).
	JsonValue Execute (const JsonValue& request, const ProgressFn& progress);

	// Default + user overridden service configuration
	JsonValue LoadServices ();
	JsonValue DefaultServices ();
}

#endif
