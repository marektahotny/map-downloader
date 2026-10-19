#include "Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace TerrainMesh {

Geo::Ring ConvexHull (const std::vector<Geo::PointENZ>& pts)
{
	std::vector<Geo::PointEN> p;
	p.reserve (pts.size ());
	for (const auto& q : pts)
		p.push_back (Geo::PointEN (q.e, q.n));
	std::sort (p.begin (), p.end (), [] (const Geo::PointEN& a, const Geo::PointEN& b) {
		return a.e < b.e || (a.e == b.e && a.n < b.n);
	});
	p.erase (std::unique (p.begin (), p.end (), [] (const Geo::PointEN& a, const Geo::PointEN& b) {
		return a.e == b.e && a.n == b.n;
	}), p.end ());
	if (p.size () < 3)
		return Geo::Ring ();
	auto cross = [] (const Geo::PointEN& o, const Geo::PointEN& a, const Geo::PointEN& b) {
		return (a.e - o.e) * (b.n - o.n) - (a.n - o.n) * (b.e - o.e);
	};
	std::vector<Geo::PointEN> h (2 * p.size ());
	size_t k = 0;
	for (size_t i = 0; i < p.size (); i++) {
		while (k >= 2 && cross (h[k - 2], h[k - 1], p[i]) <= 0)
			k--;
		h[k++] = p[i];
	}
	for (size_t i = p.size () - 1, t = k + 1; i > 0; i--) {
		while (k >= t && cross (h[k - 2], h[k - 1], p[i - 1]) <= 0)
			k--;
		h[k++] = p[i - 1];
	}
	h.resize (k);		// last == first
	return h;
}


static void RemoveDuplicateVertices (Geo::Ring& r)
{
	Geo::Ring out;
	for (const auto& p : r) {
		if (!out.empty () && std::fabs (out.back ().e - p.e) < 1e-4 && std::fabs (out.back ().n - p.n) < 1e-4)
			continue;
		out.push_back (p);
	}
	if (out.size () >= 2 && (out.front ().e != out.back ().e || out.front ().n != out.back ().n))
		out.push_back (out.front ());
	r.swap (out);
}


bool Build (const Input& in, Output& out)
{
	out = Output ();
	if (in.terrain == nullptr || in.terrain->points.empty ()) {
		out.error = "Terén nie je načítaný";
		return false;
	}
	const Geo::Terrain& t = *in.terrain;
	out.sourcePoints = t.points.size ();

	// reduction
	std::vector<size_t> keep = Geo::ReducePoints (t, in.reduction);

	// outline
	Geo::Ring outline = in.polygon;
	if (!in.exactOutline || outline.size () < 4)
		outline = ConvexHull (t.points);
	RemoveDuplicateVertices (outline);
	if (outline.size () < 4) {
		out.error = "Neplatný obrys terénu";
		return false;
	}
	// boundary orientation: counter-clockwise
	if (Geo::SignedArea (outline) < 0.0)
		std::reverse (outline.begin (), outline.end ());

	// densify outline so that the edge follows the terrain
	double g = std::max (0.1, t.spacing);
	int farStep = 1;
	if (in.reduction.enabled) {
		double pct = in.reduction.mode == 0 ? in.reduction.keepPercent : std::min (in.reduction.nearPercent, in.reduction.farPercent);
		pct = std::max (0.01, std::min (100.0, pct));
		farStep = std::max (1, (int) std::floor (std::sqrt (100.0 / pct) + 0.5));
	}
	double edgeSpacing = g * farStep;
	Geo::Ring dense = Geo::DensifyRing (outline, edgeSpacing);
	RemoveDuplicateVertices (dense);

	Geo::HeightIndex index;
	index.Build (t);

	out.minZ = std::numeric_limits<double>::max ();
	out.maxZ = -std::numeric_limits<double>::max ();

	for (size_t i = 0; i < dense.size (); i++) {
		double z = 0.0;
		if (!index.HeightAt (dense[i].e, dense[i].n, z)) {
			// outside of data: nearest any point (slow path, rarely used)
			double best = std::numeric_limits<double>::max ();
			for (const auto& p : t.points) {
				double d = (p.e - dense[i].e) * (p.e - dense[i].e) + (p.n - dense[i].n) * (p.n - dense[i].n);
				if (d < best) {
					best = d;
					z = p.z;
				}
			}
		}
		Geo::PointENZ q;
		q.e = dense[i].e;
		q.n = dense[i].n;
		q.z = z - in.h0;
		out.boundary.push_back (q);
	}
	if (!out.boundary.empty ())
		out.boundary.back () = out.boundary.front ();	// exact closure

	// interior points strictly inside, away from the edge
	double margin = std::max (0.05, g * 0.35);
	for (size_t idx : keep) {
		const Geo::PointENZ& p = t.points[idx];
		if (!Geo::PointInPolygon (dense, p.e, p.n))
			continue;
		if (Geo::DistanceToPolygonEdges (dense, p.e, p.n) < margin)
			continue;
		Geo::PointENZ q = p;
		q.z = p.z - in.h0;
		out.interior.push_back (q);
	}

	for (const auto& p : out.boundary) {
		out.minZ = std::min (out.minZ, p.z);
		out.maxZ = std::max (out.maxZ, p.z);
	}
	for (const auto& p : out.interior) {
		out.minZ = std::min (out.minZ, p.z);
		out.maxZ = std::max (out.maxZ, p.z);
	}
	out.keptPoints = out.interior.size () + out.boundary.size ();
	return true;
}

}	// namespace TerrainMesh
