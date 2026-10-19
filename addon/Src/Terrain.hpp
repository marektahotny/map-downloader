#ifndef PILON_TERRAIN_HPP
#define PILON_TERRAIN_HPP

// Prepares a terrain mesh (boundary with heights + interior points) from a point
// cloud / grid, the selection polygon and the reduction settings. Platform independent.

#include <string>
#include <vector>

#include "GeoData.hpp"

namespace TerrainMesh {

	struct Input {
		const Geo::Terrain*		terrain = nullptr;
		Geo::Ring				polygon;				// selection (closed)
		bool					exactOutline = true;	// boundary = selection polygon; otherwise bounding hull of points
		Geo::ReductionParams	reduction;
		double					h0 = 0.0;				// height of model 0.00 [m n.m. Bpv]
	};

	struct Output {
		std::vector<Geo::PointENZ>	boundary;			// closed ring, z relative to h0
		std::vector<Geo::PointENZ>	interior;			// z relative to h0
		size_t						sourcePoints = 0;
		size_t						keptPoints = 0;
		double						minZ = 0.0, maxZ = 0.0;
		std::string					error;
	};

	bool Build (const Input& in, Output& out);

	// Convex hull (used when exactOutline == false)
	Geo::Ring ConvexHull (const std::vector<Geo::PointENZ>& pts);
}

#endif
