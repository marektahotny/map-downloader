#ifndef PILON_JSON_HPP
#define PILON_JSON_HPP

// Small self-contained JSON DOM (parse + serialize) used for the
// JavaScript <-> C++ bridge and for the settings/config files.

#include <map>
#include <memory>
#include <string>
#include <vector>

class JsonValue {
public:
	enum Type { Null, Bool, Number, String, Array, Object };

	JsonValue () : type (Null), b (false), num (0.0) {}
	static JsonValue MakeBool (bool v)					{ JsonValue j; j.type = Bool; j.b = v; return j; }
	static JsonValue MakeNumber (double v)				{ JsonValue j; j.type = Number; j.num = v; return j; }
	static JsonValue MakeString (const std::string& v)	{ JsonValue j; j.type = String; j.str = v; return j; }
	static JsonValue MakeArray ()						{ JsonValue j; j.type = Array; return j; }
	static JsonValue MakeObject ()						{ JsonValue j; j.type = Object; return j; }

	Type	GetType () const	{ return type; }
	bool	IsNull () const		{ return type == Null; }
	bool	IsObject () const	{ return type == Object; }
	bool	IsArray () const	{ return type == Array; }
	bool	IsNumber () const	{ return type == Number; }
	bool	IsString () const	{ return type == String; }
	bool	IsBool () const		{ return type == Bool; }

	// Lenient getters with defaults
	double				AsNumber (double def = 0.0) const;
	bool				AsBool (bool def = false) const;
	std::string			AsString (const std::string& def = std::string ()) const;

	// Object access
	bool				Has (const std::string& key) const;
	const JsonValue&	operator[] (const std::string& key) const;		// returns Null value if missing
	JsonValue&			Set (const std::string& key, const JsonValue& v);
	JsonValue&			Set (const std::string& key, double v)				{ return Set (key, MakeNumber (v)); }
	JsonValue&			Set (const std::string& key, int v)					{ return Set (key, MakeNumber ((double) v)); }
	JsonValue&			Set (const std::string& key, bool v)				{ return Set (key, MakeBool (v)); }
	JsonValue&			Set (const std::string& key, const char* v)			{ return Set (key, MakeString (v)); }
	JsonValue&			Set (const std::string& key, const std::string& v)	{ return Set (key, MakeString (v)); }
	const std::vector<std::pair<std::string, JsonValue>>& Members () const	{ return obj; }

	// Array access
	size_t				Size () const		{ return type == Array ? arr.size () : 0; }
	const JsonValue&	At (size_t i) const;
	JsonValue&			Push (const JsonValue& v);

	std::string			Serialize () const;

	static bool			Parse (const std::string& text, JsonValue& out, std::string* error = nullptr);
	static std::string	Escape (const std::string& s);

private:
	Type type;
	bool b;
	double num;
	std::string str;
	std::vector<JsonValue> arr;
	std::vector<std::pair<std::string, JsonValue>> obj;

	void SerializeTo (std::string& out) const;
};

#endif
