/*
 * Pilon Map Downloader – zdroje údajov
 *   ÚGKK SR: kataster (INSPIRE WFS), budovy ZBGIS, ortofoto (WMS)
 *   Hlavné mesto SR Bratislava: digitálna technická mapa (ArcGIS REST) – inžinierske siete, polohopis, výškový model
 */
(function (root) {
	'use strict';
	var Geo = root.Geo;

	var Sources = {};

	Sources.config = {
		proxy: 'proxy.php',
		ortho: { url: 'https://zbgisws.skgeodesy.sk/zbgis_ortofoto_wms/service.svc/get', layers: '1', srs: 'EPSG:5514', format: 'image/jpeg', resolution: 0.25, maxPx: 6000, maxPxHQ: 16000, tilePx: 2000 },
		cadastreC: { url: 'https://inspirews.skgeodesy.sk/geoserver/cp/ows', typeName: 'cp:CadastralParcel' },
		cadastreE: { url: 'https://inspirews.skgeodesy.sk/geoserver/cp_uo/ows', typeName: 'cp_uo:CadastralParcel' },
		buildings: { url: 'https://zbgisws.skgeodesy.sk/inspire_buildings_wfs/service.svc/get', typeName: '' },
		baRoot: 'https://geoportal.bratislava.sk/hSite/rest/services/',
		baElevation: 'vyskopis/Elevation_5514W/ImageServer',
		geocoder: 'https://nominatim.openstreetmap.org/search',
		srsName: 'urn:ogc:def:crs:EPSG::5514'
	};

	// Inžinierske siete a polohopis technickej mapy (hlavné mesto SR Bratislava)
	Sources.BA_THEMES = [
		{ key: 'voda', code: 'VODA', title: 'Vodovod', path: 'inzinierske_siete/Vodovody', color: [0, 92, 230], group: 'siete' },
		{ key: 'kanal', code: 'KANAL', title: 'Kanalizácia', path: 'inzinierske_siete/Kanalizácia', color: [153, 102, 51], group: 'siete' },
		{ key: 'plyn', code: 'PLYN', title: 'Plynovod', path: 'inzinierske_siete/Plynovody', color: [230, 200, 0], group: 'siete' },
		{ key: 'teplo', code: 'TEPLO', title: 'Teplovod', path: 'inzinierske_siete/Teplovody', color: [200, 0, 160], group: 'siete' },
		{ key: 'elektro', code: 'ELEKTRO', title: 'Elektrina', path: 'inzinierske_siete/Elektrina', color: [230, 0, 0], group: 'siete' },
		{ key: 'tele', code: 'TELEKOM', title: 'Telekomunikácie', path: 'inzinierske_siete/Telekomunikácie', color: [255, 128, 0], group: 'siete' },
		{ key: 'kolektor', code: 'KOLEKTOR', title: 'Kolektory', path: 'inzinierske_siete/Kolektory', color: [100, 100, 100], group: 'siete' },
		{ key: 'produkt', code: 'PRODUKT', title: 'Produktovody', path: 'inzinierske_siete/Produktovody', color: [128, 0, 64], group: 'siete' },
		{ key: 'stavby', code: 'STAVBY', title: 'Stavby a budovy', path: 'tm/Stavby', color: [60, 60, 60], group: 'tm' },
		{ key: 'doprava', code: 'DOPRAVA', title: 'Doprava (cesty, chodníky, obrubníky)', path: 'tm/Doprava', color: [90, 90, 90], group: 'tm' },
		{ key: 'zelen', code: 'ZELEN', title: 'Zeleň', path: 'tm/Zeleň_TM', color: [0, 150, 0], group: 'tm' },
		{ key: 'vyskopis', code: 'VYSKOPIS', title: 'Výškopis technickej mapy (kóty, skaly)', path: 'tm/Výškopis_Technická_mapa', color: [150, 75, 0], group: 'tm' }
	];

	Sources.themeByKey = function (k) {
		for (var i = 0; i < Sources.BA_THEMES.length; i++) if (Sources.BA_THEMES[i].key === k) return Sources.BA_THEMES[i];
		return null;
	};

	// ---------------------------------------------------------------------------
	// Sieť: cez proxy (proxy.php alebo lokálny server), inak priamo (ak to server dovolí – CORS)
	// ---------------------------------------------------------------------------
	var proxyState = null;	// null = nezistené, true = funguje, false = nie je k dispozícii

	function encPath(p) { return p.split('/').map(encodeURIComponent).join('/'); }
	Sources.encPath = encPath;

	Sources.proxyAvailable = function () { return proxyState; };

	async function rawFetch(url, opts) {
		var cfg = Sources.config;
		if (cfg.proxy && proxyState !== false) {
			var purl = cfg.proxy + (cfg.proxy.indexOf('?') >= 0 ? '&' : '?') + 'url=' + encodeURIComponent(url) + (cfg.proxyKey ? '&key=' + encodeURIComponent(cfg.proxyKey) : '');
			try {
				var r = await fetch(purl, opts);
				if (r.headers.get('X-PMD-Proxy')) {
					proxyState = true;
					return r;
				}
				if (proxyState === true) return r;
				proxyState = false;	// odpoveď nie je z našej proxy (napr. 404 stránka hostingu)
			} catch (e) {
				if (proxyState === true) throw e;
				proxyState = false;
			}
		}
		try {
			return await fetch(url, opts);
		} catch (e) {
			throw new Error('Server ' + new URL(url).host + ' nie je dostupný priamo z prehliadača (CORS). Spustite nástroj cez start.cmd alebo z webhostingu s proxy.php.');
		}
	}

	async function getResponse(url, timeoutMs) {
		var ctl = typeof AbortController !== 'undefined' ? new AbortController() : null;
		var timer = ctl ? setTimeout(function () { ctl.abort(); }, timeoutMs || 180000) : null;
		try {
			var r = await rawFetch(url, ctl ? { signal: ctl.signal } : undefined);
			if (!r.ok) {
				var body = '';
				try { body = await r.text(); } catch (e) { }
				var ex = Geo.ogcException(body);
				throw new Error('HTTP ' + r.status + (ex ? ': ' + ex : body && body.length < 300 ? ': ' + body : '') + ' (' + new URL(url).host + ')');
			}
			return r;
		} catch (e) {
			if (e.name === 'AbortError') throw new Error('Vypršal čas odpovede servera ' + new URL(url).host);
			throw e;
		} finally {
			if (timer) clearTimeout(timer);
		}
	}

	Sources.getText = async function (url, t) { return (await getResponse(url, t)).text(); };
	Sources.getJson = async function (url, t) {
		var txt = await Sources.getText(url, t), j;
		try { j = JSON.parse(txt); } catch (e) { throw new Error('Neplatná odpoveď (nie JSON): ' + txt.substr(0, 160)); }
		if (j && j.error) throw new Error('ArcGIS: ' + (j.error.message || '') + (j.error.details && j.error.details.length ? ' – ' + j.error.details.join('; ') : ''));
		return j;
	};
	Sources.getBuffer = async function (url, t) {
		var r = await getResponse(url, t);
		var ct = r.headers.get('Content-Type') || '';
		var buf = await r.arrayBuffer();
		return { buffer: buf, contentType: ct };
	};

	function q(params) {
		return Object.keys(params).filter(function (k) { return params[k] !== undefined && params[k] !== null; })
			.map(function (k) { return encodeURIComponent(k) + '=' + encodeURIComponent(params[k]); }).join('&');
	}
	function withQuery(base, params) { return base + (base.indexOf('?') >= 0 ? '&' : '?') + q(params); }
	function n3(v) { return (Math.round(v * 1000) / 1000).toString(); }

	// ---------------------------------------------------------------------------
	// ÚGKK – WFS (kataster, budovy)
	// ---------------------------------------------------------------------------
	var axisSwap = {};	// url -> true, ak server čaká BBOX v poradí N,E

	Sources.wfsFeatures = async function (svc, bbox, opts) {
		opts = opts || {};
		var typeName = svc.typeName;
		if (!typeName) typeName = svc.typeName = await Sources.discoverType(svc, opts.typeHint || /Building/i);
		async function fetchOnce(swap) {
			var bb = swap ? [bbox.minN, bbox.minE, bbox.maxN, bbox.maxE] : [bbox.minE, bbox.minN, bbox.maxE, bbox.maxN];
			var url = withQuery(svc.url, {
				SERVICE: 'WFS', VERSION: '2.0.0', REQUEST: 'GetFeature', TYPENAMES: typeName,
				SRSNAME: Sources.config.srsName, COUNT: opts.count || 20000,
				BBOX: bb.map(n3).join(',') + ',' + Sources.config.srsName
			});
			var text = await Sources.getText(url, 240000);
			return Geo.parseGml(text, opts.labelTags);
		}
		var swap = !!axisSwap[svc.url];
		var list = await fetchOnce(swap);
		if (!list.length && !(svc.url in axisSwap)) {
			// iné poradie osí BBOX – skúsiť raz
			var alt = await fetchOnce(!swap);
			if (alt.length) { axisSwap[svc.url] = !swap; return alt; }
		}
		if (list.length) axisSwap[svc.url] = swap;
		return list;
	};

	Sources.discoverType = async function (svc, re) {
		var xml = await Sources.getText(withQuery(svc.url, { SERVICE: 'WFS', VERSION: '2.0.0', REQUEST: 'GetCapabilities' }), 60000);
		var ex = Geo.ogcException(xml);
		if (ex) throw new Error(ex);
		var names = Geo.capabilityNames(xml);
		var pick = names.filter(function (n) { return re.test(n) && !/Part/i.test(n); })[0] || names.filter(function (n) { return re.test(n); })[0] || names[0];
		if (!pick) throw new Error('Služba neponúka žiadne vrstvy (FeatureType)');
		return pick;
	};

	// ---------------------------------------------------------------------------
	// ÚGKK – ortofoto (WMS GetMap v S-JTSK, po dlaždiciach zlepené na plátne)
	// ---------------------------------------------------------------------------
	Sources.orthoImage = async function (bbox, hq, progress) {
		var o = Sources.config.ortho;
		var w = bbox.maxE - bbox.minE, h = bbox.maxN - bbox.minN;
		var pxW = Math.ceil(w / o.resolution), pxH = Math.ceil(h / o.resolution);
		var maxPx = hq ? o.maxPxHQ : o.maxPx;
		var s = Math.min(1, maxPx / Math.max(pxW, pxH));
		pxW = Math.max(16, Math.floor(pxW * s));
		pxH = Math.max(16, Math.floor(pxH * s));
		var tile = o.tilePx, nx = Math.ceil(pxW / tile), ny = Math.ceil(pxH / tile);
		var canvas = document.createElement('canvas');
		canvas.width = pxW;
		canvas.height = pxH;
		var ctx = canvas.getContext('2d');
		ctx.fillStyle = '#ffffff';
		ctx.fillRect(0, 0, pxW, pxH);
		var mpp = w / pxW, mppN = h / pxH, done = 0;
		for (var ty = 0; ty < ny; ty++) {
			for (var tx = 0; tx < nx; tx++) {
				var x0 = tx * tile, y0 = ty * tile, tw = Math.min(tile, pxW - x0), th = Math.min(tile, pxH - y0);
				var tb = { minE: bbox.minE + x0 * mpp, maxE: bbox.minE + (x0 + tw) * mpp, maxN: bbox.maxN - y0 * mppN, minN: bbox.maxN - (y0 + th) * mppN };
				var url = withQuery(o.url, {
					SERVICE: 'WMS', VERSION: '1.1.1', REQUEST: 'GetMap', LAYERS: o.layers, STYLES: '', SRS: o.srs,
					BBOX: [tb.minE, tb.minN, tb.maxE, tb.maxN].map(n3).join(','), WIDTH: tw, HEIGHT: th, FORMAT: o.format
				});
				if (progress) progress('Ortofoto: dlaždica ' + (++done) + ' / ' + (nx * ny));
				var res = await Sources.getBuffer(url, 300000);
				if (res.contentType.indexOf('image') < 0) {
					var txt = new TextDecoder().decode(res.buffer);
					throw new Error('Ortofoto: ' + (Geo.ogcException(txt) || txt.substr(0, 200)));
				}
				var bmp = await createImageBitmap(new Blob([res.buffer], { type: res.contentType }));
				ctx.drawImage(bmp, x0, y0, tw, th);
				if (bmp.close) bmp.close();
			}
		}
		var blob = await new Promise(function (resolve) { canvas.toBlob(resolve, 'image/jpeg', 0.9); });
		return { blob: blob, width: pxW, height: pxH, bbox: bbox };
	};

	// ---------------------------------------------------------------------------
	// Bratislava – ArcGIS REST
	// ---------------------------------------------------------------------------
	var metaCache = {};

	Sources.baServiceUrl = function (theme) { return Sources.config.baRoot + encPath(theme.path) + '/MapServer'; };

	function isSkipped(name) { return /vizu[aá]l|heatmap|hustota/i.test(name); }

	function symColor(sym) {
		if (sym && sym.color && sym.color.length >= 3) return [sym.color[0], sym.color[1], sym.color[2]];
		if (sym && sym.outline && sym.outline.color) return [sym.outline.color[0], sym.outline.color[1], sym.outline.color[2]];
		return null;
	}
	function symDash(sym) {
		var st = sym && (sym.style || (sym.outline && sym.outline.style)) || '';
		if (/Dash.*Dot/i.test(st)) return 'DASHDOT';
		if (/Dash/i.test(st)) return 'DASHED';
		if (/Dot/i.test(st)) return 'DOT';
		return 'CONTINUOUS';
	}

	// rozbor vrstvy -> čo a ako sa z nej kreslí
	Sources.classifyLayer = function (L) {
		var info = { id: L.id, name: L.name, kind: 'skip', geometryType: L.geometryType, minScale: L.minScale || 0, fields: L.fields || [] };
		if (L.type && L.type !== 'Feature Layer') return info;
		if (!L.geometryType || isSkipped(L.name)) return info;
		var fields = (L.fields || []).filter(function (f) { return !/OID|Geometry|GlobalID|Blob|Raster/.test(f.type) && !/^(shape_length|shape_area|shape\.|objectid)/i.test(f.name); });
		var R = L.drawingInfo && L.drawingInfo.renderer || {};
		info.renderer = {
			type: R.type || 'simple',
			field: R.field1 || null,
			classes: (R.uniqueValueInfos || []).map(function (u) { return { value: String(u.value), label: u.label || String(u.value), color: symColor(u.symbol), ltype: symDash(u.symbol) }; }),
			defaultLabel: R.defaultLabel || '',
			color: symColor(R.symbol || R.defaultSymbol),
			ltype: symDash(R.symbol || R.defaultSymbol)
		};
		// kódované hodnoty (domény) na pomenovanie tried
		info.domains = {};
		(L.fields || []).forEach(function (f) {
			if (f.domain && f.domain.codedValues) {
				var m = {};
				f.domain.codedValues.forEach(function (cv) { m[String(cv.code)] = cv.name; });
				info.domains[f.name] = m;
			}
		});
		var textField = null;
		fields.forEach(function (f) { if (!textField && /^(popis|text|label|napis|nápis|cislo|číslo)$/i.test(f.name)) textField = f.name; });
		info.rotationField = null;
		fields.forEach(function (f) { if (!info.rotationField && /^(uhol|angle|rotation|rotacia|natocenie)$/i.test(f.name)) info.rotationField = f.name; });
		if (R.rotationExpression) {
			var m = /\[([^\]]+)\]/.exec(R.rotationExpression);
			if (m) info.rotationField = m[1];
		}
		info.rotationGeographic = R.rotationType === 'geographic';
		info.hasPipeField = fields.some(function (f) { return f.name === 'Potrubie'; });
		if (/Polyline/.test(L.geometryType)) info.kind = 'line';
		else if (/Polygon/.test(L.geometryType)) info.kind = 'area';
		else if (/Point/.test(L.geometryType)) {
			var symbolic = info.renderer.type === 'uniqueValue' && info.renderer.classes.length > 0;
			if (textField && !symbolic) info.kind = 'text';
			else if (symbolic) info.kind = 'symbol';
			else {
				// bodová vrstva bez symbolov – text z prvého atribútu (napr. výšky, DN)
				textField = textField || (fields[0] && fields[0].name);
				info.kind = textField ? 'text' : 'symbol';
			}
		}
		info.textField = textField;
		info.fieldTypes = {};
		(L.fields || []).forEach(function (f) { info.fieldTypes[f.name] = f.type; });
		info.label = textOrLayerLabel(L.name);
		info.defaultOn = info.kind !== 'skip' && !/podla[žz]nos/i.test(L.name);
		return info;
	};

	function textOrLayerLabel(n) { return n; }

	Sources.baMeta = async function (theme) {
		if (metaCache[theme.key]) return metaCache[theme.key];
		var base = Sources.baServiceUrl(theme);
		var svc = await Sources.getJson(base + '?f=json', 60000);
		var layers = null;
		try {
			var all = await Sources.getJson(base + '/layers?f=json', 90000);
			if (all && all.layers && all.layers.length) layers = all.layers;
		} catch (e) { layers = null; }
		if (!layers) {
			layers = [];
			for (var i = 0; i < (svc.layers || []).length; i++) {
				var l = svc.layers[i];
				if (l.subLayerIds && l.subLayerIds.length) continue;
				try { layers.push(await Sources.getJson(base + '/' + l.id + '?f=json', 60000)); } catch (e) { }
			}
		}
		var sr = svc.spatialReference || {};
		var meta = {
			theme: theme, base: base, srWkid: sr.latestWkid || sr.wkid,
			datumTransformations: svc.datumTransformations || null,
			copyright: svc.copyrightText || '',
			layers: layers.map(Sources.classifyLayer).filter(function (x) { return x.kind !== 'skip'; }),
			maxRecordCount: svc.maxRecordCount || 1000
		};
		metaCache[theme.key] = meta;
		return meta;
	};

	// Transformácia, ktorú služba uvádza pre WGS84 -> S-JTSK (mesto: JTSK03 -> WGS84 a JTSK03 -> S-JTSK)
	function pickTransformToJtsk(meta) {
		var sets = meta.datumTransformations;
		if (!sets || !sets.length) return null;
		for (var i = 0; i < sets.length; i++) {
			var gt = sets[i].geoTransforms || [];
			if (gt.length && gt[0].transformForward === false && /WGS/i.test(gt[0].name || '')) return { geoTransforms: gt.map(function (g) { return { wkid: g.wkid, transformForward: g.transformForward }; }) };
		}
		var last = sets[sets.length - 1].geoTransforms || [];
		return last.length ? { geoTransforms: last.map(function (g) { return { wkid: g.wkid, transformForward: g.transformForward }; }) } : null;
	}

	var NATIVE_JTSK = { 5514: 1, 102067: 1, 2065: 1, 102066: 1 };

	// Súradnice z ArcGIS: ak služba nie je v S-JTSK, požiada sa server o 5514 s transformáciou zo služby;
	// ak to server odmietne, stiahne sa v 3857 a prepočíta sa lokálne (proj4, JTSK03 -> WGS84).
	async function queryLayer(meta, layer, bbox, progress, state) {
		var url = meta.base + '/' + layer.id + '/query';
		var native = NATIVE_JTSK[meta.srWkid];
		var inSR = 5514, env = [bbox.minE, bbox.minN, bbox.maxE, bbox.maxN].map(n3).join(',');
		if (!native && typeof proj4 !== 'undefined') {
			// obálku pošleme v súradniciach služby, aby výber nezávisel od transformácie servera
			var tr = proj4('EPSG:5514', 'EPSG:3857');
			var cs = [[bbox.minE, bbox.minN], [bbox.maxE, bbox.minN], [bbox.maxE, bbox.maxN], [bbox.minE, bbox.maxN]].map(function (c) { return tr.forward(c); });
			var xs = cs.map(function (c) { return c[0]; }), ys = cs.map(function (c) { return c[1]; });
			env = [Math.min.apply(null, xs) - 15, Math.min.apply(null, ys) - 15, Math.max.apply(null, xs) + 15, Math.max.apply(null, ys) + 15].map(n3).join(',');
			inSR = 3857;
		}
		var page = Math.min(meta.maxRecordCount || 1000, 2000);
		var out = [], seen = {};
		async function run(envelope, depth) {
			var offset = 0;
			while (true) {
				var p = {
					where: '1=1', geometry: envelope, geometryType: 'esriGeometryEnvelope', inSR: inSR, spatialRel: 'esriSpatialRelIntersects',
					outFields: '*', returnGeometry: 'true', returnZ: 'false', f: 'json'
				};
				if (state.mode === 'server' || native) {
					p.outSR = 5514;
					if (!native && state.transform) p.datumTransformation = JSON.stringify(state.transform);
				} else {
					p.outSR = 3857;
				}
				if (state.paging !== false) { p.resultOffset = offset; p.resultRecordCount = page; p.orderByFields = 'OBJECTID'; }
				var j;
				try {
					j = await Sources.getJson(withQuery(url, p), 240000);
				} catch (e) {
					if (state.paging !== false && /pagination|resultOffset|orderBy/i.test(e.message)) { state.paging = false; continue; }
					if (state.mode === 'server' && !native && /transform|datum/i.test(e.message)) { state.mode = 'local'; continue; }
					throw e;
				}
				var feats = j.features || [];
				var oidField = j.objectIdFieldName || 'OBJECTID', added = 0;
				feats.forEach(function (f) {
					var oid = f.attributes && f.attributes[oidField];
					if (oid !== undefined && seen[oid]) return;
					if (oid !== undefined) seen[oid] = 1;
					f.srcSR = p.outSR;
					out.push(f);
					added++;
				});
				if (j.exceededTransferLimit && state.paging !== false && feats.length && !added) state.paging = false;	// server stránkovanie ignoruje
				if (progress) progress(meta.theme.title + ' – ' + layer.name + ': ' + out.length + ' prvkov');
				if (!j.exceededTransferLimit) return;
				if (state.paging !== false && added) { offset += feats.length; continue; }
				// bez stránkovania: rozdeliť obálku na štvrtiny
				if (depth >= 6) return;
				var b = envelope.split(',').map(Number), mx = (b[0] + b[2]) / 2, my = (b[1] + b[3]) / 2;
				var quads = [[b[0], b[1], mx, my], [mx, b[1], b[2], my], [b[0], my, mx, b[3]], [mx, my, b[2], b[3]]];
				for (var k = 0; k < quads.length; k++) await run(quads[k].map(n3).join(','), depth + 1);
				return;
			}
		}
		await run(env, 0);
		return out;
	}

	// geometria prvku -> S-JTSK [e, n]
	function makeConverter(sr) {
		if (sr === 3857 && typeof proj4 !== 'undefined') {
			var tr = proj4('EPSG:3857', 'EPSG:5514');
			return function (xy) { return tr.forward([xy[0], xy[1]]); };
		}
		return function (xy) { return [xy[0], xy[1]]; };
	}

	Sources.baDownload = async function (theme, layerIds, bbox, progress) {
		var meta = await Sources.baMeta(theme);
		var state = { mode: NATIVE_JTSK[meta.srWkid] ? 'native' : 'server', transform: pickTransformToJtsk(meta), paging: undefined };
		var result = { theme: theme, meta: meta, layers: [], mode: state.mode };
		for (var i = 0; i < meta.layers.length; i++) {
			var L = meta.layers[i];
			if (layerIds && layerIds.indexOf(L.id) < 0) continue;
			var feats = await queryLayer(meta, L, Geo.expand(bbox, 5), progress, state);
			var items = [];
			feats.forEach(function (f) {
				var conv = makeConverter(f.srcSR);
				var g = f.geometry;
				if (!g) return;
				var item = { attrs: f.attributes || {} };
				if (g.paths) item.paths = g.paths.map(function (p) { return p.map(conv); });
				else if (g.rings) item.rings = g.rings.map(function (r) { return r.map(conv); });
				else if (g.x !== undefined) item.point = conv([g.x, g.y]);
				else if (g.points) item.points = g.points.map(conv);
				items.push(item);
			});
			result.layers.push({ info: L, items: items });
			result.mode = state.mode;
		}
		return result;
	};

	// Výškový model mesta (ImageServer, 1 m, S-JTSK) -> terén
	Sources.baElevation = async function (bbox, res, progress) {
		res = res || 1;
		var base = Sources.config.baRoot + encPath(Sources.config.baElevation);
		var minE = Math.floor(bbox.minE / res) * res, minN = Math.floor(bbox.minN / res) * res;
		var maxE = Math.ceil(bbox.maxE / res) * res, maxN = Math.ceil(bbox.maxN / res) * res;
		var W = Math.round((maxE - minE) / res), H = Math.round((maxN - minN) / res);
		if (W * H > 8000000) throw new Error('Územie je pre terén príliš veľké (' + W + ' × ' + H + ' bodov). Zmenšite výber alebo rozostup.');
		var tile = 2000, parts = [], nx = Math.ceil(W / tile), ny = Math.ceil(H / tile), k = 0;
		for (var ty = 0; ty < ny; ty++) {
			for (var tx = 0; tx < nx; tx++) {
				var tw = Math.min(tile, W - tx * tile), th = Math.min(tile, H - ty * tile);
				var e0 = minE + tx * tile * res, n1 = maxN - ty * tile * res;
				var tb = [e0, n1 - th * res, e0 + tw * res, n1];
				if (progress) progress('Terén Bratislava: časť ' + (++k) + ' / ' + (nx * ny));
				var url = withQuery(base + '/exportImage', {
					bbox: tb.map(n3).join(','), bboxSR: 5514, imageSR: 5514, size: tw + ',' + th, format: 'tiff', pixelType: 'F32',
					noData: -9999, noDataInterpretation: 'esriNoDataMatchAny', interpolation: 'RSP_BilinearInterpolation', f: 'image'
				});
				var r = await Sources.getBuffer(url, 300000);
				if (!/tif|octet|image/i.test(r.contentType)) {
					var txt = new TextDecoder().decode(r.buffer);
					var err = txt;
					try { var j = JSON.parse(txt); err = j.error ? j.error.message : txt; } catch (e) { }
					throw new Error('Terén: ' + String(err).substr(0, 200));
				}
				var tiff = await GeoTIFF.fromArrayBuffer(r.buffer);
				var img = await tiff.getImage();
				var ras = await img.readRasters({ interleave: true });
				var iw = img.getWidth(), ih = img.getHeight();
				var rx = (tb[2] - tb[0]) / iw, ry = (tb[3] - tb[1]) / ih;
				parts.push(Geo.terrainFromGrid({ e0: tb[0] + rx / 2, n0Top: tb[3] - ry / 2, resE: rx, resN: ry, cols: iw, rows: ih, values: ras, noData: -9999 }, 'Bratislava ' + res + ' m'));
			}
		}
		return Geo.mergeTerrains(parts);
	};

	// Terén zo súboru (GeoTIFF / ASC / XYZ)
	Sources.terrainFromFile = async function (file) {
		var name = file.name.toLowerCase();
		if (/\.(tif|tiff)$/.test(name)) {
			var buf = await file.arrayBuffer();
			var tiff = await GeoTIFF.fromArrayBuffer(buf);
			var img = await tiff.getImage();
			var bb = img.getBoundingBox(), iw = img.getWidth(), ih = img.getHeight();
			var nd = img.getGDALNoData();
			var ras = await img.readRasters({ interleave: true, samples: [0] });
			var rx = (bb[2] - bb[0]) / iw, ry = (bb[3] - bb[1]) / ih;
			return Geo.terrainFromGrid({ e0: bb[0] + rx / 2, n0Top: bb[3] - ry / 2, resE: rx, resN: ry, cols: iw, rows: ih, values: ras, noData: nd === null ? -9999 : nd }, file.name);
		}
		var text = await file.text();
		if (/^\s*ncols/i.test(text)) return Geo.parseArcGrid(text, file.name);
		return Geo.parseXyz(text, file.name);
	};

	// Text pre bodovú/textovú vrstvu
	Sources.featureText = function (info, attrs) {
		var f = info.textField, v = f ? attrs[f] : null;
		if ((v === null || v === undefined || v === '') && attrs.DN) v = 'DN ' + attrs.DN;
		if (v === null || v === undefined) return '';
		if (typeof v === 'number') {
			var t = info.fieldTypes[f] || '';
			if (/Double|Single/.test(t)) return (Math.round(v * 100) / 100).toFixed(2).replace('.', ',');
			return String(v);
		}
		return String(v).trim();
	};

	// Trieda prvku (podľa renderera / domény)
	Sources.featureClass = function (info, attrs) {
		var R = info.renderer, fld = R.field;
		if (fld && R.classes.length) {
			var v = String(attrs[fld]);
			for (var i = 0; i < R.classes.length; i++) if (R.classes[i].value === v) return R.classes[i];
			var dn = info.domains[fld] && info.domains[fld][v];
			return { value: v, label: dn || R.defaultLabel || ('typ ' + v), color: R.color, ltype: R.ltype };
		}
		return { value: '', label: '', color: R.color, ltype: R.ltype };
	};

	Sources.withQuery = withQuery;

	if (typeof module !== 'undefined' && module.exports) module.exports = Sources;
	else root.Sources = Sources;
})(this);
