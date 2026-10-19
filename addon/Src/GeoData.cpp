#include "GeoData.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace Geo {

// =============================================================================
// Geometry helpers
// =============================================================================

BBox BoundsOf (const Ring& ring)
{
	BBox b;
	if (ring.empty ())
		return b;
	b.minE = b.maxE = ring[0].e;
	b.minN = b.maxN = ring[0].n;
	for (const auto& p : ring) {
		b.minE = std::min (b.minE, p.e);
		b.maxE = std::max (b.maxE, p.e);
		b.minN = std::min (b.minN, p.n);
		b.maxN = std::max (b.maxN, p.n);
	}
	return b;
}


bool PointInPolygon (const Ring& poly, double e, double n)
{
	bool inside = false;
	size_t cnt = poly.size ();
	if (cnt < 3)
		return false;
	for (size_t i = 0, j = cnt - 1; i < cnt; j = i++) {
		const PointEN& a = poly[i];
		const PointEN& b = poly[j];
		if (((a.n > n) != (b.n > n)) && (e < (b.e - a.e) * (n - a.n) / (b.n - a.n) + a.e))
			inside = !inside;
	}
	return inside;
}


static double SegmentDistance (const PointEN& a, const PointEN& b, double e, double n)
{
	double dx = b.e - a.e, dy = b.n - a.n;
	double len2 = dx * dx + dy * dy;
	double t = len2 > 0.0 ? ((e - a.e) * dx + (n - a.n) * dy) / len2 : 0.0;
	t = std::max (0.0, std::min (1.0, t));
	double px = a.e + t * dx - e, py = a.n + t * dy - n;
	return std::sqrt (px * px + py * py);
}


double DistanceToPolygonEdges (const Ring& poly, double e, double n)
{
	double best = std::numeric_limits<double>::max ();
	for (size_t i = 0; i + 1 < poly.size (); i++)
		best = std::min (best, SegmentDistance (poly[i], poly[i + 1], e, n));
	if (poly.size () > 2 && (poly.front ().e != poly.back ().e || poly.front ().n != poly.back ().n))
		best = std::min (best, SegmentDistance (poly.back (), poly.front (), e, n));
	return best;
}


double SignedArea (const Ring& ring)
{
	double a = 0.0;
	for (size_t i = 0; i + 1 < ring.size (); i++)
		a += ring[i].e * ring[i + 1].n - ring[i + 1].e * ring[i].n;
	return a * 0.5;
}


PointEN Centroid (const Ring& ring)
{
	// area weighted centroid computed relative to first vertex (numerical stability)
	if (ring.empty ())
		return PointEN ();
	double e0 = ring[0].e, n0 = ring[0].n;
	double a = 0.0, ce = 0.0, cn = 0.0;
	for (size_t i = 0; i + 1 < ring.size (); i++) {
		double x1 = ring[i].e - e0, y1 = ring[i].n - n0;
		double x2 = ring[i + 1].e - e0, y2 = ring[i + 1].n - n0;
		double cr = x1 * y2 - x2 * y1;
		a += cr;
		ce += (x1 + x2) * cr;
		cn += (y1 + y2) * cr;
	}
	if (std::fabs (a) < 1e-12) {
		BBox b = BoundsOf (ring);
		return PointEN ((b.minE + b.maxE) * 0.5, (b.minN + b.maxN) * 0.5);
	}
	PointEN c (e0 + ce / (3.0 * a), n0 + cn / (3.0 * a));
	if (!PointInPolygon (ring, c.e, c.n)) {
		// concave shape: fall back to a point on a horizontal scan line through the bbox center
		BBox b = BoundsOf (ring);
		double y = (b.minN + b.maxN) * 0.5;
		std::vector<double> xs;
		for (size_t i = 0; i + 1 < ring.size (); i++) {
			const PointEN& p = ring[i];
			const PointEN& q = ring[i + 1];
			if ((p.n > y) != (q.n > y))
				xs.push_back (p.e + (y - p.n) * (q.e - p.e) / (q.n - p.n));
		}
		std::sort (xs.begin (), xs.end ());
		if (xs.size () >= 2)
			return PointEN ((xs[0] + xs[1]) * 0.5, y);
	}
	return c;
}


Ring DensifyRing (const Ring& ring, double maxSegment)
{
	if (ring.size () < 2 || maxSegment <= 0.0)
		return ring;
	Ring out;
	for (size_t i = 0; i + 1 < ring.size (); i++) {
		const PointEN& a = ring[i];
		const PointEN& b = ring[i + 1];
		out.push_back (a);
		double len = std::hypot (b.e - a.e, b.n - a.n);
		int parts = (int) std::ceil (len / maxSegment);
		for (int k = 1; k < parts; k++) {
			double t = (double) k / parts;
			out.push_back (PointEN (a.e + (b.e - a.e) * t, a.n + (b.n - a.n) * t));
		}
	}
	out.push_back (ring.back ());
	return out;
}


bool RingFullyInside (const Ring& inner, const Ring& outer)
{
	for (const auto& p : inner)
		if (!PointInPolygon (outer, p.e, p.n) && DistanceToPolygonEdges (outer, p.e, p.n) > 0.01)
			return false;
	return !inner.empty ();
}


PointEN NormalizeJtsk (double a, double b)
{
	// Expected in EPSG:5514: E in about (-950000, -100000), N in about (-1400000, -900000) and |E| < |N|.
	if (a > 0.0 && b > 0.0) {
		// classic JTSK (Y, X) positive values
		a = -a;
		b = -b;
	}
	if (std::fabs (a) > std::fabs (b))
		std::swap (a, b);
	return PointEN (a, b);
}


std::vector<Ring> OuterBoundary (const std::vector<Ring>& rings, double tolerance)
{
	// vertex snapping by grid key
	struct Key { long long x, y; bool operator== (const Key& o) const { return x == o.x && y == o.y; } };
	struct KeyHash { size_t operator() (const Key& k) const { return std::hash<long long> () (k.x * 1000003LL ^ k.y); } };
	auto keyOf = [&] (const PointEN& p) { return Key { (long long) std::floor (p.e / tolerance + 0.5), (long long) std::floor (p.n / tolerance + 0.5) }; };

	std::unordered_map<Key, PointEN, KeyHash> coords;
	// undirected edge counts
	struct EdgeKey { Key a, b; bool operator== (const EdgeKey& o) const { return a == o.a && b == o.b; } };
	struct EdgeHash { size_t operator() (const EdgeKey& k) const { KeyHash h; return h (k.a) * 31 + h (k.b); } };
	std::unordered_map<EdgeKey, int, EdgeHash> count;
	std::vector<std::pair<Key, Key>> directed;

	for (const Ring& r : rings) {
		for (size_t i = 0; i + 1 < r.size (); i++) {
			Key a = keyOf (r[i]), b = keyOf (r[i + 1]);
			if (a == b)
				continue;
			coords[a] = r[i];
			coords[b] = r[i + 1];
			EdgeKey ek = (a.x < b.x || (a.x == b.x && a.y < b.y)) ? EdgeKey { a, b } : EdgeKey { b, a };
			count[ek]++;
			directed.push_back (std::make_pair (a, b));
		}
	}

	// keep boundary edges (count == 1), adjacency from start vertex
	std::unordered_map<Key, std::vector<Key>, KeyHash> next;
	size_t remaining = 0;
	for (const auto& d : directed) {
		EdgeKey ek = (d.first.x < d.second.x || (d.first.x == d.second.x && d.first.y < d.second.y)) ? EdgeKey { d.first, d.second } : EdgeKey { d.second, d.first };
		if (count[ek] == 1) {
			next[d.first].push_back (d.second);
			remaining++;
		}
	}

	std::vector<Ring> out;
	while (remaining > 0) {
		// find a start
		Key start {};
		bool found = false;
		for (auto& kv : next) {
			if (!kv.second.empty ()) {
				start = kv.first;
				found = true;
				break;
			}
		}
		if (!found)
			break;
		Ring ring;
		Key cur = start;
		ring.push_back (coords[cur]);
		size_t guard = 0;
		while (guard++ < 1000000) {
			auto it = next.find (cur);
			if (it == next.end () || it->second.empty ())
				break;
			Key nx = it->second.back ();
			it->second.pop_back ();
			remaining--;
			ring.push_back (coords[nx]);
			cur = nx;
			if (cur == start)
				break;
		}
		if (ring.size () >= 4)
			out.push_back (ring);
	}
	// largest ring first
	std::sort (out.begin (), out.end (), [] (const Ring& a, const Ring& b) { return std::fabs (SignedArea (a)) > std::fabs (SignedArea (b)); });
	return out;
}


// =============================================================================
// URLs
// =============================================================================

static std::string Num (double v, int dec = 3)
{
	char buf[64];
	snprintf (buf, sizeof (buf), "%.*f", dec, v);
	return buf;
}


static std::string Enc (const std::string& s)
{
	static const char* hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : s) {
		if (isalnum (c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ',' || c == ':' || c == '(' || c == ')' || c == '/')
			out += (char) c;
		else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}


std::string AppendQuery (const std::string& baseUrl, const std::string& query)
{
	if (baseUrl.find ('?') == std::string::npos)
		return baseUrl + "?" + query;
	char last = baseUrl.back ();
	if (last == '?' || last == '&')
		return baseUrl + query;
	return baseUrl + "&" + query;
}


std::string WmsGetMapUrl (const std::string& baseUrl, const std::string& layers, const std::string& version,
						  const std::string& srs, const std::string& format, const BBox& bbox, int width, int height,
						  const std::string& styles)
{
	std::string v = version.empty () ? "1.1.1" : version;
	std::string q = "SERVICE=WMS&VERSION=" + v + "&REQUEST=GetMap&LAYERS=" + Enc (layers) + "&STYLES=" + Enc (styles);
	q += (v == "1.3.0" ? "&CRS=" : "&SRS=") + Enc (srs);
	// EPSG:5514 has East/North axis order, so the BBOX order is the same for 1.1.1 and 1.3.0
	q += "&BBOX=" + Num (bbox.minE) + "," + Num (bbox.minN) + "," + Num (bbox.maxE) + "," + Num (bbox.maxN);
	q += "&WIDTH=" + std::to_string (width) + "&HEIGHT=" + std::to_string (height);
	q += "&FORMAT=" + Enc (format.empty () ? "image/jpeg" : format);
	q += "&TRANSPARENT=FALSE&BGCOLOR=0xFFFFFF&EXCEPTIONS=" + Enc (v == "1.3.0" ? "XML" : "application/vnd.ogc.se_xml");
	return AppendQuery (baseUrl, q);
}


std::string WfsGetFeatureUrl (const std::string& baseUrl, const std::string& typeName, const std::string& srsName,
							  const BBox& bbox, int count)
{
	std::string q = "SERVICE=WFS&VERSION=2.0.0&REQUEST=GetFeature&TYPENAMES=" + Enc (typeName);
	q += "&SRSNAME=" + Enc (srsName);
	q += "&BBOX=" + Num (bbox.minE) + "," + Num (bbox.minN) + "," + Num (bbox.maxE) + "," + Num (bbox.maxN) + "," + Enc (srsName);
	if (count > 0)
		q += "&COUNT=" + std::to_string (count);
	return AppendQuery (baseUrl, q);
}


std::string WcsGetCoverageUrl (const std::string& baseUrl, const std::string& coverageId, const std::string& format,
							   const BBox& bbox, const std::string& crsAxisE, const std::string& crsAxisN, const std::string& subsettingCrs)
{
	std::string q = "SERVICE=WCS&VERSION=2.0.1&REQUEST=GetCoverage&COVERAGEID=" + Enc (coverageId);
	q += "&FORMAT=" + Enc (format.empty () ? "image/tiff" : format);
	q += "&SUBSET=" + Enc (crsAxisE.empty () ? "E" : crsAxisE) + "(" + Num (bbox.minE) + "," + Num (bbox.maxE) + ")";
	q += "&SUBSET=" + Enc (crsAxisN.empty () ? "N" : crsAxisN) + "(" + Num (bbox.minN) + "," + Num (bbox.maxN) + ")";
	if (!subsettingCrs.empty ())
		q += "&SUBSETTINGCRS=" + Enc (subsettingCrs) + "&OUTPUTCRS=" + Enc (subsettingCrs);
	return AppendQuery (baseUrl, q);
}


std::string ArcGisExportImageUrl (const std::string& baseUrl, const BBox& bbox, int width, int height, int sr)
{
	std::string url = baseUrl;
	if (url.find ("/exportImage") == std::string::npos) {
		while (!url.empty () && url.back () == '/')
			url.pop_back ();
		url += "/exportImage";
	}
	std::string q = "bbox=" + Num (bbox.minE) + "," + Num (bbox.minN) + "," + Num (bbox.maxE) + "," + Num (bbox.maxN);
	q += "&bboxSR=" + std::to_string (sr) + "&imageSR=" + std::to_string (sr);
	q += "&size=" + std::to_string (width) + "," + std::to_string (height);
	q += "&format=tiff&pixelType=F32&noData=-9999&compression=None&interpolation=RSP_BilinearInterpolation&f=image";
	return AppendQuery (url, q);
}


// =============================================================================
// XML helpers (tiny, tolerant scanning - no full XML parser needed)
// =============================================================================

// Finds next start tag whose local name (after optional prefix) equals localName.
// Returns position of '<' or npos. tagEnd receives position after '>'.
static size_t FindStartTag (const std::string& xml, const std::string& localName, size_t from, size_t limit, size_t* tagEnd = nullptr, bool* selfClosing = nullptr)
{
	size_t pos = from;
	while (true) {
		pos = xml.find ('<', pos);
		if (pos == std::string::npos || pos >= limit)
			return std::string::npos;
		size_t nameStart = pos + 1;
		if (nameStart < xml.size () && (xml[nameStart] == '/' || xml[nameStart] == '?' || xml[nameStart] == '!')) {
			pos = nameStart;
			continue;
		}
		size_t nameEnd = nameStart;
		while (nameEnd < xml.size () && !isspace ((unsigned char) xml[nameEnd]) && xml[nameEnd] != '>' && xml[nameEnd] != '/')
			nameEnd++;
		std::string name = xml.substr (nameStart, nameEnd - nameStart);
		size_t colon = name.find (':');
		std::string local = colon == std::string::npos ? name : name.substr (colon + 1);
		if (local == localName) {
			size_t gt = xml.find ('>', nameEnd);
			if (gt == std::string::npos)
				return std::string::npos;
			if (tagEnd != nullptr)
				*tagEnd = gt + 1;
			if (selfClosing != nullptr)
				*selfClosing = (gt > 0 && xml[gt - 1] == '/');
			return pos;
		}
		pos = nameEnd;
	}
}


// Finds the matching end tag for a local name (no nesting support for same name)
static size_t FindEndTag (const std::string& xml, const std::string& localName, size_t from, size_t* after = nullptr)
{
	size_t pos = from;
	while (true) {
		pos = xml.find ("</", pos);
		if (pos == std::string::npos)
			return std::string::npos;
		size_t nameStart = pos + 2;
		size_t gt = xml.find ('>', nameStart);
		if (gt == std::string::npos)
			return std::string::npos;
		std::string name = xml.substr (nameStart, gt - nameStart);
		while (!name.empty () && isspace ((unsigned char) name.back ()))
			name.pop_back ();
		size_t colon = name.find (':');
		std::string local = colon == std::string::npos ? name : name.substr (colon + 1);
		if (local == localName) {
			if (after != nullptr)
				*after = gt + 1;
			return pos;
		}
		pos = gt;
	}
}


static std::string AttrValue (const std::string& xml, size_t tagStart, size_t tagEnd, const std::string& attrLocalName)
{
	std::string tag = xml.substr (tagStart, tagEnd - tagStart);
	size_t pos = 0;
	while (true) {
		pos = tag.find (attrLocalName + "=", pos);
		if (pos == std::string::npos)
			return std::string ();
		char before = pos > 0 ? tag[pos - 1] : ' ';
		if (!(isspace ((unsigned char) before) || before == ':')) {
			pos++;
			continue;
		}
		size_t q = pos + attrLocalName.size () + 1;
		if (q >= tag.size ())
			return std::string ();
		char quote = tag[q];
		if (quote != '"' && quote != '\'')
			return std::string ();
		size_t end = tag.find (quote, q + 1);
		if (end == std::string::npos)
			return std::string ();
		return tag.substr (q + 1, end - q - 1);
	}
}


static std::string DecodeEntities (const std::string& s)
{
	if (s.find ('&') == std::string::npos)
		return s;
	std::string out;
	for (size_t i = 0; i < s.size (); i++) {
		if (s[i] == '&') {
			size_t semi = s.find (';', i);
			if (semi != std::string::npos && semi - i < 10) {
				std::string ent = s.substr (i + 1, semi - i - 1);
				if (ent == "amp")		{ out += '&'; i = semi; continue; }
				if (ent == "lt")		{ out += '<'; i = semi; continue; }
				if (ent == "gt")		{ out += '>'; i = semi; continue; }
				if (ent == "quot")		{ out += '"'; i = semi; continue; }
				if (ent == "apos")		{ out += '\''; i = semi; continue; }
			}
		}
		out += s[i];
	}
	return out;
}


static std::string Trimmed (const std::string& s)
{
	size_t b = s.find_first_not_of (" \t\r\n");
	if (b == std::string::npos)
		return std::string ();
	size_t e = s.find_last_not_of (" \t\r\n");
	return s.substr (b, e - b + 1);
}


static std::vector<double> ParseNumbers (const std::string& s)
{
	std::vector<double> v;
	const char* p = s.c_str ();
	const char* end = p + s.size ();
	while (p < end) {
		while (p < end && (isspace ((unsigned char) *p) || *p == ','))
			p++;
		if (p >= end)
			break;
		char* next = nullptr;
		double d = strtod (p, &next);
		if (next == p) {
			p++;
			continue;
		}
		v.push_back (d);
		p = next;
	}
	return v;
}


static Ring RingFromNumbers (const std::vector<double>& nums, int dim)
{
	Ring r;
	if (dim < 2)
		dim = 2;
	for (size_t i = 0; i + 1 < nums.size (); i += (size_t) dim)
		r.push_back (NormalizeJtsk (nums[i], nums[i + 1]));
	if (r.size () >= 2 && (r.front ().e != r.back ().e || r.front ().n != r.back ().n))
		r.push_back (r.front ());
	return r;
}


// =============================================================================
// GML features
// =============================================================================

bool ParseGmlFeatures (const std::string& gml, const std::string& labelTags, std::vector<Feature>& out, std::string& error)
{
	std::string ex = ExtractOgcException (gml);
	if (!ex.empty ()) {
		error = ex;
		return false;
	}

	std::vector<std::string> labels;
	{
		size_t start = 0;
		while (start <= labelTags.size ()) {
			size_t comma = labelTags.find (',', start);
			std::string t = Trimmed (labelTags.substr (start, comma == std::string::npos ? std::string::npos : comma - start));
			if (!t.empty ())
				labels.push_back (t);
			if (comma == std::string::npos)
				break;
			start = comma + 1;
		}
	}

	// Feature containers: WFS 2.0 <wfs:member>, GML 2/3 <gml:featureMember>, <gml:featureMembers> (children)
	const char* containers[] = { "member", "featureMember" };
	bool anyContainer = false;
	for (const char* containerName : containers) {
		size_t pos = 0;
		while (true) {
			size_t tagEnd = 0;
			size_t start = FindStartTag (gml, containerName, pos, gml.size (), &tagEnd);
			if (start == std::string::npos)
				break;
			size_t after = 0;
			size_t end = FindEndTag (gml, containerName, tagEnd, &after);
			if (end == std::string::npos)
				break;
			anyContainer = true;

			Feature f;
			std::string body = gml.substr (tagEnd, end - tagEnd);

			// gml:id of the first child element
			size_t firstLt = body.find ('<');
			if (firstLt != std::string::npos) {
				size_t gt = body.find ('>', firstLt);
				if (gt != std::string::npos)
					f.id = AttrValue (body, firstLt, gt, "id");
			}

			for (const auto& lt : labels) {
				size_t le = 0;
				bool selfClosing = false;
				size_t ls = FindStartTag (body, lt, 0, body.size (), &le, &selfClosing);
				if (ls != std::string::npos && !selfClosing) {
					size_t lend = FindEndTag (body, lt, le);
					if (lend != std::string::npos) {
						std::string txt = Trimmed (DecodeEntities (body.substr (le, lend - le)));
						if (!txt.empty () && txt.find ('<') == std::string::npos) {
							f.label = txt;
							break;
						}
					}
				}
			}

			// official area (INSPIRE CP areaValue)
			{
				size_t ae = 0;
				bool sc = false;
				size_t as = FindStartTag (body, "areaValue", 0, body.size (), &ae, &sc);
				if (as != std::string::npos && !sc) {
					size_t aend = FindEndTag (body, "areaValue", ae);
					if (aend != std::string::npos)
						f.officialArea = atof (Trimmed (body.substr (ae, aend - ae)).c_str ());
				}
			}

			// reference point (INSPIRE CP referencePoint/gml:Point/gml:pos)
			{
				size_t re = 0;
				size_t rs = FindStartTag (body, "referencePoint", 0, body.size (), &re);
				if (rs != std::string::npos) {
					size_t pe = 0;
					size_t ps = FindStartTag (body, "pos", re, body.size (), &pe);
					if (ps != std::string::npos) {
						size_t pend = FindEndTag (body, "pos", pe);
						if (pend != std::string::npos) {
							std::vector<double> nums = ParseNumbers (body.substr (pe, pend - pe));
							if (nums.size () >= 2) {
								f.refPoint = NormalizeJtsk (nums[0], nums[1]);
								f.hasRefPoint = true;
							}
						}
					}
				}
			}

			// all posList / coordinates rings
			size_t p = 0;
			while (true) {
				size_t te = 0;
				size_t ts = FindStartTag (body, "posList", p, body.size (), &te);
				if (ts == std::string::npos)
					break;
				std::string dimStr = AttrValue (body, ts, te, "srsDimension");
				int dim = dimStr.empty () ? 2 : atoi (dimStr.c_str ());
				size_t after2 = 0;
				size_t tend = FindEndTag (body, "posList", te, &after2);
				if (tend == std::string::npos)
					break;
				Ring r = RingFromNumbers (ParseNumbers (body.substr (te, tend - te)), dim);
				if (r.size () >= 2)
					f.rings.push_back (r);
				p = after2;
			}
			// GML 2 <gml:coordinates>x,y x,y</gml:coordinates>
			p = 0;
			while (true) {
				size_t te = 0;
				size_t ts = FindStartTag (body, "coordinates", p, body.size (), &te);
				if (ts == std::string::npos)
					break;
				size_t after2 = 0;
				size_t tend = FindEndTag (body, "coordinates", te, &after2);
				if (tend == std::string::npos)
					break;
				Ring r = RingFromNumbers (ParseNumbers (body.substr (te, tend - te)), 2);
				if (r.size () >= 2)
					f.rings.push_back (r);
				p = after2;
			}

			if (!f.rings.empty ())
				out.push_back (f);
			pos = after;
		}
	}

	if (!anyContainer) {
		size_t look = gml.find ("numberReturned=\"0\"");
		if (look != std::string::npos || gml.find ("FeatureCollection") != std::string::npos)
			return true;	// empty collection
		error = u8"Odpoveď neobsahuje GML prvky: " + gml.substr (0, 200);
		return false;
	}
	return true;
}


std::vector<std::string> ExtractCapabilityNames (const std::string& xml, size_t maxCount)
{
	std::vector<std::string> names;
	const char* tags[] = { "CoverageId", "Name" };
	for (const char* tag : tags) {
		size_t pos = 0;
		while (names.size () < maxCount) {
			size_t te = 0;
			bool sc = false;
			size_t ts = FindStartTag (xml, tag, pos, xml.size (), &te, &sc);
			if (ts == std::string::npos)
				break;
			if (sc) {
				pos = te;
				continue;
			}
			size_t after = 0;
			size_t tend = FindEndTag (xml, tag, te, &after);
			if (tend == std::string::npos)
				break;
			std::string v = Trimmed (DecodeEntities (xml.substr (te, tend - te)));
			if (!v.empty () && v.size () < 200 && v.find ('<') == std::string::npos &&
				std::find (names.begin (), names.end (), v) == names.end ())
				names.push_back (v);
			pos = after;
		}
	}
	return names;
}


std::string ExtractOgcException (const std::string& body)
{
	const char* tags[] = { "ExceptionText", "ServiceException" };
	for (const char* tag : tags) {
		size_t te = 0;
		bool sc = false;
		size_t ts = FindStartTag (body, tag, 0, std::min (body.size (), (size_t) 20000), &te, &sc);
		if (ts != std::string::npos && !sc) {
			size_t tend = FindEndTag (body, tag, te);
			if (tend != std::string::npos)
				return Trimmed (DecodeEntities (body.substr (te, tend - te)));
		}
	}
	size_t first = body.find_first_not_of (" \t\r\n");
	if (first != std::string::npos && body[first] == '{' && body.size () < 5000 &&
		body.find ("\"error\"") != std::string::npos)
		return Trimmed (body);	// ArcGIS JSON error
	return std::string ();
}


// =============================================================================
// Terrain parsing
// =============================================================================

void FinalizeTerrain (Terrain& t)
{
	if (t.points.empty ())
		return;
	t.bounds.minE = t.bounds.maxE = t.points[0].e;
	t.bounds.minN = t.bounds.maxN = t.points[0].n;
	for (const auto& p : t.points) {
		t.bounds.minE = std::min (t.bounds.minE, p.e);
		t.bounds.maxE = std::max (t.bounds.maxE, p.e);
		t.bounds.minN = std::min (t.bounds.minN, p.n);
		t.bounds.maxN = std::max (t.bounds.maxN, p.n);
	}
	if (!t.regularGrid) {
		double area = std::max (1e-6, t.bounds.Width () * t.bounds.Height ());
		t.spacing = std::max (0.05, std::sqrt (area / (double) t.points.size ()));
	}
}


void ClipTerrain (Terrain& t, const Ring& polygon, double margin)
{
	if (polygon.size () < 3)
		return;
	std::vector<PointENZ> kept;
	kept.reserve (t.points.size ());
	for (const auto& p : t.points) {
		if (PointInPolygon (polygon, p.e, p.n) || (margin > 0.0 && DistanceToPolygonEdges (polygon, p.e, p.n) <= margin))
			kept.push_back (p);
	}
	t.points.swap (kept);
	bool reg = t.regularGrid;
	double sp = t.spacing;
	FinalizeTerrain (t);
	t.regularGrid = reg;
	if (reg)
		t.spacing = sp;
}


namespace {

struct TiffReader {
	const unsigned char* d;
	size_t size;
	bool le;

	TiffReader (const std::string& data) : d ((const unsigned char*) data.data ()), size (data.size ()), le (true) {}

	bool Ok (size_t off, size_t len) const { return off + len <= size; }
	unsigned U16 (size_t off) const { return le ? (unsigned) (d[off] | (d[off + 1] << 8)) : (unsigned) ((d[off] << 8) | d[off + 1]); }
	unsigned U32 (size_t off) const
	{
		return le ? ((unsigned) d[off] | ((unsigned) d[off + 1] << 8) | ((unsigned) d[off + 2] << 16) | ((unsigned) d[off + 3] << 24))
				  : (((unsigned) d[off] << 24) | ((unsigned) d[off + 1] << 16) | ((unsigned) d[off + 2] << 8) | (unsigned) d[off + 3]);
	}
	double F64 (size_t off) const
	{
		unsigned char b[8];
		for (int i = 0; i < 8; i++)
			b[i] = le ? d[off + i] : d[off + 7 - i];
		double v;
		memcpy (&v, b, 8);
		return v;
	}
	float F32 (size_t off) const
	{
		unsigned u = U32 (off);
		float f;
		memcpy (&f, &u, 4);
		return f;
	}
};

struct TiffTag {
	unsigned type = 0;
	unsigned count = 0;
	size_t valueOffset = 0;		// offset of the value data in the file
};

static size_t TypeSize (unsigned type)
{
	switch (type) {
		case 1: case 2: case 6: case 7:	return 1;
		case 3: case 8:					return 2;
		case 4: case 9: case 11:		return 4;
		case 5: case 10: case 12:		return 8;
		default:						return 1;
	}
}

static std::vector<double> TagValues (const TiffReader& r, const TiffTag& t)
{
	std::vector<double> v;
	size_t ts = TypeSize (t.type);
	for (unsigned i = 0; i < t.count; i++) {
		size_t off = t.valueOffset + i * ts;
		if (!r.Ok (off, ts))
			break;
		switch (t.type) {
			case 1: case 7:	v.push_back (r.d[off]); break;
			case 3:			v.push_back (r.U16 (off)); break;
			case 8:			v.push_back ((short) r.U16 (off)); break;
			case 4:			v.push_back (r.U32 (off)); break;
			case 9:			v.push_back ((int) r.U32 (off)); break;
			case 11:		v.push_back (r.F32 (off)); break;
			case 12:		v.push_back (r.F64 (off)); break;
			case 5:			v.push_back (r.U32 (off + 4) != 0 ? (double) r.U32 (off) / r.U32 (off + 4) : 0.0); break;
			default:		v.push_back (r.d[off]); break;
		}
	}
	return v;
}

}	// namespace


bool ParseGeoTiff (const std::string& data, const BBox& fallbackBox, Terrain& out, std::string& error)
{
	TiffReader r (data);
	if (data.size () < 8) {
		error = u8"TIFF je príliš krátky";
		return false;
	}
	if (data[0] == 'I' && data[1] == 'I')
		r.le = true;
	else if (data[0] == 'M' && data[1] == 'M')
		r.le = false;
	else {
		error = u8"Nie je to TIFF súbor";
		return false;
	}
	if (r.U16 (2) != 42) {
		error = u8"Nepodporovaný TIFF (BigTIFF?)";
		return false;
	}
	size_t ifd = r.U32 (4);
	if (!r.Ok (ifd, 2)) {
		error = u8"Poškodený TIFF";
		return false;
	}
	unsigned nTags = r.U16 (ifd);
	std::unordered_map<unsigned, TiffTag> tags;
	for (unsigned i = 0; i < nTags; i++) {
		size_t e = ifd + 2 + i * 12;
		if (!r.Ok (e, 12))
			break;
		TiffTag t;
		unsigned id = r.U16 (e);
		t.type = r.U16 (e + 2);
		t.count = r.U32 (e + 4);
		size_t total = TypeSize (t.type) * t.count;
		t.valueOffset = total <= 4 ? e + 8 : r.U32 (e + 8);
		tags[id] = t;
	}

	auto get1 = [&] (unsigned id, double def) -> double {
		auto it = tags.find (id);
		if (it == tags.end ())
			return def;
		std::vector<double> v = TagValues (r, it->second);
		return v.empty () ? def : v[0];
	};

	int width = (int) get1 (256, 0);
	int height = (int) get1 (257, 0);
	int bits = (int) get1 (258, 32);
	int compression = (int) get1 (259, 1);
	int sampleFormat = (int) get1 (339, bits == 32 || bits == 64 ? 3 : 1);
	int spp = (int) get1 (277, 1);
	int planar = (int) get1 (284, 1);

	if (width <= 0 || height <= 0) {
		error = "TIFF bez rozmerov";
		return false;
	}
	if (compression != 1) {
		error = u8"Komprimovaný TIFF (kompresia " + std::to_string (compression) + u8") nie je podporovaný – nastavte v službe nekomprimovaný výstup alebo formát ArcGrid";
		return false;
	}
	if (spp != 1 && planar != 1) {
		error = u8"Viacpásmový TIFF nie je podporovaný";
		return false;
	}
	int bytesPerSample = bits / 8;
	if (bytesPerSample != 1 && bytesPerSample != 2 && bytesPerSample != 4 && bytesPerSample != 8) {
		error = u8"Nepodporovaná bitová hĺbka TIFF";
		return false;
	}
	size_t pixelStride = (size_t) bytesPerSample * (size_t) spp;

	auto readSample = [&] (size_t off) -> double {
		if (sampleFormat == 3) {
			if (bytesPerSample == 4)
				return r.F32 (off);
			if (bytesPerSample == 8)
				return r.F64 (off);
		}
		if (sampleFormat == 2) {
			if (bytesPerSample == 2)	return (short) r.U16 (off);
			if (bytesPerSample == 4)	return (int) r.U32 (off);
			if (bytesPerSample == 1)	return (signed char) r.d[off];
		}
		if (bytesPerSample == 2)	return r.U16 (off);
		if (bytesPerSample == 4)	return r.U32 (off);
		return r.d[off];
	};

	// georeference
	double originE = fallbackBox.minE, originN = fallbackBox.maxN;
	double px = fallbackBox.IsValid () ? fallbackBox.Width () / width : 1.0;
	double py = fallbackBox.IsValid () ? fallbackBox.Height () / height : 1.0;
	bool geo = false;
	auto scaleIt = tags.find (33550);
	auto tieIt = tags.find (33922);
	if (scaleIt != tags.end () && tieIt != tags.end ()) {
		std::vector<double> sc = TagValues (r, scaleIt->second);
		std::vector<double> tp = TagValues (r, tieIt->second);
		if (sc.size () >= 2 && tp.size () >= 6 && sc[0] > 0.0 && sc[1] > 0.0) {
			px = sc[0];
			py = sc[1];
			PointEN o = NormalizeJtsk (tp[3], tp[4]);
			// tie point refers to raster (I,J)
			originE = o.e - tp[0] * px;
			originN = o.n + tp[1] * py;
			geo = true;
		}
	}
	if (!geo && !fallbackBox.IsValid ()) {
		error = u8"TIFF nemá georeferenciu";
		return false;
	}

	double nodata = -9999.0;
	bool hasNodata = false;
	auto ndIt = tags.find (42113);
	if (ndIt != tags.end ()) {
		const TiffTag& t = ndIt->second;
		if (r.Ok (t.valueOffset, t.count)) {
			std::string s ((const char*) r.d + t.valueOffset, t.count);
			nodata = atof (s.c_str ());
			hasNodata = true;
		}
	}

	auto addPixel = [&] (int col, int row, double z) {
		if (!std::isfinite (z))
			return;
		if (hasNodata && std::fabs (z - nodata) < 1e-6)
			return;
		if (z <= -9000.0 || z > 9000.0)		// typical nodata values (-9999, -3.4e38)
			return;
		PointENZ p;
		p.e = originE + (col + 0.5) * px;
		p.n = originN - (row + 0.5) * py;
		p.z = z;
		out.points.push_back (p);
	};

	out.points.clear ();
	out.points.reserve ((size_t) width * height);

	if (tags.count (324) != 0) {
		// tiled
		int tw = (int) get1 (322, 0), th = (int) get1 (323, 0);
		if (tw <= 0 || th <= 0) {
			error = u8"TIFF dlaždice bez rozmerov";
			return false;
		}
		std::vector<double> offs = TagValues (r, tags[324]);
		int tilesAcross = (width + tw - 1) / tw;
		int tilesDown = (height + th - 1) / th;
		for (int ty = 0; ty < tilesDown; ty++) {
			for (int tx = 0; tx < tilesAcross; tx++) {
				size_t idx = (size_t) ty * tilesAcross + tx;
				if (idx >= offs.size ())
					continue;
				size_t base = (size_t) offs[idx];
				for (int y = 0; y < th; y++) {
					int row = ty * th + y;
					if (row >= height)
						break;
					for (int x = 0; x < tw; x++) {
						int col = tx * tw + x;
						if (col >= width)
							continue;
						size_t off = base + ((size_t) y * tw + x) * pixelStride;
						if (!r.Ok (off, bytesPerSample))
							continue;
						addPixel (col, row, readSample (off));
					}
				}
			}
		}
	} else {
		std::vector<double> offs = tags.count (273) ? TagValues (r, tags[273]) : std::vector<double> ();
		int rowsPerStrip = (int) get1 (278, height);
		if (rowsPerStrip <= 0)
			rowsPerStrip = height;
		if (offs.empty ()) {
			error = u8"TIFF bez dát";
			return false;
		}
		for (int row = 0; row < height; row++) {
			size_t strip = (size_t) (row / rowsPerStrip);
			if (strip >= offs.size ())
				break;
			size_t rowInStrip = (size_t) (row % rowsPerStrip);
			size_t base = (size_t) offs[strip] + rowInStrip * (size_t) width * pixelStride;
			for (int col = 0; col < width; col++) {
				size_t off = base + (size_t) col * pixelStride;
				if (!r.Ok (off, bytesPerSample))
					break;
				addPixel (col, row, readSample (off));
			}
		}
	}

	out.regularGrid = true;
	out.spacing = std::sqrt (px * py);
	FinalizeTerrain (out);
	out.regularGrid = true;
	out.spacing = std::sqrt (px * py);
	if (out.points.empty ()) {
		error = u8"Raster neobsahuje platné výšky (iba NoData)";
		return false;
	}
	return true;
}


bool ParseArcGrid (const std::string& text, Terrain& out, std::string& error)
{
	const char* p = text.c_str ();
	const char* end = p + text.size ();
	int ncols = 0, nrows = 0;
	double xll = 0, yll = 0, cell = 0, dx = 0, dy = 0, nodata = -9999;
	bool xCenter = false, yCenter = false, hasNodata = false;

	// header
	for (int i = 0; i < 8 && p < end; i++) {
		while (p < end && isspace ((unsigned char) *p))
			p++;
		if (p >= end || !isalpha ((unsigned char) *p))
			break;
		const char* ks = p;
		while (p < end && !isspace ((unsigned char) *p))
			p++;
		std::string key (ks, p);
		for (auto& c : key)
			c = (char) tolower ((unsigned char) c);
		char* next = nullptr;
		double v = strtod (p, &next);
		p = next;
		if (key == "ncols")				ncols = (int) v;
		else if (key == "nrows")		nrows = (int) v;
		else if (key == "xllcorner")	xll = v;
		else if (key == "xllcenter")	{ xll = v; xCenter = true; }
		else if (key == "yllcorner")	yll = v;
		else if (key == "yllcenter")	{ yll = v; yCenter = true; }
		else if (key == "cellsize")		cell = v;
		else if (key == "dx")			dx = v;
		else if (key == "dy")			dy = v;
		else if (key == "nodata_value")	{ nodata = v; hasNodata = true; }
	}
	if (dx <= 0)
		dx = cell;
	if (dy <= 0)
		dy = cell;
	if (ncols <= 0 || nrows <= 0 || dx <= 0 || dy <= 0) {
		error = u8"Neplatná hlavička ASCII gridu";
		return false;
	}
	double x0 = xCenter ? xll : xll + dx * 0.5;			// center of first column
	double yBottom = yCenter ? yll : yll + dy * 0.5;	// center of last row
	double yTop = yBottom + (nrows - 1) * dy;

	// ArcGrid stores positive JTSK? -> normalise origin later per point
	out.points.clear ();
	out.points.reserve ((size_t) ncols * nrows);
	for (int row = 0; row < nrows; row++) {
		for (int col = 0; col < ncols; col++) {
			char* next = nullptr;
			double z = strtod (p, &next);
			if (next == p) {
				error = u8"ASCII grid má menej hodnôt, než udáva hlavička";
				return false;
			}
			p = next;
			if (hasNodata && std::fabs (z - nodata) < 1e-6)
				continue;
			PointEN en = NormalizeJtsk (x0 + col * dx, yTop - row * dy);
			PointENZ pt;
			pt.e = en.e;
			pt.n = en.n;
			pt.z = z;
			out.points.push_back (pt);
		}
	}
	out.regularGrid = true;
	out.spacing = std::sqrt (dx * dy);
	FinalizeTerrain (out);
	out.regularGrid = true;
	out.spacing = std::sqrt (dx * dy);
	return !out.points.empty ();
}


bool ParseXyz (const std::string& text, Terrain& out, std::string& error)
{
	out.points.clear ();
	const char* p = text.c_str ();
	const char* end = p + text.size ();
	size_t lineNo = 0;
	size_t bad = 0;
	while (p < end) {
		const char* lineEnd = (const char*) memchr (p, '\n', (size_t) (end - p));
		if (lineEnd == nullptr)
			lineEnd = end;
		lineNo++;
		double v[3];
		int n = 0;
		const char* q = p;
		while (q < lineEnd && n < 3) {
			while (q < lineEnd && (isspace ((unsigned char) *q) || *q == ',' || *q == ';'))
				q++;
			if (q >= lineEnd)
				break;
			char* next = nullptr;
			double d = strtod (q, &next);
			if (next == q || next > lineEnd)
				break;
			v[n++] = d;
			q = next;
		}
		if (n == 3) {
			PointEN en = NormalizeJtsk (v[0], v[1]);
			PointENZ pt;
			pt.e = en.e;
			pt.n = en.n;
			pt.z = v[2];
			out.points.push_back (pt);
		} else if (lineNo > 1) {
			bad++;
		}
		p = lineEnd + 1;
	}
	if (out.points.empty ()) {
		error = u8"Súbor neobsahuje body X Y Z";
		return false;
	}
	// regular grid detection: all coordinates multiples of a common step
	out.regularGrid = false;
	FinalizeTerrain (out);
	if (out.points.size () > 4) {
		double minStep = std::numeric_limits<double>::max ();
		for (size_t i = 1; i < std::min (out.points.size (), (size_t) 5000); i++) {
			double d = std::fabs (out.points[i].e - out.points[i - 1].e);
			if (d > 1e-6)
				minStep = std::min (minStep, d);
		}
		if (minStep < std::numeric_limits<double>::max () && std::fabs (minStep - out.spacing) / out.spacing < 0.3) {
			out.regularGrid = true;
			out.spacing = minStep;
		}
	}
	return true;
}


bool ParseTerrainAuto (const std::string& data, const BBox& fallbackBox, Terrain& out, std::string& error)
{
	if (data.size () >= 4 && ((data[0] == 'I' && data[1] == 'I') || (data[0] == 'M' && data[1] == 'M')))
		return ParseGeoTiff (data, fallbackBox, out, error);
	std::string ex = ExtractOgcException (data);
	if (!ex.empty ()) {
		error = ex;
		return false;
	}
	size_t firstNonWs = data.find_first_not_of (" \t\r\n");
	if (firstNonWs != std::string::npos) {
		std::string head = data.substr (firstNonWs, 5);
		for (auto& c : head)
			c = (char) tolower ((unsigned char) c);
		if (head == "ncols" || head == "nrows")
			return ParseArcGrid (data, out, error);
		if (head[0] == '<') {
			error = u8"Služba vrátila XML namiesto výškových dát: " + data.substr (firstNonWs, 300);
			return false;
		}
	}
	return ParseXyz (data, out, error);
}


// =============================================================================
// Height index
// =============================================================================

void HeightIndex::Build (const Terrain& t)
{
	terrain = &t;
	cell = std::max (0.1, t.spacing * 2.0);
	originE = t.bounds.minE;
	originN = t.bounds.minN;
	nx = std::max (1, (int) std::ceil (t.bounds.Width () / cell) + 1);
	ny = std::max (1, (int) std::ceil (t.bounds.Height () / cell) + 1);
	buckets.assign ((size_t) nx * ny, std::vector<int> ());
	for (size_t i = 0; i < t.points.size (); i++) {
		int cx = std::min (nx - 1, std::max (0, (int) ((t.points[i].e - originE) / cell)));
		int cy = std::min (ny - 1, std::max (0, (int) ((t.points[i].n - originN) / cell)));
		buckets[(size_t) cy * nx + cx].push_back ((int) i);
	}
}


bool HeightIndex::HeightAt (double e, double n, double& z) const
{
	if (terrain == nullptr || terrain->points.empty ())
		return false;
	int cx = (int) std::floor ((e - originE) / cell);
	int cy = (int) std::floor ((n - originN) / cell);
	for (int radius = 1; radius <= 6; radius++) {
		double wsum = 0.0, zsum = 0.0;
		int found = 0;
		for (int y = cy - radius; y <= cy + radius; y++) {
			if (y < 0 || y >= ny)
				continue;
			for (int x = cx - radius; x <= cx + radius; x++) {
				if (x < 0 || x >= nx)
					continue;
				for (int idx : buckets[(size_t) y * nx + x]) {
					const PointENZ& p = terrain->points[(size_t) idx];
					double d2 = (p.e - e) * (p.e - e) + (p.n - n) * (p.n - n);
					if (d2 < 1e-8) {
						z = p.z;
						return true;
					}
					double w = 1.0 / d2;
					wsum += w;
					zsum += w * p.z;
					found++;
				}
			}
		}
		if (found >= 3 || (found > 0 && radius >= 3)) {
			z = zsum / wsum;
			return true;
		}
	}
	return false;
}


// =============================================================================
// Point reduction
// =============================================================================

static int StepForPercent (double percent)
{
	percent = std::max (0.01, std::min (100.0, percent));
	double s = std::sqrt (100.0 / percent);
	return std::max (1, (int) std::floor (s + 0.5));
}


std::vector<size_t> ReducePoints (const Terrain& t, const ReductionParams& p)
{
	std::vector<size_t> keep;
	if (t.points.empty ())
		return keep;
	if (!p.enabled) {
		keep.resize (t.points.size ());
		for (size_t i = 0; i < keep.size (); i++)
			keep[i] = i;
		return keep;
	}

	double g = std::max (1e-3, t.spacing);
	double e0 = t.bounds.minE, n0 = t.bounds.minN;

	if (p.mode == 0) {
		int step = StepForPercent (p.keepPercent);
		if (t.regularGrid) {
			for (size_t i = 0; i < t.points.size (); i++) {
				long long ix = (long long) std::floor ((t.points[i].e - e0) / g + 0.5);
				long long iy = (long long) std::floor ((t.points[i].n - n0) / g + 0.5);
				if (ix % step == 0 && iy % step == 0)
					keep.push_back (i);
			}
		} else {
			// irregular: one point per bucket of size step*g
			double b = step * g;
			std::unordered_map<long long, size_t> best;
			for (size_t i = 0; i < t.points.size (); i++) {
				long long ix = (long long) std::floor ((t.points[i].e - e0) / b);
				long long iy = (long long) std::floor ((t.points[i].n - n0) / b);
				long long key = ix * 4000037LL + iy;
				if (best.find (key) == best.end ())
					best[key] = i;
			}
			for (const auto& kv : best)
				keep.push_back (kv.second);
			std::sort (keep.begin (), keep.end ());
		}
		return keep;
	}

	// distance mode: density decreases linearly from origin to the farthest corner
	double maxD = 0.0;
	const double cornersE[2] = { t.bounds.minE, t.bounds.maxE };
	const double cornersN[2] = { t.bounds.minN, t.bounds.maxN };
	for (double ce : cornersE)
		for (double cn : cornersN)
			maxD = std::max (maxD, std::hypot (ce - p.origin.e, cn - p.origin.n));
	if (maxD <= 0.0)
		maxD = 1.0;

	for (size_t i = 0; i < t.points.size (); i++) {
		const PointENZ& pt = t.points[i];
		double d = std::hypot (pt.e - p.origin.e, pt.n - p.origin.n);
		double tt = std::min (1.0, d / maxD);
		double percent = p.nearPercent + (p.farPercent - p.nearPercent) * tt;
		int step = StepForPercent (percent);
		// round to power of two so that density levels nest and do not leave holes
		int pow2 = 1;
		while (pow2 * 2 <= step)
			pow2 *= 2;
		if (step - pow2 > pow2 * 2 - step)
			pow2 *= 2;
		long long ix = (long long) std::floor ((pt.e - e0) / g + 0.5);
		long long iy = (long long) std::floor ((pt.n - n0) / g + 0.5);
		if (ix % pow2 == 0 && iy % pow2 == 0)
			keep.push_back (i);
	}
	return keep;
}


// =============================================================================
// Export
// =============================================================================

std::string ToXyzText (const std::vector<PointENZ>& pts, double offsetE, double offsetN, double offsetZ, int decimals)
{
	std::string out;
	out.reserve (pts.size () * 36);
	char buf[128];
	for (const auto& p : pts) {
		snprintf (buf, sizeof (buf), "%.*f %.*f %.*f\r\n", decimals, p.e + offsetE, decimals, p.n + offsetN, decimals, p.z + offsetZ);
		out += buf;
	}
	return out;
}


std::string WorldFile (const BBox& bbox, int width, int height, double offsetE, double offsetN)
{
	double px = bbox.Width () / width;
	double py = bbox.Height () / height;
	char buf[512];
	snprintf (buf, sizeof (buf), "%.10f\r\n0.0\r\n0.0\r\n%.10f\r\n%.6f\r\n%.6f\r\n",
			  px, -py, bbox.minE + offsetE + px * 0.5, bbox.maxN + offsetN - py * 0.5);
	return buf;
}

}	// namespace Geo
