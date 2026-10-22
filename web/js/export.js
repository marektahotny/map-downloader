/*
 * Pilon Map Downloader – zostavenie výstupných súborov (DXF, XYZ, JPG+JGW, TXT, XML)
 */
(function (root) {
	'use strict';
	var Geo = root.Geo, Dxf = root.Dxf, Sources = root.Sources;

	var Exporter = {};

	Exporter.defaultLayers = {
		cadastreC: { name: 'KN_parcely_C', color: 7 },
		cadastreCText: { name: 'KN_cisla_C', color: 7 },
		cadastreE: { name: 'KN_parcely_E', color: 3 },
		cadastreEText: { name: 'KN_cisla_E', color: 3 },
		plot: { name: 'KN_POZEMOK', color: 1 },
		buildings: { name: 'ZBGIS_budovy', color: 8 },
		origin: { name: 'POCIATOK', color: 1 },
		area: { name: 'HRANICA_UZEMIA', color: 9 },
		contour: { name: 'TEREN_vrstevnice', color: 33 },
		contourMajor: { name: 'TEREN_vrstevnice_hlavne', color: 31 },
		contourText: { name: 'TEREN_koty', color: 31 },
		terrain3d: { name: 'TEREN_3D', color: 52 }
	};

	function fmt(v, d) { return Number(v).toFixed(d).replace('.', ','); }
	function bomText(s) { return '﻿' + s.replace(/\r?\n/g, '\r\n'); }

	// ---------------------------------------------------------------------------
	// Kreslenie jednotlivých tém do DXF
	// ---------------------------------------------------------------------------
	function drawRings(d, layer, rings, clip) {
		var n = 0;
		rings.forEach(function (r) {
			if (r.length < 2) return;
			if (!clip) { d.polyline(layer, r, true); n++; return; }
			if (Geo.ringFullyInside(r, clip.poly)) { d.polyline(layer, r, true); n++; return; }
			Geo.clipPath(r, clip.poly, clip.box).forEach(function (p) { d.polyline(layer, p, false); n++; });
		});
		return n;
	}

	function drawPaths(d, layer, paths, clip) {
		var n = 0;
		paths.forEach(function (p) {
			if (p.length < 2) return;
			if (!clip) { d.polyline(layer, p, false); n++; return; }
			Geo.clipPath(p, clip.poly, clip.box).forEach(function (q) { d.polyline(layer, q, false); n++; });
		});
		return n;
	}

	function inside(clip, p) {
		return !clip || Geo.pointInPolygon(clip.poly, p[0], p[1]) || Geo.distanceToEdges(clip.poly, p[0], p[1]) < 1e-6;
	}

	function drawCadastre(d, feats, L, LT, textH, clip) {
		var lines = 0, texts = 0;
		feats.forEach(function (f) {
			lines += drawRings(d, L, f.rings, clip);
			if (!f.label) return;
			var p = f.refPoint || Geo.labelPoint(f.rings[0]);
			if (!inside(clip, p)) return;
			d.text(LT, p, textH, f.label, 0, 'center');
			texts++;
		});
		return { lines: lines, texts: texts };
	}

	function baLayerName(theme, kindTag, label, suffix) {
		var prefix = 'BA_' + theme.code + '_' + (kindTag ? kindTag + '_' : '');
		suffix = suffix || '';
		var room = Math.max(4, 31 - prefix.length - suffix.length);
		return prefix + (Geo.slug(label, room) || 'ostatne') + suffix;
	}

	function drawBa(d, res, opts, clip) {
		var theme = res.theme, stats = { lines: 0, symbols: 0, texts: 0, areas: 0, layers: {} };
		res.layers.forEach(function (lay) {
			var info = lay.info;
			lay.items.forEach(function (it) {
				var cls = Sources.featureClass(info, it.attrs);
				var color = Dxf.rgbToAci(cls.color || info.renderer.color || theme.color);
				var label = opts.splitByType && cls.label ? cls.label : info.name;
				var ln;
				if (info.kind === 'line') {
					var pip = info.hasPipeField && opts.splitConnections && Number(it.attrs.Potrubie) === 1;
					ln = d.layer(baLayerName(theme, '', opts.splitByType ? label : 'vedenie', pip ? '_PRIP' : ''), color, cls.ltype);
					var n = drawPaths(d, ln, it.paths || [], clip);
					stats.lines += n;
				} else if (info.kind === 'area') {
					ln = d.layer(baLayerName(theme, 'PL', label), color, cls.ltype);
					stats.areas += drawRings(d, ln, it.rings || [], clip);
				} else if (info.kind === 'symbol') {
					var pts = it.point ? [it.point] : (it.points || []);
					ln = d.layer(baLayerName(theme, 'P', opts.splitByType ? label : 'prvky'), color);
					pts.forEach(function (p) {
						if (!inside(clip, p)) return;
						d.circle(ln, p, opts.symbolSize / 2);
						d.point(ln, p);
						stats.symbols++;
					});
				} else if (info.kind === 'text') {
					var txt = Sources.featureText(info, it.attrs);
					if (!txt || !it.point || !inside(clip, it.point)) return;
					var rot = 0;
					if (info.rotationField && it.attrs[info.rotationField] !== undefined && it.attrs[info.rotationField] !== null) {
						rot = Number(it.attrs[info.rotationField]) || 0;
						if (info.rotationGeographic) rot = 90 - rot;
					}
					ln = d.layer(baLayerName(theme, 'T', info.name), color);
					d.text(ln, it.point, opts.textHeight * 0.8, txt, rot, 'center');
					stats.texts++;
				}
				if (ln) stats.layers[ln] = 1;
			});
		});
		stats.layerCount = Object.keys(stats.layers).length;
		return stats;
	}

	function drawOrigin(d, L, origin, h0, textH) {
		var s = Math.max(1, textH * 2);
		d.line(L, [origin[0] - s, origin[1]], [origin[0] + s, origin[1]]);
		d.line(L, [origin[0], origin[1] - s], [origin[0], origin[1] + s]);
		d.circle(L, origin, s / 2);
		d.text(L, [origin[0] + s * 0.7, origin[1] + s * 0.4], textH, '±0,000 = ' + fmt(h0, 3) + ' m n. m. Bpv', 0, 'left');
		d.text(L, [origin[0] + s * 0.7, origin[1] - s * 0.4 - textH], textH * 0.8, 'S-JTSK  Y = ' + fmt(-origin[0], 2) + '   X = ' + fmt(-origin[1], 2), 0, 'left');
	}

	function drawContours(d, contours, opts, Ls) {
		var major = opts.contourInterval * (opts.contourMajorEvery || 5), n = 0;
		var LC = d.layer(Ls.contour.name, Ls.contour.color), LM = d.layer(Ls.contourMajor.name, Ls.contourMajor.color), LT = null;
		contours.forEach(function (c) {
			var isMajor = Math.abs(c.level / major - Math.round(c.level / major)) < 1e-6;
			var path = Geo.simplify(c.path, 0.03);
			var closed = path.length > 3 && Math.hypot(path[0][0] - path[path.length - 1][0], path[0][1] - path[path.length - 1][1]) < 1e-6;
			d.polyline(isMajor ? LM : LC, path, closed, c.level);
			n++;
			if (isMajor && Geo.length(path) > opts.textHeight * 12) {
				LT = LT || d.layer(Ls.contourText.name, Ls.contourText.color);
				var mid = Math.floor(path.length / 2), a = path[Math.max(0, mid - 1)], b = path[Math.min(path.length - 1, mid)];
				var ang = Math.atan2(b[1] - a[1], b[0] - a[0]) * 180 / Math.PI;
				if (ang > 90) ang -= 180;
				if (ang < -90) ang += 180;
				d.text(LT, [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2], opts.textHeight * 0.7, fmt(c.level, 1), ang, 'center');
			}
		});
		return n;
	}

	// ---------------------------------------------------------------------------
	// 3D sieť terénu (3DFACE) z vybraných bodov
	// ---------------------------------------------------------------------------
	Exporter.terrainFaces = function (t, keep, areaPoly) {
		if (typeof root.Delaunator === 'undefined' || keep.length < 3) return [];
		var coords = new Float64Array(keep.length * 2);
		for (var i = 0; i < keep.length; i++) { coords[2 * i] = t.e[keep[i]]; coords[2 * i + 1] = t.n[keep[i]]; }
		var del = new root.Delaunator(coords), tri = del.triangles, faces = [];
		var maxEdge = Math.max(t.spacing * 40, 5);
		for (var k = 0; k < tri.length; k += 3) {
			var a = keep[tri[k]], b = keep[tri[k + 1]], c = keep[tri[k + 2]];
			var pa = [t.e[a], t.n[a], t.z[a]], pb = [t.e[b], t.n[b], t.z[b]], pc = [t.e[c], t.n[c], t.z[c]];
			var l = Math.max(Math.hypot(pa[0] - pb[0], pa[1] - pb[1]), Math.hypot(pb[0] - pc[0], pb[1] - pc[1]), Math.hypot(pc[0] - pa[0], pc[1] - pa[1]));
			if (l > maxEdge) continue;
			if (areaPoly) {
				var ce = (pa[0] + pb[0] + pc[0]) / 3, cn = (pa[1] + pb[1] + pc[1]) / 3;
				if (!Geo.pointInPolygon(areaPoly, ce, cn) && Geo.distanceToEdges(areaPoly, ce, cn) > t.spacing) continue;
			}
			faces.push([pa, pb, pc]);
		}
		return faces;
	};

	// ---------------------------------------------------------------------------
	// Texty
	// ---------------------------------------------------------------------------
	Exporter.originText = function (origin, h0) {
		var ll = typeof proj4 !== 'undefined' ? proj4('EPSG:5514', 'EPSG:4326', origin) : null;
		return [
			'Bod lokálneho počiatku (Pilon Map Downloader)',
			'S-JTSK / Krovak East North (EPSG:5514):  E = ' + origin[0].toFixed(3) + '   N = ' + origin[1].toFixed(3),
			'S-JTSK (Y, X kladné):                    Y = ' + (-origin[0]).toFixed(3) + '   X = ' + (-origin[1]).toFixed(3),
			ll ? 'WGS84:  šírka ' + ll[1].toFixed(7) + '°  dĺžka ' + ll[0].toFixed(7) + '°' : '',
			'Výška ±0,000 = ' + h0.toFixed(3) + ' m n. m. (Bpv)',
			'',
			'Lokálne súradnice v súboroch:  x = E − (' + origin[0].toFixed(3) + '),  y = N − (' + origin[1].toFixed(3) + '),  z = H − ' + h0.toFixed(3),
			'Archicad: Možnosti › Nastavenia projektu › Umiestnenie projektu – zadajte zemepisnú polohu a nadmorskú výšku bodu.',
			''
		].join('\r\n');
	};

	Exporter.importGuide = function (job, files) {
		var L = [];
		L.push('ARCHICAD 23 – IMPORT PODKLADOV (Pilon Map Downloader)');
		L.push('=====================================================');
		L.push('');
		L.push('Súradnice: ' + (job.coords === 'jtsk' ? 'S-JTSK (EPSG:5514, E a N záporné)' : job.coords === 'both' ? 'lokálne aj S-JTSK (dve sady súborov)' : 'lokálne – počiatok 0,0 je v bode zo súboru bod_pociatku.txt') + '. Jednotky: metre.');
		L.push('');
		L.push('1) Situácia DXF (kataster, siete, budovy, vrstevnice)');
		L.push('   Súbor › Interoperabilita › Zlúčiť (Merge) › vyberte .dxf › Prekladač „DXF-DWG“ (jednotky: metre).');
		L.push('   Alternatíva: Súbor › Externý obsah › Umiestniť externý výkres (vloží DXF ako výkres/xref).');
		L.push('   Hladiny DXF sa prevedú na vrstvy Archicadu (KN_..., BA_<sieť>_..., TEREN_...).');
		L.push('   Pri lokálnych súradniciach vložte s počiatkom v bode 0,0 projektu.');
		L.push('');
		L.push('2) Terén XYZ → sieť (Mesh)');
		L.push('   Súbor › Interoperabilita › Place Mesh from Surveyors Data (CZ: Umístit síť z dat geodeta) › vyberte teren.xyz.');
		L.push('   V dialógu: jednotka geodeta = meter, Umiestnenie = „Use coordinates of data file“ (súradnice zo súboru).');
		L.push(job.coords === 'jtsk'
			? '   Výšky v súbore sú nadmorské – do poľa „Project Zero is above Sea Level by“ zadajte ' + job.h0.toFixed(3) + ' m.'
			: '   Výšky v lokálnom súbore sú už relatívne k ±0,000 – do poľa „Project Zero is above Sea Level by“ zadajte 0.');
		L.push('   Archicad je pri veľkom počte bodov pomalý – odporúčame do cca 30 000 bodov (redukcia v nástroji).');
		L.push('   teren_3D.dxf obsahuje trojuholníkovú sieť (3DFACE) – dá sa otvoriť ako objekt GDL (Súbor › Knižnice a objekty › Otvoriť objekt).');
		L.push('');
		L.push('3) Ortofoto JPG');
		L.push('   Nástrojom Obrázok (Figure) vložte ortofoto.jpg, nastavte rozmer v metroch a ľavý dolný roh podľa ortofoto_info.txt.');
		L.push('   Súbor ortofoto.jgw je georeferencia pre GIS/CAD programy.');
		L.push('');
		L.push('Súbory v balíku:');
		files.forEach(function (f) { L.push('   ' + f.name); });
		L.push('');
		return L.join('\r\n');
	};

	Exporter.licenseText = function (job) {
		var L = ['ZDROJE A PODMIENKY ÚDAJOV', '========================', ''];
		if (job.usesUgkk) {
			L.push('Úrad geodézie, kartografie a katastra SR (ÚGKK SR) – kataster nehnuteľností (INSPIRE), ZBGIS budovy, ortofotomozaika.');
			L.push('  Licencia: CC BY 4.0 – uvádzajte zdroj „© ÚGKK SR“. Údaje katastra sú informatívne, nenahrádzajú výpis z KN ani geometrický plán.');
			L.push('');
		}
		if (job.usesBa) {
			L.push('Digitálna technická mapa hlavného mesta SR Bratislavy (©) Hlavné mesto SR Bratislava – inžinierske siete, polohopis, výškopis.');
			L.push('  Údaje z verejných GIS služieb mesta (geoportal.bratislava.sk) sa aktualizujú približne raz ročne a sú INFORMATÍVNE –');
			L.push('  neslúžia na právne účely. Záväzné vyjadrenie o existencii a polohe sietí vydávajú správcovia sietí');
			L.push('  (BVS, SPP-D, ZSD, MHTH, Slovak Telekom, ...). Aktuálne CAD údaje (DXF/DWG/DGN) predáva mesto podľa cenníka –');
			L.push('  dtmb@bratislava.sk, https://data.bratislava.sk/pages/technicka-mapa-mesta');
			if (job.baModes) L.push('  Prepočet súradníc: ' + job.baModes);
			L.push('');
		}
		L.push('Geokódovanie adries: © OpenStreetMap prispievatelia (Nominatim).');
		L.push('Vygenerované: ' + new Date().toLocaleString('sk-SK'));
		return L.join('\r\n') + '\r\n';
	};

	Exporter.plotReport = function (plot, neighbours, outline, error) {
		var total = 0, totalOff = 0;
		var txt = ['REPORT O POZEMKU (informatívny)', 'Zdroj: ÚGKK SR – kataster nehnuteľností (INSPIRE), Pilon Map Downloader', '', 'Parcely pozemku (register C):'];
		var xml = ['<?xml version="1.0" encoding="UTF-8"?>', '<pozemok>', '  <parcely>'];
		function esc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }
		plot.forEach(function (p) {
			var a = Geo.area(p.rings[0]);
			total += a;
			totalOff += p.officialArea || 0;
			txt.push('  ' + (p.label + '              ').substr(0, 14) + '  výmera z geometrie ' + ('          ' + fmt(a, 1)).slice(-10) + ' m²' + (p.officialArea ? '   úradná ' + Math.round(p.officialArea) + ' m²' : ''));
			xml.push('    <parcela id="' + esc(p.id) + '" cislo="' + esc(p.label) + '" vymeraGeometria="' + a.toFixed(2) + '" vymeraUradna="' + Math.round(p.officialArea || 0) + '"/>');
		});
		txt.push('');
		txt.push('Spolu: ' + plot.length + ' parciel, výmera z geometrie ' + fmt(total, 1) + ' m²' + (totalOff ? ', úradná ' + Math.round(totalOff) + ' m²' : ''));
		if (outline && outline.length) txt.push('Obrys pozemku: ' + outline[0].length + ' vrcholov, obvod ' + fmt(Geo.length(outline[0]), 2) + ' m');
		txt.push('');
		txt.push('Susedné parcely (dotýkajúce sa pozemku aspoň bodom):');
		xml.push('  </parcely>', '  <susedneParcely>');
		if (error) txt.push('  (susedné parcely sa nepodarilo načítať: ' + error + ')');
		neighbours.forEach(function (f) {
			var a = Geo.area(f.rings[0]);
			txt.push('  ' + (f.label + '              ').substr(0, 14) + '  výmera z geometrie ' + ('          ' + fmt(a, 1)).slice(-10) + ' m²');
			xml.push('    <parcela id="' + esc(f.id) + '" cislo="' + esc(f.label) + '" vymeraGeometria="' + a.toFixed(2) + '"/>');
		});
		xml.push('  </susedneParcely>', '</pozemok>', '');
		txt.push('');
		txt.push('Vlastníkov služba neposkytuje – overte na kataster.skgeodesy.sk alebo v aplikácii MAPKA.');
		return { txt: bomText(txt.join('\n') + '\n'), xml: xml.join('\r\n') };
	};

	// ---------------------------------------------------------------------------
	// Zostavenie balíka
	// job: { area, origin, h0, coords, textHeight, clip, separate, layers,
	//        cadastreC, cadastreE, plot, plotOutline, buildings, ba[], baOpts, terrain{t, keep, contours, faces}, ortho, xyzPositive }
	// ---------------------------------------------------------------------------
	Exporter.build = function (job) {
		var files = [], log = [];
		var Ls = job.layers || Exporter.defaultLayers;
		var clip = job.clip && job.area ? { poly: job.area, box: Geo.bounds(job.area) } : null;
		var systems = job.coords === 'both' ? ['local', 'jtsk'] : [job.coords === 'jtsk' ? 'jtsk' : 'local'];
		var th = job.textHeight || 0.5;

		systems.forEach(function (sys) {
			var suffix = systems.length > 1 ? (sys === 'jtsk' ? '_jtsk' : '_lokal') : '';
			var offs = sys === 'local' ? { offsetE: job.origin[0], offsetN: job.origin[1], offsetZ: job.h0 } : {};
			var docs = {};
			function doc(key) {
				var k = job.separate ? key : 'situacia';
				if (!docs[k]) docs[k] = new Dxf(offs);
				return docs[k];
			}
			var first = sys === systems[0];
			function note(item, ok, msg) { if (first) log.push({ item: item, ok: ok, message: msg }); }

			if (job.cadastreC) {
				var d = doc('kataster');
				var s = drawCadastre(d, job.cadastreC, d.layer(Ls.cadastreC.name, Ls.cadastreC.color), d.layer(Ls.cadastreCText.name, Ls.cadastreCText.color), th, clip);
				note('Kataster – parcely C', true, job.cadastreC.length + ' parciel, ' + s.lines + ' čiar, ' + s.texts + ' čísel');
			}
			if (job.cadastreE) {
				var dE = doc('kataster');
				var sE = drawCadastre(dE, job.cadastreE, dE.layer(Ls.cadastreE.name, Ls.cadastreE.color), dE.layer(Ls.cadastreEText.name, Ls.cadastreEText.color), th, clip);
				note('Kataster – parcely E', true, job.cadastreE.length + ' parciel, ' + sE.lines + ' čiar, ' + sE.texts + ' čísel');
			}
			if (job.plotOutline && job.plotOutline.length) {
				var dP = doc('kataster'), LP = dP.layer(Ls.plot.name, Ls.plot.color);
				job.plotOutline.forEach(function (r) { dP.polyline(LP, r, true); });
				note('Hranica pozemku', true, 'obvod ' + fmt(Geo.length(job.plotOutline[0]), 2) + ' m, výmera ' + fmt(Geo.area(job.plotOutline[0]), 1) + ' m²');
			}
			if (job.buildings) {
				var dB = doc('budovy'), LB = dB.layer(Ls.buildings.name, Ls.buildings.color), nb = 0;
				job.buildings.forEach(function (f) { nb += drawRings(dB, LB, f.rings, clip); });
				note('Budovy ZBGIS', true, job.buildings.length + ' budov, ' + nb + ' čiar');
			}
			(job.ba || []).forEach(function (res) {
				var dN = doc(res.theme.group === 'siete' ? 'siete_BA' : 'polohopis_BA');
				var st = drawBa(dN, res, { splitByType: job.baOpts.splitByType, splitConnections: job.baOpts.splitConnections, symbolSize: job.baOpts.symbolSize, textHeight: th }, clip);
				var parts = [];
				if (st.lines) parts.push(st.lines + ' línií');
				if (st.areas) parts.push(st.areas + ' plôch');
				if (st.symbols) parts.push(st.symbols + ' prvkov');
				if (st.texts) parts.push(st.texts + ' popisov');
				note(res.theme.title + ' (BA)', true, (parts.join(', ') || 'v území nie sú žiadne prvky') + (st.layerCount ? ' · ' + st.layerCount + ' hladín' : ''));
			});
			if (job.terrain && job.terrain.contours && job.terrain.contours.length) {
				var dC = doc('vrstevnice');
				var nc = drawContours(dC, job.terrain.contours, { contourInterval: job.terrain.interval, contourMajorEvery: job.terrain.majorEvery, textHeight: th }, Ls);
				note('Vrstevnice', true, nc + ' línií, interval ' + fmt(job.terrain.interval, 2) + ' m');
			}
			if (job.area && job.drawArea) {
				var dA = doc('kataster');
				dA.polyline(dA.layer(Ls.area.name, Ls.area.color, 'DASHED'), job.area, true);
			}
			if (job.originMarker) {
				var dO = docs.situacia || docs.kataster || docs.siete_BA || doc('kataster');
				drawOrigin(dO, dO.layer(Ls.origin.name, Ls.origin.color), job.origin, job.h0, th);
			}
			Object.keys(docs).forEach(function (k) {
				if (docs[k].hasContent()) files.push({ name: k + suffix + '.dxf', data: docs[k].toBytes() });
			});

			// terén
			if (job.terrain && job.terrain.keep && job.terrain.keep.length) {
				var t = job.terrain.t, keep = job.terrain.keep;
				var off = sys === 'local' ? [-job.origin[0], -job.origin[1], -job.h0] : [0, 0, 0];
				var xyz;
				if (sys === 'jtsk' && job.xyzPositive) {
					var lines = new Array(keep.length);
					for (var i = 0; i < keep.length; i++) lines[i] = (-t.e[keep[i]]).toFixed(3) + ' ' + (-t.n[keep[i]]).toFixed(3) + ' ' + t.z[keep[i]].toFixed(3);
					xyz = lines.join('\r\n') + '\r\n';
				} else {
					xyz = Geo.toXyz(t, keep, off, 3);
				}
				files.push({ name: 'teren' + suffix + '.xyz', data: xyz });
				note('Terén XYZ', true, keep.length.toLocaleString('sk-SK') + ' bodov (z ' + t.count.toLocaleString('sk-SK') + '), ' + (sys === 'local' ? 'výšky relatívne k ±0,000' : 'nadmorské výšky'));
				if (job.terrain.faces && job.terrain.faces.length) {
					var d3 = new Dxf(offs), L3 = d3.layer(Ls.terrain3d.name, Ls.terrain3d.color);
					job.terrain.faces.forEach(function (fc) { d3.face(L3, fc[0], fc[1], fc[2]); });
					files.push({ name: 'teren_3D' + suffix + '.dxf', data: d3.toBytes() });
					note('Terén 3D (3DFACE)', true, job.terrain.faces.length.toLocaleString('sk-SK') + ' trojuholníkov');
				}
			}

			// ortofoto
			if (job.ortho && sys === systems[0]) {
				var o = job.ortho, oe = sys === 'local' ? -job.origin[0] : 0, on = sys === 'local' ? -job.origin[1] : 0;
				files.push({ name: 'ortofoto.jpg', data: o.blob });
				files.push({ name: 'ortofoto.jgw', data: Geo.worldFile(o.bbox, o.width, o.height, oe, on) });
				var w = o.bbox.maxE - o.bbox.minE, h = o.bbox.maxN - o.bbox.minN;
				files.push({
					name: 'ortofoto_info.txt', data: bomText([
						'Ortofoto ZBGIS (© ÚGKK SR)',
						'Obrázok: ' + o.width + ' × ' + o.height + ' px, ' + fmt(w / o.width, 3) + ' m/px',
						'Rozmer v modeli: šírka ' + fmt(w, 3) + ' m, výška ' + fmt(h, 3) + ' m',
						'Ľavý dolný roh – lokálne: x = ' + fmt(o.bbox.minE - job.origin[0], 3) + '   y = ' + fmt(o.bbox.minN - job.origin[1], 3),
						'Ľavý dolný roh – S-JTSK:  E = ' + fmt(o.bbox.minE, 3) + '   N = ' + fmt(o.bbox.minN, 3),
						'Archicad: nástroj Obrázok – vložte JPG, nastavte veľkosť v metroch a umiestnite ľavým dolným rohom do uvedeného bodu.',
						'Súbor ortofoto.jgw je v ' + (sys === 'local' ? 'lokálnych' : 'S-JTSK') + ' súradniciach.'
					].join('\n') + '\n')
				});
				note('Ortofoto', true, o.width + ' × ' + o.height + ' px (' + fmt(w / o.width, 2) + ' m/px)');
			}
		});

		if (job.plotReport) {
			files.push({ name: 'report_pozemok.txt', data: job.plotReport.txt });
			files.push({ name: 'report_pozemok.xml', data: job.plotReport.xml });
			log.push({ item: 'Report o pozemku', ok: true, message: job.plot.length + ' parciel, ' + job.plotNeighbours + ' susedných' });
		}
		files.push({ name: 'bod_pociatku.txt', data: bomText(Exporter.originText(job.origin, job.h0)) });
		files.push({ name: 'zdroje_a_licencie.txt', data: bomText(Exporter.licenseText(job)) });
		files.push({ name: 'NAVOD_import_Archicad23.txt', data: bomText(Exporter.importGuide(job, files.concat([{ name: 'NAVOD_import_Archicad23.txt' }]))) });
		return { files: files, log: log };
	};

	if (typeof module !== 'undefined' && module.exports) module.exports = Exporter;
	else root.Exporter = Exporter;
})(this);
