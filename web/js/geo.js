/*
 * Pilon Map Downloader – geometrické jadro (prehliadač aj Node.js)
 * S-JTSK / Krovak East North (EPSG:5514): E aj N sú záporné, E rastie na východ, N na sever.
 */
(function (root) {
	'use strict';

	var Geo = {};

	Geo.EPSG5514 = '+proj=krovak +lat_0=49.5 +lon_0=24.83333333333333 +alpha=30.28813972222222 +k=0.9999 ' +
		'+x_0=0 +y_0=0 +ellps=bessel +towgs84=485.021,169.465,483.839,7.786342,4.397554,4.102655,0 +units=m +no_defs';

	// ---------------------------------------------------------------------------
	// Základná geometria (body {e,n} alebo [e,n])
	// ---------------------------------------------------------------------------
	function E(p) { return p.e !== undefined ? p.e : p[0]; }
	function N(p) { return p.n !== undefined ? p.n : p[1]; }
	Geo.E = E;
	Geo.N = N;

	Geo.bounds = function (pts) {
		var b = { minE: Infinity, minN: Infinity, maxE: -Infinity, maxN: -Infinity };
		for (var i = 0; i < pts.length; i++) {
			var e = E(pts[i]), n = N(pts[i]);
			if (e < b.minE) b.minE = e;
			if (e > b.maxE) b.maxE = e;
			if (n < b.minN) b.minN = n;
			if (n > b.maxN) b.maxN = n;
		}
		return b;
	};

	Geo.boundsUnion = function (a, b) {
		if (!a) return b;
		if (!b) return a;
		return { minE: Math.min(a.minE, b.minE), minN: Math.min(a.minN, b.minN), maxE: Math.max(a.maxE, b.maxE), maxN: Math.max(a.maxN, b.maxN) };
	};

	Geo.expand = function (b, m) {
		return { minE: b.minE - m, minN: b.minN - m, maxE: b.maxE + m, maxN: b.maxN + m };
	};

	Geo.bboxRing = function (b) {
		return [[b.minE, b.minN], [b.maxE, b.minN], [b.maxE, b.maxN], [b.minE, b.maxN], [b.minE, b.minN]];
	};

	Geo.pointInPolygon = function (poly, e, n) {
		var inside = false;
		for (var i = 0, j = poly.length - 1; i < poly.length; j = i++) {
			var ei = E(poly[i]), ni = N(poly[i]), ej = E(poly[j]), nj = N(poly[j]);
			if (((ni > n) !== (nj > n)) && (e < (ej - ei) * (n - ni) / (nj - ni) + ei))
				inside = !inside;
		}
		return inside;
	};

	function segDist(ae, an, be, bn, e, n) {
		var dx = be - ae, dy = bn - an;
		var l2 = dx * dx + dy * dy;
		var t = l2 > 0 ? ((e - ae) * dx + (n - an) * dy) / l2 : 0;
		t = Math.max(0, Math.min(1, t));
		var px = ae + t * dx - e, py = an + t * dy - n;
		return Math.sqrt(px * px + py * py);
	}

	Geo.distanceToEdges = function (poly, e, n) {
		var d = Infinity;
		for (var i = 0; i + 1 < poly.length; i++)
			d = Math.min(d, segDist(E(poly[i]), N(poly[i]), E(poly[i + 1]), N(poly[i + 1]), e, n));
		return d;
	};

	Geo.signedArea = function (ring) {
		var a = 0;
		for (var i = 0; i + 1 < ring.length; i++)
			a += E(ring[i]) * N(ring[i + 1]) - E(ring[i + 1]) * N(ring[i]);
		return a / 2;
	};

	Geo.area = function (ring) { return Math.abs(Geo.signedArea(ring)); };

	Geo.length = function (path) {
		var l = 0;
		for (var i = 0; i + 1 < path.length; i++)
			l += Math.hypot(E(path[i + 1]) - E(path[i]), N(path[i + 1]) - N(path[i]));
		return l;
	};

	Geo.centroid = function (ring) {
		var a = 0, cx = 0, cy = 0;
		var e0 = E(ring[0]), n0 = N(ring[0]);
		for (var i = 0; i + 1 < ring.length; i++) {
			var x0 = E(ring[i]) - e0, y0 = N(ring[i]) - n0, x1 = E(ring[i + 1]) - e0, y1 = N(ring[i + 1]) - n0;
			var f = x0 * y1 - x1 * y0;
			a += f;
			cx += (x0 + x1) * f;
			cy += (y0 + y1) * f;
		}
		if (Math.abs(a) < 1e-9) {
			var b = Geo.bounds(ring);
			return [(b.minE + b.maxE) / 2, (b.minN + b.maxN) / 2];
		}
		return [e0 + cx / (3 * a), n0 + cy / (3 * a)];
	};

	// Bod vnútri polygónu vhodný na popis (ťažisko, ak leží vnútri, inak najlepší bod z mriežky)
	Geo.labelPoint = function (ring) {
		var c = Geo.centroid(ring);
		if (Geo.pointInPolygon(ring, c[0], c[1])) return c;
		var b = Geo.bounds(ring), best = null, bestD = -1;
		for (var i = 1; i < 12; i++) {
			for (var j = 1; j < 12; j++) {
				var e = b.minE + (b.maxE - b.minE) * i / 12, n = b.minN + (b.maxN - b.minN) * j / 12;
				if (!Geo.pointInPolygon(ring, e, n)) continue;
				var d = Geo.distanceToEdges(ring, e, n);
				if (d > bestD) { bestD = d; best = [e, n]; }
			}
		}
		return best || c;
	};

	Geo.ringFullyInside = function (inner, outer) {
		for (var i = 0; i < inner.length; i++)
			if (!Geo.pointInPolygon(outer, E(inner[i]), N(inner[i])) && Geo.distanceToEdges(outer, E(inner[i]), N(inner[i])) > 0.05)
				return false;
		return true;
	};

	Geo.closeRing = function (r) {
		if (r.length && (E(r[0]) !== E(r[r.length - 1]) || N(r[0]) !== N(r[r.length - 1])))
			r = r.concat([[E(r[0]), N(r[0])]]);
		return r;
	};

	// Súradnice z rôznych zápisov S-JTSK na EPSG:5514 (E, N záporné, |E| < |N|)
	Geo.normalizeJtsk = function (a, b) {
		if (a > 0 && b > 0) { a = -a; b = -b; }
		if (Math.abs(a) > Math.abs(b)) { var t = a; a = b; b = t; }
		return [a, b];
	};

	// Vonkajšia hranica zjednotenia susediacich parciel (zrušia sa spoločné hrany)
	Geo.outerBoundary = function (rings, tol) {
		tol = tol || 0.01;
		function key(p) { return Math.round(E(p) / tol) + ',' + Math.round(N(p) / tol); }
		var coords = {}, count = {}, directed = [];
		rings.forEach(function (r) {
			for (var i = 0; i + 1 < r.length; i++) {
				var a = key(r[i]), b = key(r[i + 1]);
				if (a === b) continue;
				coords[a] = [E(r[i]), N(r[i])];
				coords[b] = [E(r[i + 1]), N(r[i + 1])];
				var ek = a < b ? a + '|' + b : b + '|' + a;
				count[ek] = (count[ek] || 0) + 1;
				directed.push([a, b]);
			}
		});
		var next = {}, remaining = 0;
		directed.forEach(function (d) {
			var ek = d[0] < d[1] ? d[0] + '|' + d[1] : d[1] + '|' + d[0];
			if (count[ek] === 1) {
				(next[d[0]] = next[d[0]] || []).push(d[1]);
				remaining++;
			}
		});
		var out = [];
		while (remaining > 0) {
			var start = null;
			for (var k in next) if (next[k].length) { start = k; break; }
			if (start === null) break;
			var ring = [coords[start]], cur = start, guard = 0;
			while (guard++ < 1000000) {
				var list = next[cur];
				if (!list || !list.length) break;
				var nx = list.pop();
				remaining--;
				ring.push(coords[nx]);
				cur = nx;
				if (cur === start) break;
			}
			if (ring.length >= 4) out.push(dropCollinear(ring, tol));
		}
		out.sort(function (a, b) { return Geo.area(b) - Geo.area(a); });
		return out;
	};

	// odstráni vrcholy ležiace na priamke medzi susedmi (uzavretý prstenec)
	function dropCollinear(ring, tol) {
		var pts = ring.slice(0, ring.length - 1), changed = true;
		while (changed && pts.length > 3) {
			changed = false;
			for (var i = 0; i < pts.length && pts.length > 3; i++) {
				var a = pts[(i + pts.length - 1) % pts.length], b = pts[i], c = pts[(i + 1) % pts.length];
				if (segDist(a[0], a[1], c[0], c[1], b[0], b[1]) <= tol) { pts.splice(i, 1); changed = true; i--; }
			}
		}
		return pts.concat([pts[0]]);
	}

	Geo.ringsTouch = function (a, b, tol) {
		var ba = Geo.bounds(a), bb = Geo.bounds(b);
		if (ba.maxE + tol < bb.minE || bb.maxE + tol < ba.minE || ba.maxN + tol < bb.minN || bb.maxN + tol < ba.minN) return false;
		var i;
		for (i = 0; i < a.length; i++) if (Geo.distanceToEdges(b, E(a[i]), N(a[i])) <= tol) return true;
		for (i = 0; i < b.length; i++) if (Geo.distanceToEdges(a, E(b[i]), N(b[i])) <= tol) return true;
		return false;
	};

	// ---------------------------------------------------------------------------
	// Orezanie línií polygónom územia
	// ---------------------------------------------------------------------------
	function segIntersectT(ax, ay, bx, by, cx, cy, dx, dy) {
		var rx = bx - ax, ry = by - ay, sx = dx - cx, sy = dy - cy;
		var den = rx * sy - ry * sx;
		if (Math.abs(den) < 1e-12) return -1;
		var t = ((cx - ax) * sy - (cy - ay) * sx) / den;
		var u = ((cx - ax) * ry - (cy - ay) * rx) / den;
		if (t > 1e-12 && t < 1 - 1e-12 && u >= -1e-12 && u <= 1 + 1e-12) return t;
		return -1;
	}

	// path: [[e,n],...]; vracia zoznam častí ležiacich vnútri polygónu
	Geo.clipPath = function (path, poly, polyBox) {
		polyBox = polyBox || Geo.bounds(poly);
		var pb = Geo.bounds(path);
		if (pb.maxE < polyBox.minE || pb.minE > polyBox.maxE || pb.maxN < polyBox.minN || pb.minN > polyBox.maxN) return [];
		// celá línia vnútri obdĺžnika obálky a vnútri polygónu – rýchla cesta
		var out = [], cur = null;
		function push(p) {
			if (!cur) { cur = [p]; return; }
			var l = cur[cur.length - 1];
			if (Math.abs(l[0] - p[0]) > 1e-9 || Math.abs(l[1] - p[1]) > 1e-9) cur.push(p);
		}
		function flush() { if (cur && cur.length >= 2) out.push(cur); cur = null; }
		for (var i = 0; i + 1 < path.length; i++) {
			var ax = E(path[i]), ay = N(path[i]), bx = E(path[i + 1]), by = N(path[i + 1]);
			var ts = [0, 1];
			var sMinE = Math.min(ax, bx), sMaxE = Math.max(ax, bx), sMinN = Math.min(ay, by), sMaxN = Math.max(ay, by);
			if (!(sMaxE < polyBox.minE || sMinE > polyBox.maxE || sMaxN < polyBox.minN || sMinN > polyBox.maxN)) {
				for (var j = 0; j + 1 < poly.length; j++) {
					var t = segIntersectT(ax, ay, bx, by, E(poly[j]), N(poly[j]), E(poly[j + 1]), N(poly[j + 1]));
					if (t > 0) ts.push(t);
				}
			}
			ts.sort(function (x, y) { return x - y; });
			for (var k = 0; k + 1 < ts.length; k++) {
				var t0 = ts[k], t1 = ts[k + 1];
				if (t1 - t0 < 1e-12) continue;
				var tm = (t0 + t1) / 2;
				var inside = Geo.pointInPolygon(poly, ax + (bx - ax) * tm, ay + (by - ay) * tm);
				if (inside) {
					push([ax + (bx - ax) * t0, ay + (by - ay) * t0]);
					push([ax + (bx - ax) * t1, ay + (by - ay) * t1]);
				} else {
					flush();
				}
			}
		}
		flush();
		return out;
	};

	// ---------------------------------------------------------------------------
	// GML (WFS 1.1 / 2.0, INSPIRE CP, budovy) – bez DOM, funguje aj v Node.js
	// ---------------------------------------------------------------------------
	function decodeEntities(s) {
		return s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&apos;/g, "'")
			.replace(/&#(\d+);/g, function (m, d) { return String.fromCharCode(+d); })
			.replace(/&#x([0-9a-f]+);/gi, function (m, h) { return String.fromCharCode(parseInt(h, 16)); })
			.replace(/&amp;/g, '&');
	}

	function numbers(s) {
		var parts = s.trim().split(/[\s,]+/), out = [];
		for (var i = 0; i < parts.length; i++) {
			if (parts[i] === '') continue;
			var v = parseFloat(parts[i]);
			if (!isNaN(v)) out.push(v);
		}
		return out;
	}

	function ringFromNumbers(nums, dim) {
		var r = [];
		dim = dim > 0 ? dim : 2;
		for (var i = 0; i + 1 < nums.length; i += dim)
			r.push(Geo.normalizeJtsk(nums[i], nums[i + 1]));
		return r;
	}

	Geo.ogcException = function (text) {
		if (!text || text.length > 200000) return '';
		var m = /<(?:[\w-]+:)?ExceptionText[^>]*>([\s\S]*?)<\/(?:[\w-]+:)?ExceptionText>/.exec(text) ||
			/<(?:[\w-]+:)?ServiceException[^>]*>([\s\S]*?)<\/(?:[\w-]+:)?ServiceException>/.exec(text);
		return m ? decodeEntities(m[1]).trim() : '';
	};

	Geo.parseGml = function (gml, labelTags) {
		var ex = Geo.ogcException(gml);
		if (ex) throw new Error(ex);
		labelTags = labelTags || ['label', 'text', 'nationalCadastralReference'];
		var features = [];
		var reMember = /<((?:[\w-]+:)?(?:member|featureMember))\b[^>]*>([\s\S]*?)<\/\1>/g;
		var m, any = false;
		while ((m = reMember.exec(gml)) !== null) {
			any = true;
			var body = m[2];
			var f = { id: '', label: '', rings: [], officialArea: 0, refPoint: null, props: {} };
			var idm = /<[\w-]+:[\w-]+\b[^>]*?\bgml:id="([^"]*)"/.exec(body) || /\bid="([^"]*)"/.exec(body);
			if (idm) f.id = idm[1];
			for (var i = 0; i < labelTags.length; i++) {
				var lm = new RegExp('<(?:[\\w-]+:)?' + labelTags[i] + '\\b[^>/]*>([^<]*)</', '').exec(body);
				if (lm && lm[1].trim()) { f.label = decodeEntities(lm[1]).trim(); break; }
			}
			var am = /<(?:[\w-]+:)?areaValue\b[^>]*>([^<]*)</.exec(body);
			if (am) f.officialArea = parseFloat(am[1]) || 0;
			var rp = /<(?:[\w-]+:)?referencePoint\b[^>]*>[\s\S]*?<(?:[\w-]+:)?pos\b[^>]*>([^<]*)</.exec(body);
			if (rp) {
				var rn = numbers(rp[1]);
				if (rn.length >= 2) f.refPoint = Geo.normalizeJtsk(rn[0], rn[1]);
			}
			// jednoduché atribúty (napr. budovy: počet podlaží, typ)
			var reProp = /<(?:[\w-]+:)?([\w-]+)\b[^>/]*>([^<]{1,120})<\/(?:[\w-]+:)?\1>/g, pm;
			while ((pm = reProp.exec(body)) !== null) {
				if (/^(posList|pos|coordinates|lowerCorner|upperCorner)$/.test(pm[1])) continue;
				if (!(pm[1] in f.props)) f.props[pm[1]] = decodeEntities(pm[2]).trim();
			}
			var rePos = /<(?:[\w-]+:)?posList\b([^>]*)>([^<]*)</g, pp;
			while ((pp = rePos.exec(body)) !== null) {
				var dm = /srsDimension="(\d)"/.exec(pp[1]);
				var r = ringFromNumbers(numbers(pp[2]), dm ? +dm[1] : 2);
				if (r.length >= 2) f.rings.push(r);
			}
			var reCoord = /<(?:[\w-]+:)?coordinates\b[^>]*>([^<]*)</g, cc;
			while ((cc = reCoord.exec(body)) !== null) {
				var r2 = ringFromNumbers(numbers(cc[1]), 2);
				if (r2.length >= 2) f.rings.push(r2);
			}
			if (f.rings.length) features.push(f);
		}
		if (!any && !/FeatureCollection/.test(gml))
			throw new Error('Odpoveď neobsahuje GML prvky: ' + gml.substr(0, 200));
		return features;
	};

	Geo.capabilityNames = function (xml) {
		var out = [], re = /<(?:[\w-]+:)?FeatureType\b[^>]*>[\s\S]*?<(?:[\w-]+:)?Name>([^<]+)<\/(?:[\w-]+:)?Name>/g, m;
		while ((m = re.exec(xml)) !== null) out.push(m[1].trim());
		return out;
	};

	// ---------------------------------------------------------------------------
	// Terén
	// ---------------------------------------------------------------------------
	// Terén = { e, n, z: Float64Array, count, bounds, spacing, grid: {e0, n0, res, cols, rows, idx: Int32Array} | null }
	Geo.makeTerrain = function (e, n, z, count, source) {
		count = count === undefined ? z.length : count;
		var t = { e: e, n: n, z: z, count: count, source: source || '', grid: null, spacing: 1 };
		var b = { minE: Infinity, minN: Infinity, maxE: -Infinity, maxN: -Infinity }, minZ = Infinity, maxZ = -Infinity;
		for (var i = 0; i < count; i++) {
			if (e[i] < b.minE) b.minE = e[i];
			if (e[i] > b.maxE) b.maxE = e[i];
			if (n[i] < b.minN) b.minN = n[i];
			if (n[i] > b.maxN) b.maxN = n[i];
			if (z[i] < minZ) minZ = z[i];
			if (z[i] > maxZ) maxZ = z[i];
		}
		t.bounds = b;
		t.minZ = minZ;
		t.maxZ = maxZ;
		if (count < 2) return t;
		// rozostup: najmenší kladný rozdiel medzi susednými E vo vzorke
		var sample = [], step = Math.max(1, Math.floor(count / 20000));
		for (i = 0; i < count; i += step) sample.push(e[i]);
		sample.sort(function (a, c) { return a - c; });
		var res = Infinity;
		for (i = 1; i < sample.length; i++) {
			var d = sample[i] - sample[i - 1];
			if (d > 1e-4 && d < res) res = d;
		}
		if (!isFinite(res)) res = Math.sqrt(((b.maxE - b.minE) * (b.maxN - b.minN)) / count) || 1;
		// zaokrúhliť na rozumnú hodnotu (0,01 m)
		res = Math.round(res * 100) / 100 || res;
		t.spacing = res;
		var cols = Math.round((b.maxE - b.minE) / res) + 1, rows = Math.round((b.maxN - b.minN) / res) + 1;
		if (cols * rows > Math.max(4 * count, 1000) || cols * rows > 60000000) return t;
		var idx = new Int32Array(cols * rows).fill(-1), ok = true;
		for (i = 0; i < count; i++) {
			var fx = (e[i] - b.minE) / res, fy = (n[i] - b.minN) / res;
			var ix = Math.round(fx), iy = Math.round(fy);
			if (Math.abs(fx - ix) > 0.02 || Math.abs(fy - iy) > 0.02) { ok = false; break; }
			idx[iy * cols + ix] = i;
		}
		if (ok) t.grid = { e0: b.minE, n0: b.minN, res: res, cols: cols, rows: rows, idx: idx };
		return t;
	};

	Geo.terrainFromGrid = function (g, source) {
		// g = { e0 (stred ľavého stĺpca), n0Top (stred horného riadku), res, cols, rows, values (riadky zhora), noData }
		var n = g.cols * g.rows, E = new Float64Array(n), N = new Float64Array(n), Z = new Float64Array(n), c = 0;
		for (var r = 0; r < g.rows; r++) {
			for (var k = 0; k < g.cols; k++) {
				var v = g.values[r * g.cols + k];
				if (!isFinite(v) || v === g.noData || v < -1000 || v > 9000) continue;
				var q = Geo.normalizeJtsk(g.e0 + k * g.resE, g.n0Top - r * g.resN);
				E[c] = q[0];
				N[c] = q[1];
				Z[c] = v;
				c++;
			}
		}
		return Geo.makeTerrain(E, N, Z, c, source);
	};

	Geo.parseArcGrid = function (text, source) {
		var lines = text.split(/\r?\n/), hdr = {}, li = 0;
		for (; li < lines.length; li++) {
			var m = /^\s*([a-zA-Z_]+)\s+([-+0-9.eE]+)\s*$/.exec(lines[li]);
			if (!m) break;
			hdr[m[1].toLowerCase()] = parseFloat(m[2]);
		}
		if (!hdr.ncols || !hdr.nrows || !hdr.cellsize) throw new Error('Neplatná hlavička ASC');
		var cs = hdr.cellsize, cols = hdr.ncols, rows = hdr.nrows;
		var x0 = hdr.xllcenter !== undefined ? hdr.xllcenter : hdr.xllcorner + cs / 2;
		var y0 = hdr.yllcenter !== undefined ? hdr.yllcenter : hdr.yllcorner + cs / 2;
		var vals = new Float64Array(cols * rows), c = 0;
		var rest = lines.slice(li).join(' ').trim().split(/\s+/);
		for (var i = 0; i < rest.length && c < vals.length; i++) if (rest[i] !== '') vals[c++] = parseFloat(rest[i]);
		return Geo.terrainFromGrid({ e0: x0, n0Top: y0 + (rows - 1) * cs, resE: cs, resN: cs, cols: cols, rows: rows, values: vals, noData: hdr.nodata_value }, source);
	};

	Geo.parseXyz = function (text, source) {
		var lines = text.split(/\r?\n/), n = lines.length;
		var E = new Float64Array(n), N = new Float64Array(n), Z = new Float64Array(n), c = 0;
		for (var i = 0; i < n; i++) {
			var s = lines[i].trim();
			if (!s || s[0] === '#') continue;
			var p = s.split(/[\s;,]+/);
			if (p.length < 3) continue;
			var a = parseFloat(p[0]), b = parseFloat(p[1]), z = parseFloat(p[2]);
			if (isNaN(a) || isNaN(b) || isNaN(z)) {
				if (p.length >= 4) { a = parseFloat(p[1]); b = parseFloat(p[2]); z = parseFloat(p[3]); }
				if (isNaN(a) || isNaN(b) || isNaN(z)) continue;
			}
			var q = Geo.normalizeJtsk(a, b);
			E[c] = q[0]; N[c] = q[1]; Z[c] = z; c++;
		}
		if (!c) throw new Error('V súbore nie sú body X Y Z');
		return Geo.makeTerrain(E, N, Z, c, source);
	};

	Geo.mergeTerrains = function (list) {
		if (list.length === 1) return list[0];
		var total = 0;
		list.forEach(function (t) { total += t.count; });
		var E = new Float64Array(total), N = new Float64Array(total), Z = new Float64Array(total), c = 0, seen = {};
		list.forEach(function (t) {
			for (var i = 0; i < t.count; i++) {
				var k = Math.round(t.e[i] * 100) + ',' + Math.round(t.n[i] * 100);
				if (seen[k]) continue;
				seen[k] = 1;
				E[c] = t.e[i]; N[c] = t.n[i]; Z[c] = t.z[i]; c++;
			}
		});
		return Geo.makeTerrain(E, N, Z, c, list.map(function (t) { return t.source; }).join(' + '));
	};

	Geo.clipTerrain = function (t, poly, margin) {
		margin = margin || 0;
		var b = Geo.expand(Geo.bounds(poly), margin);
		var E = new Float64Array(t.count), N = new Float64Array(t.count), Z = new Float64Array(t.count), c = 0;
		for (var i = 0; i < t.count; i++) {
			var e = t.e[i], n = t.n[i];
			if (e < b.minE || e > b.maxE || n < b.minN || n > b.maxN) continue;
			if (!Geo.pointInPolygon(poly, e, n) && Geo.distanceToEdges(poly, e, n) > Math.max(margin, 1e-6)) continue;
			E[c] = e; N[c] = n; Z[c] = t.z[i]; c++;
		}
		return Geo.makeTerrain(E, N, Z, c, t.source);
	};

	// Výška v bode (z mriežky bilineárne, inak najbližší bod v okolí)
	Geo.heightAt = function (t, e, n) {
		if (t.grid) {
			var g = t.grid, fx = (e - g.e0) / g.res, fy = (n - g.n0) / g.res;
			var ix = Math.floor(fx), iy = Math.floor(fy);
			var wsum = 0, zsum = 0;
			for (var dy = 0; dy <= 1; dy++) for (var dx = 0; dx <= 1; dx++) {
				var x = ix + dx, y = iy + dy;
				if (x < 0 || y < 0 || x >= g.cols || y >= g.rows) continue;
				var k = g.idx[y * g.cols + x];
				if (k < 0) continue;
				var w = (dx ? fx - ix : 1 - (fx - ix)) * (dy ? fy - iy : 1 - (fy - iy));
				wsum += w; zsum += w * t.z[k];
			}
			if (wsum > 1e-12) return zsum / wsum;
		}
		var best = Infinity, bz = NaN;
		for (var i = 0; i < t.count; i++) {
			var d = (t.e[i] - e) * (t.e[i] - e) + (t.n[i] - n) * (t.n[i] - n);
			if (d < best) { best = d; bz = t.z[i]; }
		}
		return bz;
	};

	// ---------------------------------------------------------------------------
	// Redukcia bodov (rovnomerná / klesajúca hustota od počiatku)
	// ---------------------------------------------------------------------------
	function stepForPercent(p) {
		p = Math.max(0.01, Math.min(100, p));
		return Math.max(1, Math.round(Math.sqrt(100 / p)));
	}

	Geo.reducePoints = function (t, p) {
		var keep = [], i;
		if (!t.count) return keep;
		if (!p || !p.enabled) {
			for (i = 0; i < t.count; i++) keep.push(i);
			return keep;
		}
		var g = Math.max(1e-3, t.spacing), e0 = t.bounds.minE, n0 = t.bounds.minN;
		if (p.mode === 0) {
			var step = stepForPercent(p.keepPercent);
			if (t.grid) {
				for (i = 0; i < t.count; i++) {
					var ix = Math.round((t.e[i] - e0) / g), iy = Math.round((t.n[i] - n0) / g);
					if (ix % step === 0 && iy % step === 0) keep.push(i);
				}
			} else {
				var bsz = step * g, best = {};
				for (i = 0; i < t.count; i++) {
					var key = Math.floor((t.e[i] - e0) / bsz) + ',' + Math.floor((t.n[i] - n0) / bsz);
					if (!(key in best)) best[key] = i;
				}
				for (var k in best) keep.push(best[k]);
				keep.sort(function (a, b) { return a - b; });
			}
			return keep;
		}
		var maxD = 0, b = t.bounds, o = p.origin;
		[b.minE, b.maxE].forEach(function (ce) { [b.minN, b.maxN].forEach(function (cn) { maxD = Math.max(maxD, Math.hypot(ce - o[0], cn - o[1])); }); });
		if (maxD <= 0) maxD = 1;
		for (i = 0; i < t.count; i++) {
			var d = Math.hypot(t.e[i] - o[0], t.n[i] - o[1]);
			var tt = Math.min(1, d / maxD);
			var s = stepForPercent(p.nearPercent + (p.farPercent - p.nearPercent) * tt);
			var pow2 = 1;
			while (pow2 * 2 <= s) pow2 *= 2;
			if (s - pow2 > pow2 * 2 - s) pow2 *= 2;
			var jx = Math.round((t.e[i] - e0) / g), jy = Math.round((t.n[i] - n0) / g);
			if (jx % pow2 === 0 && jy % pow2 === 0) keep.push(i);
		}
		return keep;
	};

	// ---------------------------------------------------------------------------
	// Vrstevnice (marching squares na mriežke)
	// ---------------------------------------------------------------------------
	Geo.contours = function (t, interval, opts) {
		opts = opts || {};
		var g = t.grid;
		if (!g || !(interval > 0)) return [];
		var cols = g.cols, rows = g.rows, idx = g.idx, Z = t.z;
		function zAt(x, y) { var k = idx[y * cols + x]; return k < 0 ? NaN : Z[k]; }
		var segsByLevel = {};
		function edgePoint(x0, y0, z0, x1, y1, z1, lv) {
			var f = (lv - z0) / (z1 - z0);
			return [g.e0 + (x0 + (x1 - x0) * f) * g.res, g.n0 + (y0 + (y1 - y0) * f) * g.res];
		}
		for (var y = 0; y + 1 < rows; y++) {
			for (var x = 0; x + 1 < cols; x++) {
				var a = zAt(x, y), b = zAt(x + 1, y), c = zAt(x + 1, y + 1), d = zAt(x, y + 1);
				if (isNaN(a) || isNaN(b) || isNaN(c) || isNaN(d)) continue;
				var lo = Math.min(a, b, c, d), hi = Math.max(a, b, c, d);
				var l0 = Math.ceil(lo / interval) * interval;
				for (var lv = l0; lv <= hi; lv += interval) {
					if (lv === hi && lv === lo) continue;
					// rohy nad úrovňou (pri rovnosti posun o epsilon)
					var e = 1e-9, A = a === lv ? a + e : a, B = b === lv ? b + e : b, C = c === lv ? c + e : c, D = d === lv ? d + e : d;
					var code = (A > lv ? 1 : 0) | (B > lv ? 2 : 0) | (C > lv ? 4 : 0) | (D > lv ? 8 : 0);
					if (code === 0 || code === 15) continue;
					// hrany: 0 dolná (a-b), 1 pravá (b-c), 2 horná (d-c), 3 ľavá (a-d)
					var ek = [
						'h' + x + ',' + y, 'v' + (x + 1) + ',' + y, 'h' + x + ',' + (y + 1), 'v' + x + ',' + y
					];
					var pts = [
						function () { return edgePoint(x, y, A, x + 1, y, B, lv); },
						function () { return edgePoint(x + 1, y, B, x + 1, y + 1, C, lv); },
						function () { return edgePoint(x, y + 1, D, x + 1, y + 1, C, lv); },
						function () { return edgePoint(x, y, A, x, y + 1, D, lv); }
					];
					var pairs;
					switch (code) {
						case 1: case 14: pairs = [[0, 3]]; break;
						case 2: case 13: pairs = [[0, 1]]; break;
						case 3: case 12: pairs = [[1, 3]]; break;
						case 4: case 11: pairs = [[1, 2]]; break;
						case 6: case 9: pairs = [[0, 2]]; break;
						case 7: case 8: pairs = [[2, 3]]; break;
						case 5: case 10:
							var center = (A + B + C + D) / 4;
							if ((center > lv) === (code === 5)) pairs = [[0, 1], [2, 3]];
							else pairs = [[0, 3], [1, 2]];
							break;
					}
					var key = lv.toFixed(4);
					var list = segsByLevel[key] || (segsByLevel[key] = { level: lv, segs: [] });
					pairs.forEach(function (pr) {
						list.segs.push({ k0: ek[pr[0]], k1: ek[pr[1]], p0: pts[pr[0]](), p1: pts[pr[1]]() });
					});
				}
			}
		}
		var out = [];
		Object.keys(segsByLevel).forEach(function (key) {
			var L = segsByLevel[key], byKey = {};
			L.segs.forEach(function (s, i) {
				(byKey[s.k0] = byKey[s.k0] || []).push(i);
				(byKey[s.k1] = byKey[s.k1] || []).push(i);
			});
			var used = new Uint8Array(L.segs.length);
			function other(s, k) { return s.k0 === k ? { k: s.k1, p: s.p1 } : { k: s.k0, p: s.p0 }; }
			function walk(startKey, line) {
				var k = startKey;
				while (true) {
					var cand = (byKey[k] || []).filter(function (i) { return !used[i]; });
					if (!cand.length) break;
					var si = cand[0];
					used[si] = 1;
					var o = other(L.segs[si], k);
					line.push(o.p);
					k = o.k;
				}
				return line;
			}
			for (var i = 0; i < L.segs.length; i++) {
				if (used[i]) continue;
				used[i] = 1;
				var s = L.segs[i];
				var fwd = walk(s.k1, [s.p0, s.p1]);
				var back = walk(s.k0, []);
				var line = back.reverse().concat(fwd);
				if (line.length >= 2) out.push({ level: L.level, path: line });
			}
		});
		if (opts.minLength) out = out.filter(function (c) { return Geo.length(c.path) >= opts.minLength; });
		return out;
	};

	// Zjednodušenie línie (Douglas–Peucker)
	Geo.simplify = function (path, tol) {
		if (path.length < 3 || !(tol > 0)) return path;
		var keep = new Uint8Array(path.length);
		keep[0] = keep[path.length - 1] = 1;
		var stack = [[0, path.length - 1]];
		while (stack.length) {
			var r = stack.pop(), a = r[0], b = r[1], maxD = -1, idx = -1;
			for (var i = a + 1; i < b; i++) {
				var d = segDist(path[a][0], path[a][1], path[b][0], path[b][1], path[i][0], path[i][1]);
				if (d > maxD) { maxD = d; idx = i; }
			}
			if (maxD > tol) { keep[idx] = 1; stack.push([a, idx], [idx, b]); }
		}
		return path.filter(function (p, i) { return keep[i]; });
	};

	// ---------------------------------------------------------------------------
	// Výstupy
	// ---------------------------------------------------------------------------
	Geo.toXyz = function (t, keep, off, dec) {
		dec = dec === undefined ? 3 : dec;
		var out = new Array(keep.length);
		for (var i = 0; i < keep.length; i++) {
			var k = keep[i];
			out[i] = (t.e[k] + off[0]).toFixed(dec) + ' ' + (t.n[k] + off[1]).toFixed(dec) + ' ' + (t.z[k] + off[2]).toFixed(dec);
		}
		return out.join('\r\n') + '\r\n';
	};

	Geo.worldFile = function (b, w, h, offE, offN) {
		var px = (b.maxE - b.minE) / w, py = (b.maxN - b.minN) / h;
		return px.toFixed(10) + '\r\n0.0\r\n0.0\r\n' + (-py).toFixed(10) + '\r\n' +
			(b.minE + offE + px / 2).toFixed(6) + '\r\n' + (b.maxN + offN - py / 2).toFixed(6) + '\r\n';
	};

	Geo.slug = function (s, max) {
		var map = { 'á': 'a', 'ä': 'a', 'č': 'c', 'ď': 'd', 'é': 'e', 'ě': 'e', 'í': 'i', 'ĺ': 'l', 'ľ': 'l', 'ň': 'n', 'ó': 'o', 'ô': 'o', 'ö': 'o', 'ŕ': 'r', 'ř': 'r', 'š': 's', 'ť': 't', 'ú': 'u', 'ů': 'u', 'ü': 'u', 'ý': 'y', 'ž': 'z' };
		var out = String(s).toLowerCase().replace(/[^\u0000-\u007f]/g, function (c) { return map[c] || ''; })
			.replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '');
		return max ? out.substr(0, max).replace(/_+$/, '') : out;
	};

	if (typeof module !== 'undefined' && module.exports) module.exports = Geo;
	else root.Geo = Geo;
})(this);
