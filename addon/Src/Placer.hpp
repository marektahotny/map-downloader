#ifndef PILON_PLACER_HPP
#define PILON_PLACER_HPP

// Creates Archicad elements from geodata. All functions must be called inside
// ACAPI_CallUndoableCommand. Coordinates are EPSG:5514 values; 'offset' is added
// (local origin placement: offset = -origin).

#include <string>
#include <vector>

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "GeoData.hpp"

namespace Placer {

	struct Offset {
		double e = 0.0;
		double n = 0.0;
	};

	struct LineStyle {
		std::string	layer;
		short		pen = 1;
		std::string	lineType;		// name; empty = Archicad default (solid)
	};

	struct Stats {
		int lines = 0;
		int texts = 0;
		int pictures = 0;
		int meshes = 0;
		int failed = 0;
		std::string lastError;
	};

	// Target database handling (floor plan of the current story or a worksheet)
	class TargetDatabase {
	public:
		TargetDatabase ();
		~TargetDatabase ();
		GSErrCode SwitchToWorksheet (const GS::UniString& name);		// finds or creates
		GSErrCode SwitchToFloorPlan ();
		void      Restore ();
	private:
		API_DatabaseInfo original;
		bool             changed;
	};

	API_AttributeIndex	EnsureLayer (const std::string& name);
	API_AttributeIndex	FindLineType (const std::string& name);

	void	PlaceRings (const std::vector<Geo::Ring>& rings, const LineStyle& style, const Offset& off, Stats& stats);
	void	PlaceText (const std::string& text, const Geo::PointEN& pos, const std::string& layer, short pen, double sizeMm, const Offset& off, Stats& stats);
	bool	PlacePicture (const std::string& imageBytes, int widthPx, int heightPx, const Geo::BBox& bbox, const std::string& layer,
						  const std::string& name, const Offset& off, Stats& stats);

	// Mesh: boundary ring (closed, EPSG:5514) with heights + interior points. z values are already relative to 0.00.
	bool	PlaceMesh (const std::vector<Geo::PointENZ>& boundary, const std::vector<Geo::PointENZ>& interior,
					   const std::string& layer, const Offset& off, Stats& stats);

	void	PlaceOriginMarker (const Geo::PointEN& origin, const std::string& layer, const Offset& off, double h0, Stats& stats);

	// Project location (Options > Project Preferences > Project Location)
	bool	GetPlace (double& latitude, double& longitude, double& altitude);
	bool	SetPlace (double latitude, double longitude, double altitude);
}

#endif
