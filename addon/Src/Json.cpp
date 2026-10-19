#include "Json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static const JsonValue& NullValue ()
{
	static const JsonValue nullValue;
	return nullValue;
}


double JsonValue::AsNumber (double def) const
{
	if (type == Number)
		return num;
	if (type == Bool)
		return b ? 1.0 : 0.0;
	if (type == String && !str.empty ()) {
		char* end = nullptr;
		double v = strtod (str.c_str (), &end);
		if (end != nullptr && end != str.c_str ())
			return v;
	}
	return def;
}


bool JsonValue::AsBool (bool def) const
{
	if (type == Bool)
		return b;
	if (type == Number)
		return num != 0.0;
	if (type == String)
		return str == "true" || str == "1";
	return def;
}


std::string JsonValue::AsString (const std::string& def) const
{
	if (type == String)
		return str;
	if (type == Number) {
		char buf[64];
		if (std::floor (num) == num && std::fabs (num) < 1e15)
			snprintf (buf, sizeof (buf), "%.0f", num);
		else
			snprintf (buf, sizeof (buf), "%.10g", num);
		return buf;
	}
	if (type == Bool)
		return b ? "true" : "false";
	return def;
}


bool JsonValue::Has (const std::string& key) const
{
	if (type != Object)
		return false;
	for (const auto& m : obj)
		if (m.first == key)
			return true;
	return false;
}


const JsonValue& JsonValue::operator[] (const std::string& key) const
{
	if (type != Object)
		return NullValue ();
	for (const auto& m : obj)
		if (m.first == key)
			return m.second;
	return NullValue ();
}


JsonValue& JsonValue::Set (const std::string& key, const JsonValue& v)
{
	if (type != Object) {
		type = Object;
		obj.clear ();
	}
	for (auto& m : obj) {
		if (m.first == key) {
			m.second = v;
			return m.second;
		}
	}
	obj.emplace_back (key, v);
	return obj.back ().second;
}


const JsonValue& JsonValue::At (size_t i) const
{
	if (type != Array || i >= arr.size ())
		return NullValue ();
	return arr[i];
}


JsonValue& JsonValue::Push (const JsonValue& v)
{
	if (type != Array) {
		type = Array;
		arr.clear ();
	}
	arr.push_back (v);
	return arr.back ();
}


std::string JsonValue::Escape (const std::string& s)
{
	std::string out;
	out.reserve (s.size () + 2);
	for (unsigned char c : s) {
		switch (c) {
			case '"':	out += "\\\""; break;
			case '\\':	out += "\\\\"; break;
			case '\n':	out += "\\n"; break;
			case '\r':	out += "\\r"; break;
			case '\t':	out += "\\t"; break;
			case '\b':	out += "\\b"; break;
			case '\f':	out += "\\f"; break;
			default:
				if (c < 0x20) {
					char buf[8];
					snprintf (buf, sizeof (buf), "\\u%04x", c);
					out += buf;
				} else {
					out += (char) c;
				}
		}
	}
	return out;
}


void JsonValue::SerializeTo (std::string& out) const
{
	switch (type) {
		case Null:		out += "null"; break;
		case Bool:		out += b ? "true" : "false"; break;
		case Number: {
			if (!std::isfinite (num)) {
				out += "null";
			} else {
				char buf[64];
				snprintf (buf, sizeof (buf), "%.10g", num);
				out += buf;
			}
			break;
		}
		case String:	out += '"'; out += Escape (str); out += '"'; break;
		case Array: {
			out += '[';
			for (size_t i = 0; i < arr.size (); i++) {
				if (i > 0)
					out += ',';
				arr[i].SerializeTo (out);
			}
			out += ']';
			break;
		}
		case Object: {
			out += '{';
			bool first = true;
			for (const auto& m : obj) {
				if (!first)
					out += ',';
				first = false;
				out += '"';
				out += Escape (m.first);
				out += "\":";
				m.second.SerializeTo (out);
			}
			out += '}';
			break;
		}
	}
}


std::string JsonValue::Serialize () const
{
	std::string out;
	SerializeTo (out);
	return out;
}


// --- Parser -------------------------------------------------------------------

namespace {

class Parser {
public:
	Parser (const std::string& t) : text (t), pos (0) {}

	bool ParseValue (JsonValue& out, int depth = 0)
	{
		if (depth > 200)
			return Fail ("too deep");
		SkipWs ();
		if (pos >= text.size ())
			return Fail ("unexpected end");
		char c = text[pos];
		if (c == '{')
			return ParseObject (out, depth);
		if (c == '[')
			return ParseArray (out, depth);
		if (c == '"') {
			std::string s;
			if (!ParseString (s))
				return false;
			out = JsonValue::MakeString (s);
			return true;
		}
		if (c == 't' && text.compare (pos, 4, "true") == 0)	{ pos += 4; out = JsonValue::MakeBool (true); return true; }
		if (c == 'f' && text.compare (pos, 5, "false") == 0)	{ pos += 5; out = JsonValue::MakeBool (false); return true; }
		if (c == 'n' && text.compare (pos, 4, "null") == 0)	{ pos += 4; out = JsonValue (); return true; }
		return ParseNumber (out);
	}

	bool AtEnd ()
	{
		SkipWs ();
		return pos >= text.size ();
	}

	std::string error;

private:
	const std::string& text;
	size_t pos;

	bool Fail (const char* msg)
	{
		char buf[128];
		snprintf (buf, sizeof (buf), "%s at %u", msg, (unsigned) pos);
		error = buf;
		return false;
	}

	void SkipWs ()
	{
		while (pos < text.size () && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r'))
			pos++;
	}

	static void AppendUtf8 (std::string& out, unsigned int cp)
	{
		if (cp < 0x80) {
			out += (char) cp;
		} else if (cp < 0x800) {
			out += (char) (0xC0 | (cp >> 6));
			out += (char) (0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			out += (char) (0xE0 | (cp >> 12));
			out += (char) (0x80 | ((cp >> 6) & 0x3F));
			out += (char) (0x80 | (cp & 0x3F));
		} else {
			out += (char) (0xF0 | (cp >> 18));
			out += (char) (0x80 | ((cp >> 12) & 0x3F));
			out += (char) (0x80 | ((cp >> 6) & 0x3F));
			out += (char) (0x80 | (cp & 0x3F));
		}
	}

	bool ParseHex4 (unsigned int& cp)
	{
		if (pos + 4 > text.size ())
			return Fail ("bad unicode escape");
		cp = 0;
		for (int i = 0; i < 4; i++) {
			char h = text[pos++];
			cp <<= 4;
			if (h >= '0' && h <= '9')		cp |= (unsigned) (h - '0');
			else if (h >= 'a' && h <= 'f')	cp |= (unsigned) (h - 'a' + 10);
			else if (h >= 'A' && h <= 'F')	cp |= (unsigned) (h - 'A' + 10);
			else return Fail ("bad hex digit");
		}
		return true;
	}

	bool ParseString (std::string& s)
	{
		pos++;	// opening quote
		while (pos < text.size ()) {
			char c = text[pos++];
			if (c == '"')
				return true;
			if (c == '\\') {
				if (pos >= text.size ())
					return Fail ("bad escape");
				char e = text[pos++];
				switch (e) {
					case '"':	s += '"'; break;
					case '\\':	s += '\\'; break;
					case '/':	s += '/'; break;
					case 'b':	s += '\b'; break;
					case 'f':	s += '\f'; break;
					case 'n':	s += '\n'; break;
					case 'r':	s += '\r'; break;
					case 't':	s += '\t'; break;
					case 'u': {
						unsigned int cp;
						if (!ParseHex4 (cp))
							return false;
						if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= text.size () && text[pos] == '\\' && text[pos + 1] == 'u') {
							pos += 2;
							unsigned int lo;
							if (!ParseHex4 (lo))
								return false;
							cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						}
						AppendUtf8 (s, cp);
						break;
					}
					default:
						return Fail ("bad escape");
				}
			} else {
				s += c;
			}
		}
		return Fail ("unterminated string");
	}

	bool ParseNumber (JsonValue& out)
	{
		const char* start = text.c_str () + pos;
		char* end = nullptr;
		double v = strtod (start, &end);
		if (end == start)
			return Fail ("bad value");
		pos += (size_t) (end - start);
		out = JsonValue::MakeNumber (v);
		return true;
	}

	bool ParseArray (JsonValue& out, int depth)
	{
		pos++;
		out = JsonValue::MakeArray ();
		SkipWs ();
		if (pos < text.size () && text[pos] == ']') {
			pos++;
			return true;
		}
		while (true) {
			JsonValue item;
			if (!ParseValue (item, depth + 1))
				return false;
			out.Push (item);
			SkipWs ();
			if (pos >= text.size ())
				return Fail ("unterminated array");
			if (text[pos] == ',') {
				pos++;
				continue;
			}
			if (text[pos] == ']') {
				pos++;
				return true;
			}
			return Fail ("expected , or ]");
		}
	}

	bool ParseObject (JsonValue& out, int depth)
	{
		pos++;
		out = JsonValue::MakeObject ();
		SkipWs ();
		if (pos < text.size () && text[pos] == '}') {
			pos++;
			return true;
		}
		while (true) {
			SkipWs ();
			if (pos >= text.size () || text[pos] != '"')
				return Fail ("expected key");
			std::string key;
			if (!ParseString (key))
				return false;
			SkipWs ();
			if (pos >= text.size () || text[pos] != ':')
				return Fail ("expected :");
			pos++;
			JsonValue val;
			if (!ParseValue (val, depth + 1))
				return false;
			out.Set (key, val);
			SkipWs ();
			if (pos >= text.size ())
				return Fail ("unterminated object");
			if (text[pos] == ',') {
				pos++;
				continue;
			}
			if (text[pos] == '}') {
				pos++;
				return true;
			}
			return Fail ("expected , or }");
		}
	}
};

}	// namespace


bool JsonValue::Parse (const std::string& text, JsonValue& out, std::string* error)
{
	Parser p (text);
	if (!p.ParseValue (out)) {
		if (error != nullptr)
			*error = p.error;
		return false;
	}
	if (!p.AtEnd ()) {
		if (error != nullptr)
			*error = "trailing characters";
		return false;
	}
	return true;
}
