#include "Placer.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Placer {

// =============================================================================
// Target database
// =============================================================================

TargetDatabase::TargetDatabase () : changed (false)
{
	BNZeroMemory (&original, sizeof (original));
	ACAPI_Database (APIDb_GetCurrentDatabaseID, &original, nullptr);
}


TargetDatabase::~TargetDatabase ()
{
	Restore ();
}


GSErrCode TargetDatabase::SwitchToWorksheet (const GS::UniString& name)
{
	GS::Array<API_DatabaseUnId> dbs;
	ACAPI_Database (APIDb_GetWorksheetDatabasesID, nullptr, &dbs);

	API_DatabaseInfo target;
	bool found = false;
	for (const API_DatabaseUnId& id : dbs) {
		API_DatabaseInfo info;
		BNZeroMemory (&info, sizeof (info));
		info.typeID = APIWind_WorksheetID;
		info.databaseUnId = id;
		if (ACAPI_Database (APIDb_GetDatabaseInfoID, &info, nullptr) == NoError) {
			if (GS::UniString (info.name) == name) {
				target = info;
				found = true;
				break;
			}
		}
	}

	if (!found) {
		BNZeroMemory (&target, sizeof (target));
		target.typeID = APIWind_WorksheetID;
		GS::ucsncpy (target.name, name.ToUStr ().Get (), API_UniLongNameLen - 1);
		GS::ucsncpy (target.ref, GS::UniString ("MAP").ToUStr ().Get (), API_UniLongNameLen - 1);
		GSErrCode err = ACAPI_Database (APIDb_NewDatabaseID, &target, nullptr);
		if (err != NoError)
			return err;
	}

	GSErrCode err = ACAPI_Database (APIDb_ChangeCurrentDatabaseID, &target, nullptr);
	if (err == NoError)
		changed = true;
	return err;
}


GSErrCode TargetDatabase::SwitchToFloorPlan ()
{
	if (original.typeID == APIWind_FloorPlanID && !changed)
		return NoError;
	API_DatabaseInfo plan;
	BNZeroMemory (&plan, sizeof (plan));
	plan.typeID = APIWind_FloorPlanID;
	GSErrCode err = ACAPI_Database (APIDb_ChangeCurrentDatabaseID, &plan, nullptr);
	if (err == NoError)
		changed = true;
	return err;
}


void TargetDatabase::Restore ()
{
	if (changed) {
		ACAPI_Database (APIDb_ChangeCurrentDatabaseID, &original, nullptr);
		changed = false;
	}
}


// =============================================================================
// Attributes
// =============================================================================

API_AttributeIndex EnsureLayer (const std::string& name)
{
	if (name.empty ())
		return 1;
	GS::UniString uname = Util::ToUni (name);
	API_Attribute attrib;
	BNZeroMemory (&attrib, sizeof (attrib));
	attrib.header.typeID = API_LayerID;
	attrib.header.uniStringNamePtr = &uname;
	GSErrCode err = ACAPI_Attribute_Create (&attrib, nullptr);
	if (err == NoError || err == APIERR_ATTREXIST)
		return attrib.header.index;

	// fall back to search
	API_Attr_Head head;
	BNZeroMemory (&head, sizeof (head));
	head.typeID = API_LayerID;
	head.uniStringNamePtr = &uname;
	if (ACAPI_Attribute_Search (&head) == NoError)
		return head.index;
	return 1;
}


API_AttributeIndex FindLineType (const std::string& name)
{
	if (name.empty ())
		return 1;
	GS::UniString uname = Util::ToUni (name);
	API_Attr_Head head;
	BNZeroMemory (&head, sizeof (head));
	head.typeID = API_LinetypeID;
	head.uniStringNamePtr = &uname;
	if (ACAPI_Attribute_Search (&head) == NoError)
		return head.index;
	return 1;
}


static void NoteError (Stats& stats, const char* what, GSErrCode err)
{
	stats.failed++;
	char buf[160];
	snprintf (buf, sizeof (buf), "%s (chyba %d)", what, (int) err);
	stats.lastError = buf;
}


// =============================================================================
// Elements
// =============================================================================

void PlaceRings (const std::vector<Geo::Ring>& rings, const LineStyle& style, const Offset& off, Stats& stats)
{
	API_AttributeIndex layer = EnsureLayer (style.layer);
	API_AttributeIndex ltype = FindLineType (style.lineType);

	for (const Geo::Ring& ring : rings) {
		if (ring.size () < 2)
			continue;

		API_Element element;
		BNZeroMemory (&element, sizeof (element));
		element.header.typeID = API_PolyLineID;
		GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
		if (err != NoError) {
			NoteError (stats, u8"Predvolené nastavenie čiary", err);
			return;
		}

		Int32 n = (Int32) ring.size ();
		element.header.layer = layer;
		element.polyLine.poly.nCoords = n;
		element.polyLine.poly.nSubPolys = 1;
		element.polyLine.poly.nArcs = 0;
		element.polyLine.linePen.penIndex = style.pen;
		element.polyLine.ltypeInd = ltype;

		API_ElementMemo memo;
		BNZeroMemory (&memo, sizeof (memo));
		memo.coords = reinterpret_cast<API_Coord**> (BMAllocateHandle ((n + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
		memo.pends = reinterpret_cast<Int32**> (BMAllocateHandle (2 * sizeof (Int32), ALLOCATE_CLEAR, 0));
		if (memo.coords == nullptr || memo.pends == nullptr) {
			ACAPI_DisposeElemMemoHdls (&memo);
			NoteError (stats, u8"Nedostatok pamäte", APIERR_MEMFULL);
			return;
		}
		(*memo.coords)[0].x = -1.0;
		for (Int32 i = 0; i < n; i++) {
			(*memo.coords)[i + 1].x = ring[(size_t) i].e + off.e;
			(*memo.coords)[i + 1].y = ring[(size_t) i].n + off.n;
		}
		(*memo.pends)[0] = 0;
		(*memo.pends)[1] = n;

		err = ACAPI_Element_Create (&element, &memo);
		ACAPI_DisposeElemMemoHdls (&memo);
		if (err != NoError)
			NoteError (stats, u8"Vytvorenie čiary", err);
		else
			stats.lines++;
	}
}


void PlaceText (const std::string& text, const Geo::PointEN& pos, const std::string& layer, short pen, double sizeMm, const Offset& off, Stats& stats)
{
	if (text.empty ())
		return;

	API_Element element;
	BNZeroMemory (&element, sizeof (element));
	element.header.typeID = API_TextID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		NoteError (stats, u8"Predvolené nastavenie textu", err);
		return;
	}
	element.header.layer = EnsureLayer (layer);
	element.text.loc.x = pos.e + off.e;
	element.text.loc.y = pos.n + off.n;
	element.text.anchor = APIAnc_MM;
	element.text.just = APIJust_Center;
	element.text.size = sizeMm > 0.0 ? sizeMm : 2.0;
	element.text.angle = 0.0;
	element.text.nonBreaking = true;
	element.text.multiStyle = false;
	element.text.pen = pen;

	API_ElementMemo memo;
	BNZeroMemory (&memo, sizeof (memo));
	memo.textContent = BMAllocateHandle ((GSSize) text.size () + 1, ALLOCATE_CLEAR, 0);
	if (memo.textContent == nullptr) {
		NoteError (stats, u8"Nedostatok pamäte", APIERR_MEMFULL);
		return;
	}
	memcpy (*memo.textContent, text.c_str (), text.size () + 1);

	err = ACAPI_Element_Create (&element, &memo);
	ACAPI_DisposeElemMemoHdls (&memo);
	if (err != NoError)
		NoteError (stats, "Vytvorenie textu", err);
	else
		stats.texts++;
}


bool PlacePicture (const std::string& imageBytes, int widthPx, int heightPx, const Geo::BBox& bbox, const std::string& layer,
				   const std::string& name, const Offset& off, Stats& stats)
{
	if (imageBytes.empty ())
		return false;

	API_Element element;
	BNZeroMemory (&element, sizeof (element));
	element.header.typeID = API_PictureID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		NoteError (stats, u8"Predvolené nastavenie obrázka", err);
		return false;
	}

	bool isPng = imageBytes.size () > 4 && (unsigned char) imageBytes[0] == 0x89 && imageBytes[1] == 'P';

	element.header.layer = EnsureLayer (layer);
	element.picture.usePixelSize = false;
	element.picture.mirrored = false;
	element.picture.transparent = false;
	element.picture.rotAngle = 0.0;
	element.picture.anchorPoint = APIAnc_LB;
	element.picture.storageFormat = isPng ? APIPictForm_PNG : APIPictForm_JPEG;
	element.picture.colorDepth = APIColorDepth_TC24;
	element.picture.pixelSizeX = (short) std::min (widthPx, 32767);
	element.picture.pixelSizeY = (short) std::min (heightPx, 32767);
	element.picture.origBox.xMin = 0.0;
	element.picture.origBox.yMin = 0.0;
	element.picture.origBox.xMax = widthPx;
	element.picture.origBox.yMax = heightPx;
	element.picture.destBox.xMin = bbox.minE + off.e;
	element.picture.destBox.yMin = bbox.minN + off.n;
	element.picture.destBox.xMax = bbox.maxE + off.e;
	element.picture.destBox.yMax = bbox.maxN + off.n;
	GS::UniString uname = Util::ToUni (name);
	GS::ucsncpy (element.picture.pictName, uname.ToUStr ().Get (), API_UniLongNameLen - 1);

	API_ElementMemo memo;
	BNZeroMemory (&memo, sizeof (memo));
	memo.pictHdl = BMAllocateHandle ((GSSize) imageBytes.size (), ALLOCATE_CLEAR, 0);
	if (memo.pictHdl == nullptr) {
		NoteError (stats, u8"Nedostatok pamäte pre obrázok", APIERR_MEMFULL);
		return false;
	}
	memcpy (*memo.pictHdl, imageBytes.data (), imageBytes.size ());

	err = ACAPI_Element_Create (&element, &memo);
	ACAPI_DisposeElemMemoHdls (&memo);
	if (err != NoError) {
		NoteError (stats, u8"Vloženie ortofota", err);
		return false;
	}
	stats.pictures++;
	return true;
}


static GSErrCode CreateMeshOnce (const std::vector<Geo::PointENZ>& boundary, const std::vector<Geo::PointENZ>& interior,
								 API_AttributeIndex layer, const Offset& off)
{
	API_Element element;
	BNZeroMemory (&element, sizeof (element));
	element.header.typeID = API_MeshID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError)
		return err;

	Int32 n = (Int32) boundary.size ();		// closed: last == first
	Int32 m = (Int32) interior.size ();

	element.header.layer = layer;
	element.mesh.level = 0.0;
	element.mesh.poly.nCoords = n;
	element.mesh.poly.nSubPolys = 1;
	element.mesh.poly.nArcs = 0;
	element.mesh.levelLines.nCoords = m;
	element.mesh.levelLines.nSubLines = m;

	API_ElementMemo memo;
	BNZeroMemory (&memo, sizeof (memo));
	memo.coords = reinterpret_cast<API_Coord**> (BMAllocateHandle ((n + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
	memo.pends = reinterpret_cast<Int32**> (BMAllocateHandle (2 * sizeof (Int32), ALLOCATE_CLEAR, 0));
	memo.parcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (0, ALLOCATE_CLEAR, 0));
	memo.meshPolyZ = reinterpret_cast<double**> (BMAllocateHandle ((n + 1) * sizeof (double), ALLOCATE_CLEAR, 0));
	if (m > 0) {
		memo.meshLevelCoords = reinterpret_cast<API_MeshLevelCoord**> (BMAllocateHandle (m * sizeof (API_MeshLevelCoord), ALLOCATE_CLEAR, 0));
		memo.meshLevelEnds = reinterpret_cast<Int32**> (BMAllocateHandle (m * sizeof (Int32), ALLOCATE_CLEAR, 0));
	}
	if (memo.coords == nullptr || memo.pends == nullptr || memo.meshPolyZ == nullptr ||
		(m > 0 && (memo.meshLevelCoords == nullptr || memo.meshLevelEnds == nullptr))) {
		ACAPI_DisposeElemMemoHdls (&memo);
		return APIERR_MEMFULL;
	}

	(*memo.coords)[0].x = -1.0;
	for (Int32 i = 0; i < n; i++) {
		(*memo.coords)[i + 1].x = boundary[(size_t) i].e + off.e;
		(*memo.coords)[i + 1].y = boundary[(size_t) i].n + off.n;
		(*memo.meshPolyZ)[i + 1] = boundary[(size_t) i].z;
	}
	(*memo.pends)[0] = 0;
	(*memo.pends)[1] = n;

	for (Int32 i = 0; i < m; i++) {
		API_MeshLevelCoord& lc = (*memo.meshLevelCoords)[i];
		lc.c.x = interior[(size_t) i].e + off.e;
		lc.c.y = interior[(size_t) i].n + off.n;
		lc.c.z = interior[(size_t) i].z;
		lc.vertexID = n + i + 1;
		(*memo.meshLevelEnds)[i] = i + 1;
	}

	err = ACAPI_Element_Create (&element, &memo);
	ACAPI_DisposeElemMemoHdls (&memo);
	return err;
}


bool PlaceMesh (const std::vector<Geo::PointENZ>& boundary, const std::vector<Geo::PointENZ>& interior,
				const std::string& layer, const Offset& off, Stats& stats)
{
	if (boundary.size () < 4) {
		NoteError (stats, u8"Obrys terénu má príliš málo bodov", APIERR_BADPARS);
		return false;
	}
	API_AttributeIndex layerIdx = EnsureLayer (layer);

	GSErrCode err = CreateMeshOnce (boundary, interior, layerIdx, off);
	if (err == APIERR_IRREGULARPOLY) {
		std::vector<Geo::PointENZ> reversed (boundary.rbegin (), boundary.rend ());
		err = CreateMeshOnce (reversed, interior, layerIdx, off);
	}
	if (err != NoError) {
		Util::Log ("Mesh create failed: " + std::to_string ((int) err) + ", boundary=" + std::to_string (boundary.size ()) +
				   ", interior=" + std::to_string (interior.size ()));
		// last resort: mesh without interior points so the user gets at least the outline
		if (!interior.empty ()) {
			GSErrCode err2 = CreateMeshOnce (boundary, std::vector<Geo::PointENZ> (), layerIdx, off);
			if (err2 == NoError) {
				stats.meshes++;
				NoteError (stats, u8"Mesh vložený bez vnútorných bodov", err);
				return true;
			}
		}
		NoteError (stats, "Vytvorenie meshu", err);
		return false;
	}
	stats.meshes++;
	return true;
}


void PlaceOriginMarker (const Geo::PointEN& origin, const std::string& layer, const Offset& off, double h0, Stats& stats)
{
	API_Element element;
	BNZeroMemory (&element, sizeof (element));
	element.header.typeID = API_HotspotID;
	if (ACAPI_Element_GetDefaults (&element, nullptr) == NoError) {
		element.header.layer = EnsureLayer (layer);
		element.hotspot.pos.x = origin.e + off.e;
		element.hotspot.pos.y = origin.n + off.n;
		element.hotspot.height = 0.0;
		ACAPI_Element_Create (&element, nullptr);
	}
	char buf[200];
	snprintf (buf, sizeof (buf), "Pociatok S-JTSK: E=%.3f N=%.3f (Y=%.3f X=%.3f), 0.00 = %.3f m n.m. Bpv",
			  origin.e, origin.n, -origin.e, -origin.n, h0);
	Geo::PointEN tp (origin.e + 1.0, origin.n + 1.0);
	PlaceText (buf, tp, layer, 1, 2.0, off, stats);
}


// =============================================================================
// Project location
// =============================================================================

bool GetPlace (double& latitude, double& longitude, double& altitude)
{
	API_PlaceInfo info;
	BNZeroMemory (&info, sizeof (info));
	if (ACAPI_Environment (APIEnv_GetPlaceSetsID, &info, nullptr) != NoError)
		return false;
	latitude = info.latitude;
	longitude = info.longitude;
	altitude = info.altitude;
	return true;
}


bool SetPlace (double latitude, double longitude, double altitude)
{
	API_PlaceInfo info;
	BNZeroMemory (&info, sizeof (info));
	if (ACAPI_Environment (APIEnv_GetPlaceSetsID, &info, nullptr) != NoError)
		return false;
	info.latitude = latitude;
	info.longitude = longitude;
	info.altitude = altitude;
	return ACAPI_Environment (APIEnv_ChangePlaceSetsID, &info, nullptr) == NoError;
}

}	// namespace Placer
