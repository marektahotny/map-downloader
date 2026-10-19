#ifndef PILON_GEODATA_HPP
#define PILON_GEODATA_HPP

// Platform independent geodata handling (no Archicad / Windows dependencies),
// so it can be unit-tested on any machine.
//
// Coordinate system everywhere: S-JTSK / Krovak East North (EPSG:5514),
// E = easting (negative in SK/CZ), N = northing (negative).

#include <string>
#include <vector>

namespace Geo {

	struct PointEN {
		double e = 0.0;
		double n = 0.0;
		PointEN () {}
		PointEN (double ee, double nn) : e (ee), n (nn) {}
	};

	struct PointENZ {
		double e = 0.0;
		double n = 0.0;
		double z = 0.0;
	};

	struct BBox {
		double minE = 0.0, minN = 0.0, maxE = 0.0, maxN = 0.0;
		double Width () const	{ return maxE - minE; }
		double Height () const	{ return maxN - minN; }
		bool   IsValid () const	{ return maxE > minE && maxN > minN; }
	};

	typedef std::vector<PointEN> Ring;

	struct Feature {
		std::string			label;			// parcel number etc.
		std::string			id;				// gml:id / inspire id
		std::vector<Ring>	rings;			// exterior + interior rings, closed (first == last)
		bool				hasRefPoint = false;
		PointEN				refPoint;
		double				officialArea = 0.0;	// areaValue from the service (m2), 0 if not present
		std::string			zoning;				// optional extra attribute (land use etc.)
	};

	// --- Geometry helpers ---
	BBox	BoundsOf (const Ring& ring);
	bool	PointInPolygon (const Ring& poly, double e, double n);
	double	DistanceToPolygonEdges (const Ring& poly, double e, double n);
	double	SignedArea (const Ring& ring);
	PointEN	Centroid (const Ring& ring);
	Ring	DensifyRing (const Ring& ring, double maxSegment);	// keeps closed ring closed
	bool	RingFullyInside (const Ring& inner, const Ring& outer);

	// Normalises a coordinate pair coming from an external source to EPSG:5514 (E, N):
	//  - positive JTSK (Y, X) values are negated
	//  - swapped axis order is detected (|E| < |N| always holds in SK/CZ)
	PointEN NormalizeJtsk (double a, double b);

	// Outer boundary of a set of adjacent polygons (e.g. parcels forming a building plot):
	// edges shared by two rings cancel out, remaining edges are chained into closed rings.
	std::vector<Ring> OuterBoundary (const std::vector<Ring>& rings, double tolerance = 0.005);

	// --- OGC request URLs ---
	std::string AppendQuery (const std::string& baseUrl, const std::string& query);
	std::string WmsGetMapUrl (const std::string& baseUrl, const std::string& layers, const std::string& version,
							  const std::string& srs, const std::string& format, const BBox& bbox, int width, int height,
							  const std::string& styles = std::string ());
	std::string WfsGetFeatureUrl (const std::string& baseUrl, const std::string& typeName, const std::string& srsName,
								  const BBox& bbox, int count);
	std::string WcsGetCoverageUrl (const std::string& baseUrl, const std::string& coverageId, const std::string& format,
								   const BBox& bbox, const std::string& crsAxisE, const std::string& crsAxisN, const std::string& subsettingCrs);
	std::string ArcGisExportImageUrl (const std::string& baseUrl, const BBox& bbox, int width, int height, int sr);

	// --- Parsers ---
	// GML 2/3 (WFS GetFeature). labelTags = comma separated local tag names searched for the label.
	bool ParseGmlFeatures (const std::string& gml, const std::string& labelTags, std::vector<Feature>& out, std::string& error);

	// Extracts <Name>, FeatureType names, CoverageIds from a capabilities document (for diagnostics)
	std::vector<std::string> ExtractCapabilityNames (const std::string& xml, size_t maxCount = 40);

	// Exception report / error message inside an OGC XML response (empty if none)
	std::string ExtractOgcException (const std::string& body);

	// --- Terrain ---
	struct Terrain {
		std::vector<PointENZ>	points;
		double					spacing = 1.0;		// estimated base spacing [m]
		bool					regularGrid = false;
		BBox					bounds;
		std::string				source;
	};

	// Raster (GeoTIFF uncompressed / ArcInfo ASCII grid) -> points.
	// fallbackBox is used when the raster has no georeference (pixel-is-area).
	bool ParseGeoTiff (const std::string& data, const BBox& fallbackBox, Terrain& out, std::string& error);
	bool ParseArcGrid (const std::string& text, Terrain& out, std::string& error);
	bool ParseXyz (const std::string& text, Terrain& out, std::string& error);
	bool ParseTerrainAuto (const std::string& data, const BBox& fallbackBox, Terrain& out, std::string& error);

	void FinalizeTerrain (Terrain& t);		// computes bounds + spacing
	void ClipTerrain (Terrain& t, const Ring& polygon, double margin);

	// Spatial index for height lookup (inverse distance weighting from nearest points)
	class HeightIndex {
	public:
		void	Build (const Terrain& t);
		bool	HeightAt (double e, double n, double& z) const;
	private:
		double cell = 1.0;
		double originE = 0.0, originN = 0.0;
		int nx = 0, ny = 0;
		std::vector<std::vector<int>> buckets;
		const Terrain* terrain = nullptr;
	};

	// --- Point reduction ---
	struct ReductionParams {
		bool	enabled = true;
		int		mode = 0;				// 0 = uniform, 1 = density decreasing with distance from origin
		double	keepPercent = 25.0;		// uniform
		double	nearPercent = 100.0;	// distance mode: density near origin
		double	farPercent = 6.0;		// distance mode: density at the farthest point
		PointEN	origin;
	};

	// Returns indexes of kept points
	std::vector<size_t> ReducePoints (const Terrain& t, const ReductionParams& p);

	// --- Export ---
	std::string ToXyzText (const std::vector<PointENZ>& pts, double offsetE, double offsetN, double offsetZ, int decimals);
	std::string WorldFile (const BBox& bbox, int width, int height, double offsetE, double offsetN);
}

#endif
