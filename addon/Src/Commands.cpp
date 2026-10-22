#include "Commands.hpp"

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "GeoData.hpp"
#include "Http.hpp"
#include "Placer.hpp"
#include "Terrain.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>

#define PILON_VERSION "0.1.0"

namespace Commands {

// =============================================================================
// Service configuration
// =============================================================================

static const char* kDefaultServices = u8R"JSON({
 "ortho": {
   "title": "Ortofotomozaika ZBGIS (ÚGKK SR)",
   "type": "wms",
   "url": "https://zbgisws.skgeodesy.sk/zbgis_ortofoto_wms/service.svc/get",
   "layers": "1",
   "version": "1.1.1",
   "srs": "EPSG:5514",
   "format": "image/jpeg",
   "resolution": 0.25,
   "maxPx": 4000,
   "maxPxHQ": 16000,
   "tilePx": 2000
 },
 "cadastreC": {
   "title": "Kataster – parcely registra C (INSPIRE WFS)",
   "type": "wfs",
   "url": "https://inspirews.skgeodesy.sk/geoserver/cp/ows",
   "typeName": "cp:CadastralParcel",
   "srsName": "urn:ogc:def:crs:EPSG::5514",
   "labelTags": "label,text,nationalCadastralReference",
   "count": 20000
 },
 "cadastreE": {
   "title": "Kataster – parcely registra E (INSPIRE WFS)",
   "type": "wfs",
   "url": "https://inspirews.skgeodesy.sk/geoserver/cp_uo/ows",
   "typeName": "cp_uo:CadastralParcel",
   "srsName": "urn:ogc:def:crs:EPSG::5514",
   "labelTags": "label,text,nationalCadastralReference",
   "count": 20000
 },
 "terrain": {
   "title": "Terén DMR 5.0 (ÚGKK SR)",
   "type": "wcs",
   "url": "https://inspirews.skgeodesy.sk/geoserver/el/ows",
   "coverageId": "el__DMR5",
   "format": "image/tiff",
   "axisE": "E",
   "axisN": "N",
   "crs": "http://www.opengis.net/def/crs/EPSG/0/5514",
   "resolution": 1.0,
   "maxPoints": 4000000
 },
 "displayLayers": {
   "orthoWms": "https://zbgisws.skgeodesy.sk/zbgis_ortofoto_wms/service.svc/get",
   "orthoLayers": "1",
   "cadastreWms": "https://kataster.skgeodesy.sk/eskn/services/NR/kn_wms_norm/MapServer/WmsServer",
   "cadastreLayers": "1,2,3,5,8,10",
   "cadastreEWms": "https://kataster.skgeodesy.sk/eskn/services/NR/uo_wms_norm/MapServer/WmsServer",
   "cadastreELayers": "0,2"
 },
 "geocoder": "https://nominatim.openstreetmap.org/search"
})JSON";


JsonValue DefaultServices ()
{
	JsonValue v;
	std::string err;
	JsonValue::Parse (kDefaultServices, v, &err);
	return v;
}


static std::wstring ServicesPath ()
{
	return Util::AppDataFolder () + std::wstring ((const wchar_t*) L"\\services.json");
}


static std::wstring SettingsPath ()
{
	return Util::AppDataFolder () + std::wstring ((const wchar_t*) L"\\settings.json");
}


JsonValue LoadServices ()
{
	JsonValue def = DefaultServices ();
	std::string text;
	if (!Util::ReadFile (ServicesPath (), text))
		return def;
	JsonValue user;
	std::string err;
	if (!JsonValue::Parse (text, user, &err) || !user.IsObject ()) {
		Util::Log ("services.json parse error: " + err);
		return def;
	}
	// shallow merge per service (user values override defaults)
	for (const auto& m : user.Members ()) {
		if (m.second.IsObject () && def[m.first].IsObject ()) {
			JsonValue merged = def[m.first];
			for (const auto& f : m.second.Members ())
				merged.Set (f.first, f.second);
			def.Set (m.first, merged);
		} else {
			def.Set (m.first, m.second);
		}
	}
	return def;
}


// =============================================================================
// Session state
// =============================================================================

static Geo::Terrain	gTerrain;
static bool			gTerrainLoaded = false;


// =============================================================================
// Request helpers
// =============================================================================

static Geo::Ring ReadRing (const JsonValue& arr)
{
	Geo::Ring r;
	for (size_t i = 0; i < arr.Size (); i++) {
		const JsonValue& p = arr.At (i);
		if (p.Size () >= 2)
			r.push_back (Geo::PointEN (p.At (0).AsNumber (), p.At (1).AsNumber ()));
		else if (p.IsObject ())
			r.push_back (Geo::PointEN (p["e"].AsNumber (), p["n"].AsNumber ()));
	}
	if (r.size () >= 3 && (r.front ().e != r.back ().e || r.front ().n != r.back ().n))
		r.push_back (r.front ());
	return r;
}


static Geo::ReductionParams ReadReduction (const JsonValue& j, const Geo::PointEN& origin)
{
	Geo::ReductionParams p;
	p.enabled = j["enabled"].AsBool (true);
	p.mode = (int) j["mode"].AsNumber (0);
	p.keepPercent = j["keepPercent"].AsNumber (25.0);
	p.nearPercent = j["nearPercent"].AsNumber (100.0);
	p.farPercent = j["farPercent"].AsNumber (6.0);
	p.origin = origin;
	return p;
}


static Geo::BBox Expand (const Geo::BBox& b, double m)
{
	Geo::BBox r = b;
	r.minE -= m;
	r.minN -= m;
	r.maxE += m;
	r.maxN += m;
	return r;
}


static JsonValue Error (const std::string& msg)
{
	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", false);
	r.Set ("error", msg);
	return r;
}


// =============================================================================
// Downloads
// =============================================================================

static bool DownloadFeatures (const JsonValue& svc, const Geo::BBox& bbox, std::vector<Geo::Feature>& out, std::string& error)
{
	std::string url = Geo::WfsGetFeatureUrl (svc["url"].AsString (), svc["typeName"].AsString (), svc["srsName"].AsString ("urn:ogc:def:crs:EPSG::5514"),
											 bbox, (int) svc["count"].AsNumber (20000));
	Util::Log ("WFS: " + url);
	Http::Response r = Http::Get (url, 180);
	if (!r.ok) {
		error = "Kataster: " + Http::Describe (r);
		return false;
	}
	if (!Geo::ParseGmlFeatures (r.body, svc["labelTags"].AsString ("label,text"), out, error)) {
		error = "Kataster: " + error;
		return false;
	}
	return true;
}


struct OrthoImage {
	std::string	bytes;
	int			width = 0;
	int			height = 0;
	Geo::BBox	bbox;
};


static bool DownloadOrtho (const JsonValue& svc, const Geo::BBox& bbox, bool hq, OrthoImage& out, std::string& error)
{
	double res = svc["resolution"].AsNumber (0.25);
	int maxPx = (int) (hq ? svc["maxPxHQ"].AsNumber (16000) : svc["maxPx"].AsNumber (4000));
	double w = bbox.Width (), h = bbox.Height ();
	int pxW = (int) std::ceil (w / res);
	int pxH = (int) std::ceil (h / res);
	double scale = std::min (1.0, (double) maxPx / std::max (pxW, pxH));
	pxW = std::max (16, (int) std::floor (pxW * scale));
	pxH = std::max (16, (int) std::floor (pxH * scale));

	// Many WMS servers limit the image size; try the full size first, then smaller ones
	int tries[] = { 0, 4096, 2048, 1024 };
	for (int limit : tries) {
		int tw = pxW, th = pxH;
		if (limit > 0 && std::max (tw, th) > limit) {
			double s = (double) limit / std::max (tw, th);
			tw = std::max (16, (int) (tw * s));
			th = std::max (16, (int) (th * s));
		} else if (limit > 0) {
			continue;
		}
		std::string url = Geo::WmsGetMapUrl (svc["url"].AsString (), svc["layers"].AsString ("1"), svc["version"].AsString ("1.1.1"),
											 svc["srs"].AsString ("EPSG:5514"), svc["format"].AsString ("image/jpeg"), bbox, tw, th);
		Util::Log ("WMS: " + url);
		Http::Response r = Http::Get (url, 300);
		bool isImage = r.ok && r.contentType.find ("image") != std::string::npos && r.body.size () > 100;
		if (isImage) {
			out.bytes.swap (r.body);
			out.width = tw;
			out.height = th;
			out.bbox = bbox;
			return true;
		}
		error = "Ortofoto: " + Http::Describe (r);
		std::string ex = Geo::ExtractOgcException (r.body);
		if (!ex.empty ())
			error = "Ortofoto: " + ex;
	}
	return false;
}


static bool DownloadTerrain (const JsonValue& svc, const Geo::BBox& bbox, Geo::Terrain& out, std::string& error)
{
	std::string type = Util::ToLower (svc["type"].AsString ("wcs"));
	double res = std::max (0.1, svc["resolution"].AsNumber (1.0));
	std::string url;
	if (type == "wcs") {
		url = Geo::WcsGetCoverageUrl (svc["url"].AsString (), svc["coverageId"].AsString (), svc["format"].AsString ("image/tiff"), bbox,
									  svc["axisE"].AsString ("E"), svc["axisN"].AsString ("N"), svc["crs"].AsString ());
	} else if (type == "arcgis") {
		int w = std::max (2, (int) std::ceil (bbox.Width () / res));
		int h = std::max (2, (int) std::ceil (bbox.Height () / res));
		url = Geo::ArcGisExportImageUrl (svc["url"].AsString (), bbox, w, h, (int) svc["sr"].AsNumber (5514));
	} else if (type == "url") {
		// generic URL template with {minE},{minN},{maxE},{maxN},{w},{h}
		url = svc["url"].AsString ();
		int w = std::max (2, (int) std::ceil (bbox.Width () / res));
		int h = std::max (2, (int) std::ceil (bbox.Height () / res));
		url = Util::ReplaceAll (url, "{minE}", Util::FormatDouble (bbox.minE, 3));
		url = Util::ReplaceAll (url, "{minN}", Util::FormatDouble (bbox.minN, 3));
		url = Util::ReplaceAll (url, "{maxE}", Util::FormatDouble (bbox.maxE, 3));
		url = Util::ReplaceAll (url, "{maxN}", Util::FormatDouble (bbox.maxN, 3));
		url = Util::ReplaceAll (url, "{w}", std::to_string (w));
		url = Util::ReplaceAll (url, "{h}", std::to_string (h));
	} else {
		error = u8"Služba terénu nie je nastavená (typ '" + type + u8"'). Použite import súboru XYZ / ASC / TIFF.";
		return false;
	}
	Util::Log ("Terrain: " + url);
	Http::Response r = Http::Get (url, 300);
	if (!r.ok) {
		error = u8"Terén: " + Http::Describe (r);
		std::string ex = Geo::ExtractOgcException (r.body);
		if (!ex.empty ())
			error = u8"Terén: " + ex;
		return false;
	}
	if (!Geo::ParseTerrainAuto (r.body, bbox, out, error)) {
		error = u8"Terén: " + error;
		return false;
	}
	out.source = svc["title"].AsString (u8"služba");
	return true;
}


// =============================================================================
// Commands
// =============================================================================

static JsonValue CmdInit ()
{
	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", true);
	r.Set ("version", PILON_VERSION);
	r.Set ("services", LoadServices ());

	double lat = 0, lon = 0, alt = 0;
	if (Placer::GetPlace (lat, lon, alt)) {
		JsonValue place = JsonValue::MakeObject ();
		place.Set ("lat", lat);
		place.Set ("lon", lon);
		place.Set ("alt", alt);
		r.Set ("place", place);
	}

	std::string text;
	JsonValue settings;
	if (Util::ReadFile (SettingsPath (), text) && JsonValue::Parse (text, settings) && settings.IsObject ())
		r.Set ("settings", settings);

	r.Set ("dataFolder", Util::WideToUtf8 (Util::AppDataFolder ()));
	return r;
}


static JsonValue CmdSaveSettings (const JsonValue& req)
{
	if (!Util::WriteFile (SettingsPath (), req["settings"].Serialize ()))
		return Error (u8"Nastavenia sa nepodarilo uložiť");
	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", true);
	return r;
}


static JsonValue CmdSaveServices (const JsonValue& req)
{
	const JsonValue& s = req["services"];
	if (req["reset"].AsBool (false)) {
		DeleteFileW ((LPCWSTR) ServicesPath ().c_str ());
	} else {
		if (!s.IsObject ())
			return Error (u8"Neplatná konfigurácia služieb");
		if (!Util::WriteFile (ServicesPath (), s.Serialize ()))
			return Error (u8"Konfiguráciu služieb sa nepodarilo uložiť");
	}
	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", true);
	r.Set ("services", LoadServices ());
	return r;
}


static JsonValue CmdTestServices (const ProgressFn& progress)
{
	JsonValue services = LoadServices ();
	JsonValue list = JsonValue::MakeArray ();

	struct Probe { const char* key; const char* query; };
	Probe probes[] = {
		{ "ortho",		"SERVICE=WMS&REQUEST=GetCapabilities" },
		{ "cadastreC",	"SERVICE=WFS&VERSION=2.0.0&REQUEST=GetCapabilities" },
		{ "cadastreE",	"SERVICE=WFS&VERSION=2.0.0&REQUEST=GetCapabilities" },
		{ "buildings",	"SERVICE=WFS&VERSION=2.0.0&REQUEST=GetCapabilities" },
		{ "terrain",	nullptr },
	};
	for (const Probe& p : probes) {
		const JsonValue& svc = services[p.key];
		JsonValue item = JsonValue::MakeObject ();
		item.Set ("key", p.key);
		item.Set ("title", svc["title"].AsString (p.key));
		std::string base = svc["url"].AsString ();
		std::string query = p.query != nullptr ? p.query : std::string ();
		if (p.query == nullptr) {
			std::string type = Util::ToLower (svc["type"].AsString ());
			if (type == "wcs")
				query = "SERVICE=WCS&VERSION=2.0.1&REQUEST=GetCapabilities";
			else if (type == "arcgis")
				query = "f=pjson";
		}
		if (base.empty ()) {
			item.Set ("ok", false);
			item.Set ("info", u8"adresa nie je nastavená");
			list.Push (item);
			continue;
		}
		std::string url = query.empty () ? base : Geo::AppendQuery (base, query);
		item.Set ("url", url);
		if (progress)
			progress ("Testujem: " + svc["title"].AsString (p.key));
		Http::Response r = Http::Get (url, 40);
		item.Set ("ok", r.ok);
		item.Set ("info", Http::Describe (r, 160));
		JsonValue names = JsonValue::MakeArray ();
		for (const auto& n : Geo::ExtractCapabilityNames (r.body, 60))
			names.Push (JsonValue::MakeString (n));
		item.Set ("names", names);
		list.Push (item);
	}
	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", true);
	r.Set ("results", list);
	return r;
}


static JsonValue TerrainInfo ()
{
	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", true);
	r.Set ("count", (double) gTerrain.points.size ());
	r.Set ("spacing", gTerrain.spacing);
	r.Set ("regular", gTerrain.regularGrid);
	r.Set ("source", gTerrain.source);
	double mn = 1e9, mx = -1e9;
	for (const auto& p : gTerrain.points) {
		mn = std::min (mn, p.z);
		mx = std::max (mx, p.z);
	}
	r.Set ("minZ", gTerrain.points.empty () ? 0.0 : mn);
	r.Set ("maxZ", gTerrain.points.empty () ? 0.0 : mx);
	return r;
}


static JsonValue CmdLoadTerrain (const JsonValue& req, const ProgressFn& progress)
{
	Geo::Ring area = ReadRing (req["area"]);
	if (area.size () < 4)
		return Error (u8"Najprv vyberte územie");
	Geo::BBox box = Geo::BoundsOf (area);

	Geo::Terrain t;
	std::string error;
	std::string source = req["source"].AsString ("service");
	if (source == "file") {
		std::wstring path = Util::OpenFileDialog (std::wstring ((const wchar_t*) L"Vyberte súbor terénu (XYZ, ASC, TIFF)"),
												   std::wstring ((const wchar_t*) L"Terén (*.xyz;*.txt;*.asc;*.tif;*.tiff;*.csv)|*.xyz;*.txt;*.asc;*.tif;*.tiff;*.csv|Všetky súbory|*.*"));
		if (path.empty ())
			return Error (u8"Zrušené");
		if (progress)
			progress (u8"Načítavam súbor terénu…");
		std::string data;
		if (!Util::ReadFile (path, data))
			return Error (u8"Súbor sa nepodarilo prečítať");
		if (!Geo::ParseTerrainAuto (data, Geo::BBox (), t, error))
			return Error (error);
		t.source = Util::WideToUtf8 (path);
	} else {
		if (progress)
			progress (u8"Sťahujem terén DMR 5.0…");
		JsonValue services = LoadServices ();
		if (!DownloadTerrain (services["terrain"], Expand (box, 3.0), t, error))
			return Error (error);
	}

	size_t before = t.points.size ();
	bool reg = t.regularGrid;
	double sp = t.spacing;
	Geo::ClipTerrain (t, area, std::max (1.0, t.spacing * 2.0));
	t.regularGrid = reg;
	t.spacing = sp;
	if (t.points.empty ())
		return Error (u8"V zvolenom území nie sú žiadne body terénu (" + std::to_string (before) + u8" bodov mimo výberu)");

	gTerrain = t;
	gTerrainLoaded = true;
	return TerrainInfo ();
}


static JsonValue CmdTerrainPreview (const JsonValue& req)
{
	if (!gTerrainLoaded)
		return Error (u8"Najprv načítajte terén");
	size_t maxPts = (size_t) std::max (100.0, req["maxPoints"].AsNumber (12000));
	size_t step = std::max ((size_t) 1, (size_t) std::ceil (std::sqrt ((double) gTerrain.points.size () / (double) maxPts)));
	JsonValue pts = JsonValue::MakeArray ();
	Geo::ReductionParams p;
	p.mode = 0;
	p.keepPercent = 100.0 / (double) (step * step);
	std::vector<size_t> keep = Geo::ReducePoints (gTerrain, p);
	size_t stride = std::max ((size_t) 1, keep.size () / maxPts + 1);
	for (size_t i = 0; i < keep.size (); i += stride) {
		const Geo::PointENZ& q = gTerrain.points[keep[i]];
		JsonValue a = JsonValue::MakeArray ();
		a.Push (JsonValue::MakeNumber (std::round (q.e * 100.0) / 100.0));
		a.Push (JsonValue::MakeNumber (std::round (q.n * 100.0) / 100.0));
		a.Push (JsonValue::MakeNumber (std::round (q.z * 100.0) / 100.0));
		pts.Push (a);
	}
	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", true);
	r.Set ("points", pts);
	return r;
}


static JsonValue CmdReduceCount (const JsonValue& req)
{
	if (!gTerrainLoaded)
		return Error (u8"Terén nie je načítaný");
	Geo::PointEN origin (req["origin"]["e"].AsNumber (), req["origin"]["n"].AsNumber ());
	Geo::ReductionParams p = ReadReduction (req["reduction"], origin);
	std::vector<size_t> keep = Geo::ReducePoints (gTerrain, p);
	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", true);
	r.Set ("kept", (double) keep.size ());
	r.Set ("count", (double) gTerrain.points.size ());
	return r;
}


static void AddResult (JsonValue& results, const std::string& item, bool ok, const std::string& msg)
{
	JsonValue r = JsonValue::MakeObject ();
	r.Set ("item", item);
	r.Set ("ok", ok);
	r.Set ("message", msg);
	results.Push (r);
}


static Placer::LineStyle ReadStyle (const JsonValue& j, const std::string& defLayer, short defPen)
{
	Placer::LineStyle s;
	s.layer = j["layer"].AsString (defLayer);
	s.pen = (short) j["pen"].AsNumber (defPen);
	s.lineType = j["lineType"].AsString ();
	return s;
}


static JsonValue CmdPlace (const JsonValue& req, const ProgressFn& progress)
{
	Geo::Ring area = ReadRing (req["area"]);
	if (area.size () < 4)
		return Error (u8"Najprv vyberte územie");
	Geo::BBox box = Geo::BoundsOf (area);
	Geo::PointEN origin (req["origin"]["e"].AsNumber ((box.minE + box.maxE) * 0.5), req["origin"]["n"].AsNumber ((box.minN + box.maxN) * 0.5));
	double h0 = req["h0"].AsNumber (0.0);
	bool inJtsk = req["insertJtsk"].AsBool (false);
	bool inLocal = req["insertLocal"].AsBool (true);
	if (!inJtsk && !inLocal)
		inLocal = true;
	bool toWorksheet = req["target"].AsString ("plan") == "worksheet";
	const JsonValue& items = req["items"];
	const JsonValue& st = req["settings"];
	double textSize = st["textSize"].AsNumber (2.0);

	JsonValue services = LoadServices ();
	JsonValue results = JsonValue::MakeArray ();

	// ---- Phase 1: downloads (outside of the undo step) ----
	std::vector<Geo::Feature> parcelsC, parcelsE;
	bool haveC = false, haveE = false, haveOrtho = false;
	OrthoImage ortho;
	std::string error;

	if (items["cadastreC"].AsBool ()) {
		if (progress)
			progress (u8"Sťahujem kataster – parcely C…");
		haveC = DownloadFeatures (services["cadastreC"], box, parcelsC, error);
		if (!haveC)
			AddResult (results, "Kataster C", false, error);
	}
	if (items["cadastreE"].AsBool ()) {
		if (progress)
			progress (u8"Sťahujem kataster – parcely E…");
		error.clear ();
		haveE = DownloadFeatures (services["cadastreE"], box, parcelsE, error);
		if (!haveE)
			AddResult (results, "Kataster E", false, error);
	}
	std::vector<Geo::Feature> buildings;
	bool haveBuildings = false;
	if (items["buildings"].AsBool ()) {
		if (progress)
			progress (u8"Sťahujem budovy ZBGIS…");
		error.clear ();
		haveBuildings = DownloadFeatures (services["buildings"], box, buildings, error);
		if (!haveBuildings)
			AddResult (results, "Budovy ZBGIS", false, error);
	}

	std::vector<Geo::Ring> plotOutline;
	if (items["plot"].AsBool () && req["plot"].Size () > 0) {
		std::vector<Geo::Ring> rings;
		for (size_t i = 0; i < req["plot"].Size (); i++) {
			Geo::Ring r = ReadRing (req["plot"].At (i));
			if (r.size () >= 4)
				rings.push_back (r);
		}
		plotOutline = Geo::OuterBoundary (rings);
		if (plotOutline.empty ())
			AddResult (results, "Pozemok", false, u8"Hranicu pozemku sa nepodarilo vytvoriť");
	}

	if (items["ortho"].AsBool ()) {
		if (progress)
			progress (u8"Sťahujem ortofoto…");
		error.clear ();
		haveOrtho = DownloadOrtho (services["ortho"], box, req["orthoHQ"].AsBool (false), ortho, error);
		if (!haveOrtho)
			AddResult (results, "Ortofoto", false, error);
	}

	TerrainMesh::Output mesh;
	bool haveMesh = false;
	if (items["terrain"].AsBool ()) {
		if (!gTerrainLoaded) {
			if (progress)
				progress (u8"Sťahujem terén…");
			JsonValue loadReq = JsonValue::MakeObject ();
			loadReq.Set ("area", req["area"]);
			JsonValue lr = CmdLoadTerrain (loadReq, progress);
			if (!lr["ok"].AsBool ())
				AddResult (results, u8"Terén", false, lr["error"].AsString ());
		}
		if (gTerrainLoaded) {
			if (progress)
				progress (u8"Pripravujem mesh terénu…");
			TerrainMesh::Input in;
			in.terrain = &gTerrain;
			in.polygon = area;
			in.exactOutline = st["terrainExactOutline"].AsBool (true);
			in.reduction = ReadReduction (req["reduction"], origin);
			in.h0 = h0;
			haveMesh = TerrainMesh::Build (in, mesh);
			if (!haveMesh)
				AddResult (results, u8"Terén", false, mesh.error);
		}
	}

	// ---- Phase 2: element creation in one undo step ----
	if (progress)
		progress (u8"Vkladám prvky do Archicadu…");

	Placer::Stats stats;
	Placer::Offset localOff;
	localOff.e = -origin.e;
	localOff.n = -origin.n;
	Placer::Offset jtskOff;

	std::vector<Placer::Offset> offsets;
	if (inJtsk)
		offsets.push_back (jtskOff);
	if (inLocal)
		offsets.push_back (localOff);

	const JsonValue& layers = st["layers"];

	GSErrCode err = ACAPI_CallUndoableCommand ("Pilon Map Downloader", [&] () -> GSErrCode {
		Placer::TargetDatabase db;
		if (!toWorksheet)
			db.SwitchToFloorPlan ();
		if (toWorksheet) {
			if (db.SwitchToWorksheet (Util::ToUni (st["worksheetName"].AsString (u8"Mapové podklady"))) != NoError)
				AddResult (results, u8"Pracovný list", false, u8"Pracovný list sa nepodarilo vytvoriť – vkladám do pôdorysu");
		}

		auto placeParcels = [&] (const std::vector<Geo::Feature>& parcels, const std::string& lineKey, const std::string& textKey,
								 const std::string& defLine, const std::string& defText, short defPen) {
			Placer::LineStyle ls = ReadStyle (layers[lineKey], defLine, defPen);
			Placer::LineStyle ts = ReadStyle (layers[textKey], defText, defPen);
			for (const Placer::Offset& off : offsets) {
				for (const Geo::Feature& f : parcels) {
					Placer::PlaceRings (f.rings, ls, off, stats);
					if (!f.label.empty () && !f.rings.empty ()) {
						Geo::PointEN pos = f.hasRefPoint ? f.refPoint : Geo::Centroid (f.rings[0]);
						Placer::PlaceText (f.label, pos, ts.layer, ts.pen, textSize, off, stats);
					}
				}
			}
		};

		if (haveC) {
			int l0 = stats.lines;
			placeParcels (parcelsC, "cadastreC", "cadastreCText", "KN_parcely_C", "KN_cisla_C", 1);
			AddResult (results, "Kataster C", true, std::to_string (parcelsC.size ()) + " parciel, " + std::to_string (stats.lines - l0) + u8" čiar");
		}
		if (haveE) {
			int l0 = stats.lines;
			placeParcels (parcelsE, "cadastreE", "cadastreEText", "KN_parcely_E", "KN_cisla_E", 3);
			AddResult (results, "Kataster E", true, std::to_string (parcelsE.size ()) + " parciel, " + std::to_string (stats.lines - l0) + u8" čiar");
		}
		if (haveBuildings) {
			Placer::LineStyle bs = ReadStyle (layers["buildings"], "ZBGIS_budovy", 2);
			int l0 = stats.lines;
			for (const Placer::Offset& off : offsets)
				for (const Geo::Feature& f : buildings)
					Placer::PlaceRings (f.rings, bs, off, stats);
			AddResult (results, "Budovy ZBGIS", true, std::to_string (buildings.size ()) + u8" budov, " + std::to_string (stats.lines - l0) + u8" čiar");
		}
		if (!plotOutline.empty ()) {
			Placer::LineStyle ps = ReadStyle (layers["plot"], "KN_POZEMOK", 20);
			for (const Placer::Offset& off : offsets)
				Placer::PlaceRings (plotOutline, ps, off, stats);
			char buf[120];
			snprintf (buf, sizeof (buf), u8"%u obrys(y), výmera %.0f m²", (unsigned) plotOutline.size (), std::fabs (Geo::SignedArea (plotOutline[0])));
			AddResult (results, "Pozemok", true, buf);
		}
		if (haveOrtho) {
			std::string layer = layers["ortho"]["layer"].AsString ("ORTOFOTO");
			int ok = 0;
			for (const Placer::Offset& off : offsets)
				if (Placer::PlacePicture (ortho.bytes, ortho.width, ortho.height, ortho.bbox, layer, "Ortofoto ZBGIS", off, stats))
					ok++;
			AddResult (results, "Ortofoto", ok > 0, ok > 0 ? (std::to_string (ortho.width) + u8" × " + std::to_string (ortho.height) + " px") : stats.lastError);
		}

		if (haveMesh) {
			// mesh is always placed in the local origin on the floor plan
			db.SwitchToFloorPlan ();
			std::string layer = layers["terrain"]["layer"].AsString ("TEREN_DMR5");
			bool ok = Placer::PlaceMesh (mesh.boundary, mesh.interior, layer, localOff, stats);
			char buf[200];
			snprintf (buf, sizeof (buf), u8"%u bodov (z %u), výšky %.2f až %.2f m od 0,00",
					  (unsigned) mesh.keptPoints, (unsigned) mesh.sourcePoints, mesh.minZ, mesh.maxZ);
			AddResult (results, u8"Terén", ok, ok ? std::string (buf) : stats.lastError);
		}

		if (items["originMarker"].AsBool (true) && (inLocal || haveMesh)) {
			Placer::PlaceOriginMarker (origin, layers["origin"]["layer"].AsString ("POCIATOK"), localOff, h0, stats);
		}
		db.Restore ();
		return NoError;
	});

	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", err == NoError);
	if (err != NoError)
		r.Set ("error", "Vkladanie zlyhalo (chyba " + std::to_string ((int) err) + ")");
	r.Set ("results", results);
	r.Set ("failed", stats.failed);
	if (!stats.lastError.empty ())
		r.Set ("lastError", stats.lastError);
	return r;
}


static JsonValue FeatureToJson (const Geo::Feature& f)
{
	JsonValue o = JsonValue::MakeObject ();
	o.Set ("id", f.id.empty () ? f.label : f.id);
	o.Set ("label", f.label);
	double area = 0.0;
	JsonValue rings = JsonValue::MakeArray ();
	for (size_t ri = 0; ri < f.rings.size (); ri++) {
		const Geo::Ring& r = f.rings[ri];
		area += (ri == 0 ? 1.0 : -1.0) * std::fabs (Geo::SignedArea (r));
		JsonValue ra = JsonValue::MakeArray ();
		for (const auto& p : r) {
			JsonValue pt = JsonValue::MakeArray ();
			pt.Push (JsonValue::MakeNumber (std::round (p.e * 1000.0) / 1000.0));
			pt.Push (JsonValue::MakeNumber (std::round (p.n * 1000.0) / 1000.0));
			ra.Push (pt);
		}
		rings.Push (ra);
	}
	o.Set ("rings", rings);
	o.Set ("area", std::round (area * 100.0) / 100.0);
	o.Set ("official", f.officialArea);
	return o;
}


static JsonValue CmdPlotParcels (const JsonValue& req, const ProgressFn& progress)
{
	Geo::Ring poly = ReadRing (req["polygon"]);
	if (poly.size () < 4)
		return Error (u8"Nakreslite polygón okolo parciel pozemku");
	if (progress)
		progress (u8"Hľadám parcely vo vybranom tvare…");
	std::vector<Geo::Feature> features;
	std::string error;
	if (!DownloadFeatures (LoadServices ()["cadastreC"], Geo::BoundsOf (poly), features, error))
		return Error (error);
	JsonValue list = JsonValue::MakeArray ();
	for (const Geo::Feature& f : features) {
		if (f.rings.empty ())
			continue;
		if (Geo::RingFullyInside (f.rings[0], poly))
			list.Push (FeatureToJson (f));
	}
	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", true);
	r.Set ("parcels", list);
	return r;
}


static bool RingsTouch (const Geo::Ring& a, const Geo::Ring& b, double tol)
{
	Geo::BBox ba = Geo::BoundsOf (a), bb = Geo::BoundsOf (b);
	if (ba.maxE + tol < bb.minE || bb.maxE + tol < ba.minE || ba.maxN + tol < bb.minN || bb.maxN + tol < ba.minN)
		return false;
	for (const auto& p : a)
		if (Geo::DistanceToPolygonEdges (b, p.e, p.n) <= tol)
			return true;
	for (const auto& p : b)
		if (Geo::DistanceToPolygonEdges (a, p.e, p.n) <= tol)
			return true;
	return false;
}


static std::string XmlEsc (const std::string& s)
{
	std::string o;
	for (char c : s) {
		if (c == '&') o += "&amp;";
		else if (c == '<') o += "&lt;";
		else if (c == '>') o += "&gt;";
		else if (c == '"') o += "&quot;";
		else o += c;
	}
	return o;
}


static JsonValue CmdPlotReport (const JsonValue& req, const ProgressFn& progress)
{
	const JsonValue& parcels = req["parcels"];
	if (parcels.Size () == 0)
		return Error (u8"Najprv vyberte pozemok (▣ Vybrať pozemok)");

	std::vector<Geo::Ring> selRings;
	std::vector<std::string> selIds;
	Geo::BBox box;
	bool first = true;
	for (size_t i = 0; i < parcels.Size (); i++) {
		const JsonValue& p = parcels.At (i);
		Geo::Ring r = ReadRing (p["rings"].At (0));
		if (r.size () < 4)
			continue;
		selRings.push_back (r);
		selIds.push_back (p["id"].AsString ());
		Geo::BBox b = Geo::BoundsOf (r);
		if (first) { box = b; first = false; }
		else { box.minE = std::min (box.minE, b.minE); box.minN = std::min (box.minN, b.minN); box.maxE = std::max (box.maxE, b.maxE); box.maxN = std::max (box.maxN, b.maxN); }
	}

	std::wstring path = Util::SaveFileDialog (std::wstring ((const wchar_t*) L"Uložiť report o pozemku"), std::wstring ((const wchar_t*) L"report_pozemok.txt"),
											  std::wstring ((const wchar_t*) L"Text (*.txt)|*.txt"), std::wstring ((const wchar_t*) L"txt"));
	if (path.empty ())
		return Error (u8"Zrušené");

	if (progress)
		progress (u8"Hľadám susedné parcely…");
	std::vector<Geo::Feature> around;
	std::string error;
	DownloadFeatures (LoadServices ()["cadastreC"], Expand (box, 25.0), around, error);

	std::vector<const Geo::Feature*> neighbours;
	for (const Geo::Feature& f : around) {
		std::string id = f.id.empty () ? f.label : f.id;
		if (std::find (selIds.begin (), selIds.end (), id) != selIds.end () || f.rings.empty ())
			continue;
		for (const Geo::Ring& r : selRings) {
			if (RingsTouch (f.rings[0], r, 0.05)) {
				neighbours.push_back (&f);
				break;
			}
		}
	}

	std::vector<Geo::Ring> outline = Geo::OuterBoundary (selRings);
	double total = 0.0, totalOfficial = 0.0;

	std::string txt = u8"REPORT O POZEMKU (informatívny)\r\n";
	txt += u8"Zdroj: ÚGKK SR – kataster nehnuteľností (INSPIRE), Pilon Map Downloader\r\n\r\n";
	txt += u8"Parcely pozemku (register C):\r\n";
	std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<pozemok>\r\n  <parcely>\r\n";
	for (size_t i = 0; i < parcels.Size (); i++) {
		const JsonValue& p = parcels.At (i);
		double a = p["area"].AsNumber (), off = p["official"].AsNumber ();
		total += a;
		totalOfficial += off;
		char buf[300];
		snprintf (buf, sizeof (buf), u8"  %-14s  výmera z geometrie %10.1f m²%s\r\n", p["label"].AsString ().c_str (), a,
				  off > 0 ? (std::string (u8"   úradná ") + Util::FormatDouble (off, 0) + u8" m²").c_str () : "");
		txt += buf;
		xml += "    <parcela id=\"" + XmlEsc (p["id"].AsString ()) + "\" cislo=\"" + XmlEsc (p["label"].AsString ()) + "\" vymeraGeometria=\"" +
			   Util::FormatDouble (a, 2) + "\" vymeraUradna=\"" + Util::FormatDouble (off, 0) + "\"/>\r\n";
	}
	char buf[300];
	snprintf (buf, sizeof (buf), u8"\r\nSpolu: %u parciel, výmera z geometrie %.1f m²%s\r\n", (unsigned) parcels.Size (), total,
			  totalOfficial > 0 ? (std::string (u8", úradná ") + Util::FormatDouble (totalOfficial, 0) + u8" m²").c_str () : "");
	txt += buf;
	if (!outline.empty ()) {
		snprintf (buf, sizeof (buf), u8"Obrys pozemku: %u vrcholov, obvod %.2f m\r\n", (unsigned) outline[0].size (), [&] () {
			double l = 0; for (size_t i = 0; i + 1 < outline[0].size (); i++) l += std::hypot (outline[0][i + 1].e - outline[0][i].e, outline[0][i + 1].n - outline[0][i].n); return l; } ());
		txt += buf;
	}
	xml += "  </parcely>\r\n  <susedneParcely>\r\n";
	txt += u8"\r\nSusedné parcely (dotýkajúce sa pozemku aspoň bodom):\r\n";
	if (!error.empty ())
		txt += u8"  (susedné parcely sa nepodarilo načítať: " + error + ")\r\n";
	for (const Geo::Feature* f : neighbours) {
		double a = f->rings.empty () ? 0.0 : std::fabs (Geo::SignedArea (f->rings[0]));
		snprintf (buf, sizeof (buf), u8"  %-14s  výmera z geometrie %10.1f m²\r\n", f->label.c_str (), a);
		txt += buf;
		xml += "    <parcela id=\"" + XmlEsc (f->id) + "\" cislo=\"" + XmlEsc (f->label) + "\" vymeraGeometria=\"" + Util::FormatDouble (a, 2) + "\"/>\r\n";
	}
	xml += "  </susedneParcely>\r\n</pozemok>\r\n";
	txt += u8"\r\nVlastníkov služba neposkytuje – overte na kataster.skgeodesy.sk alebo v aplikácii MAPKA.\r\n";

	// UTF-8 BOM for Notepad
	if (!Util::WriteFile (path, std::string ("\xEF\xBB\xBF") + txt))
		return Error (u8"Súbor sa nepodarilo zapísať");
	Util::WriteFile (Util::StripExtension (path) + std::wstring ((const wchar_t*) L".xml"), xml);

	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", true);
	r.Set ("path", Util::WideToUtf8 (path));
	r.Set ("neighbours", (double) neighbours.size ());
	return r;
}


static std::string OriginText (const Geo::PointEN& origin, double h0)
{
	char buf[600];
	snprintf (buf, sizeof (buf),
			  "Bod lokalneho pociatku (Pilon Map Downloader)\r\n"
			  "S-JTSK / Krovak East North (EPSG:5514):  E = %.3f   N = %.3f\r\n"
			  "S-JTSK (Y, X kladne):                   Y = %.3f   X = %.3f\r\n"
			  "Vyska 0,00 = %.3f m n. m. (Bpv)\r\n"
			  "Model: x = E - (%.3f), y = N - (%.3f), z = H - %.3f\r\n",
			  origin.e, origin.n, -origin.e, -origin.n, h0, origin.e, origin.n, h0);
	return buf;
}


static JsonValue CmdExportXyz (const JsonValue& req, const ProgressFn& progress)
{
	if (!gTerrainLoaded)
		return Error (u8"Najprv načítajte terén");
	Geo::Ring area = ReadRing (req["area"]);
	Geo::BBox box = Geo::BoundsOf (area);
	Geo::PointEN origin (req["origin"]["e"].AsNumber ((box.minE + box.maxE) * 0.5), req["origin"]["n"].AsNumber ((box.minN + box.maxN) * 0.5));
	double h0 = req["h0"].AsNumber (0.0);
	bool local = req["local"].AsBool (false);
	bool positive = req["positiveYX"].AsBool (false);

	std::wstring path = Util::SaveFileDialog (std::wstring ((const wchar_t*) L"Uložiť terén ako XYZ"), std::wstring ((const wchar_t*) L"teren_dmr5.xyz"),
											  std::wstring ((const wchar_t*) L"XYZ (*.xyz)|*.xyz|Text (*.txt)|*.txt"), std::wstring ((const wchar_t*) L"xyz"));
	if (path.empty ())
		return Error (u8"Zrušené");
	if (progress)
		progress (u8"Ukladám XYZ…");

	std::vector<size_t> keep = Geo::ReducePoints (gTerrain, ReadReduction (req["reduction"], origin));
	std::vector<Geo::PointENZ> pts;
	pts.reserve (keep.size ());
	for (size_t i : keep) {
		Geo::PointENZ p = gTerrain.points[i];
		if (positive) {
			double y = -p.e, x = -p.n;
			p.e = y;
			p.n = x;
		}
		pts.push_back (p);
	}
	double offE = 0, offN = 0, offZ = 0;
	if (local) {
		offE = positive ? origin.e : -origin.e;
		offN = positive ? origin.n : -origin.n;
		offZ = -h0;
	}
	if (!Util::WriteFile (path, Geo::ToXyzText (pts, offE, offN, offZ, 3)))
		return Error (u8"Súbor sa nepodarilo zapísať");
	Util::WriteFile (Util::DirName (path) + std::wstring ((const wchar_t*) L"\\bod_pociatku.txt"), OriginText (origin, h0));

	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", true);
	r.Set ("path", Util::WideToUtf8 (path));
	r.Set ("points", (double) pts.size ());
	return r;
}


static JsonValue CmdExportOrtho (const JsonValue& req, const ProgressFn& progress)
{
	Geo::Ring area = ReadRing (req["area"]);
	if (area.size () < 4)
		return Error (u8"Najprv vyberte územie");
	Geo::BBox box = Geo::BoundsOf (area);
	std::wstring path = Util::SaveFileDialog (std::wstring ((const wchar_t*) L"Uložiť ortofoto"), std::wstring ((const wchar_t*) L"ortofoto.jpg"),
											  std::wstring ((const wchar_t*) L"JPEG (*.jpg)|*.jpg"), std::wstring ((const wchar_t*) L"jpg"));
	if (path.empty ())
		return Error (u8"Zrušené");
	if (progress)
		progress (u8"Sťahujem ortofoto…");
	OrthoImage img;
	std::string error;
	if (!DownloadOrtho (LoadServices ()["ortho"], box, req["orthoHQ"].AsBool (false), img, error))
		return Error (error);
	if (!Util::WriteFile (path, img.bytes))
		return Error (u8"Súbor sa nepodarilo zapísať");
	Util::WriteFile (Util::StripExtension (path) + std::wstring ((const wchar_t*) L".jgw"), Geo::WorldFile (img.bbox, img.width, img.height, 0, 0));
	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", true);
	r.Set ("path", Util::WideToUtf8 (path));
	return r;
}


static JsonValue CmdSetPlace (const JsonValue& req)
{
	bool ok = false;
	GSErrCode err = ACAPI_CallUndoableCommand ("Pilon Map Downloader - location", [&] () -> GSErrCode {
		ok = Placer::SetPlace (req["lat"].AsNumber (), req["lon"].AsNumber (), req["alt"].AsNumber ());
		return NoError;
	});
	if (err != NoError || !ok)
		return Error (u8"Umiestnenie projektu sa nepodarilo zmeniť");
	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", true);
	return r;
}


static JsonValue CmdGetPlace ()
{
	double lat = 0, lon = 0, alt = 0;
	if (!Placer::GetPlace (lat, lon, alt))
		return Error (u8"Umiestnenie projektu sa nepodarilo načítať");
	JsonValue r = JsonValue::MakeObject ();
	r.Set ("ok", true);
	r.Set ("lat", lat);
	r.Set ("lon", lon);
	r.Set ("alt", alt);
	return r;
}


JsonValue Execute (const JsonValue& request, const ProgressFn& progress)
{
	std::string cmd = request["cmd"].AsString ();
	Util::Log ("cmd: " + cmd);
	try {
		if (cmd == "init")				return CmdInit ();
		if (cmd == "saveSettings")		return CmdSaveSettings (request);
		if (cmd == "saveServices")		return CmdSaveServices (request);
		if (cmd == "testServices")		return CmdTestServices (progress);
		if (cmd == "loadTerrain")		return CmdLoadTerrain (request, progress);
		if (cmd == "reduceCount")		return CmdReduceCount (request);
		if (cmd == "terrainPreview")	return CmdTerrainPreview (request);
		if (cmd == "terrainInfo")		return gTerrainLoaded ? TerrainInfo () : Error (u8"Terén nie je načítaný");
		if (cmd == "place")				return CmdPlace (request, progress);
		if (cmd == "exportXyz")			return CmdExportXyz (request, progress);
		if (cmd == "exportOrtho")		return CmdExportOrtho (request, progress);
		if (cmd == "setPlace")			return CmdSetPlace (request);
		if (cmd == "plotParcels")		return CmdPlotParcels (request, progress);
		if (cmd == "plotReport")		return CmdPlotReport (request, progress);
		if (cmd == "getPlace")			return CmdGetPlace ();
	} catch (const std::exception& e) {
		Util::Log (std::string ("exception: ") + e.what ());
		return Error (std::string (u8"Interná chyba: ") + e.what ());
	} catch (...) {
		return Error (u8"Interná chyba");
	}
	return Error (u8"Neznámy príkaz: " + cmd);
}

}	// namespace Commands
