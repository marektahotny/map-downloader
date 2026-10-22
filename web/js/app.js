/*
 * Pilon Map Downloader – webové rozhranie
 */
(function () {
	'use strict';

	proj4.defs('EPSG:5514', Geo.EPSG5514);

	function $(id) { return document.getElementById(id); }
	function fmt(v, d) { return Number(v).toLocaleString('sk-SK', { minimumFractionDigits: d, maximumFractionDigits: d }); }
	function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }
	function setStatus(text, cls) { var el = $('status'); el.textContent = text; el.className = cls || ''; }
	function setBusy(on) { $('busy').className = on ? 'on' : ''; $('btnExport').disabled = !!on; }
	function debounce(fn, ms) { var t = null; return function () { var a = arguments; clearTimeout(t); t = setTimeout(function () { fn.apply(null, a); }, ms); }; }
	function toJtsk(ll) { return proj4('EPSG:4326', 'EPSG:5514', [ll.lng, ll.lat]); }
	function toLatLng(en) { var p = proj4('EPSG:5514', 'EPSG:4326', [en[0], en[1]]); return L.latLng(p[1], p[0]); }
	function store(k, v) { try { localStorage.setItem('pmd.' + k, JSON.stringify(v)); } catch (e) { } }
	function load(k, def) { try { var v = localStorage.getItem('pmd.' + k); return v ? JSON.parse(v) : def; } catch (e) { return def; } }
	function radio(name) { var el = document.querySelector('input[name=' + name + ']:checked'); return el ? el.value : null; }
	function setRadio(name, v) { var el = document.querySelector('input[name=' + name + '][value="' + v + '"]'); if (el) el.checked = true; }

	// =========================================================================
	// Nastavenia
	// =========================================================================
	var defaultSettings = { layers: JSON.parse(JSON.stringify(Exporter.defaultLayers)), proxy: 'proxy.php', proxyKey: '' };
	var settings = load('settings', null) || JSON.parse(JSON.stringify(defaultSettings));
	Object.keys(defaultSettings.layers).forEach(function (k) { if (!settings.layers[k]) settings.layers[k] = defaultSettings.layers[k]; });
	function applySettings() {
		Sources.config.proxy = settings.proxy;
		Sources.config.proxyKey = settings.proxyKey;
	}
	applySettings();
	var layerLabels = {
		cadastreC: 'Kataster C – hranice', cadastreCText: 'Kataster C – čísla', cadastreE: 'Kataster E – hranice', cadastreEText: 'Kataster E – čísla',
		plot: 'Hranica pozemku', buildings: 'Budovy ZBGIS', origin: 'Počiatok', area: 'Hranica územia',
		contour: 'Vrstevnice', contourMajor: 'Vrstevnice hlavné', contourText: 'Kóty vrstevníc', terrain3d: 'Terén 3D (3DFACE)'
	};

	// =========================================================================
	// Stav
	// =========================================================================
	var state = {
		area: null,			// [[e,n],...] uzavretý
		origin: null,		// [e,n]
		plot: [],			// [{id,label,rings,officialArea}]
		terrain: null,		// zrezaný terén
		terrainRaw: null,
		baLayers: {},		// key -> [ids] (null = predvolené)
		baMeta: {}
	};
	var saved = load('state', {});
	if (saved.area) state.area = saved.area;
	if (saved.origin) state.origin = saved.origin;
	if (saved.plot) state.plot = saved.plot;
	if (saved.baLayers) state.baLayers = saved.baLayers;

	var uiIds = ['itCadC', 'itCadE', 'itPlot', 'itBuildings', 'itOrtho', 'orthoHQ', 'itPlotReport', 'baSplit', 'baConn', 'baShow', 'itTerrain', 'redOn', 'terContours', 'terFaces', 'xyzPositive', 'optClip', 'optSeparate', 'optArea', 'optOrigin',
		'redKeep', 'redNear', 'redFar', 'terInterval', 'terMajor', 'terRes', 'h0', 'textH', 'symSize'];
	function saveUi() {
		var ui = { radios: { terSrc: radio('terSrc'), redMode: radio('redMode'), coords: radio('coords') }, themes: {} };
		uiIds.forEach(function (id) { var el = $(id); if (el) ui[id] = el.type === 'checkbox' ? el.checked : el.value; });
		Sources.BA_THEMES.forEach(function (t) { var el = $('th_' + t.key); if (el) ui.themes[t.key] = el.checked; });
		store('ui', ui);
		store('state', { area: state.area, origin: state.origin, plot: state.plot, baLayers: state.baLayers });
	}
	function restoreUi() {
		var ui = load('ui', null);
		if (!ui) return;
		uiIds.forEach(function (id) { var el = $(id); if (el && ui[id] !== undefined) { if (el.type === 'checkbox') el.checked = ui[id]; else el.value = ui[id]; } });
		if (ui.radios) Object.keys(ui.radios).forEach(function (k) { if (ui.radios[k]) setRadio(k, ui.radios[k]); });
		if (ui.themes) Object.keys(ui.themes).forEach(function (k) { var el = $('th_' + k); if (el) el.checked = ui.themes[k]; });
	}

	// =========================================================================
	// Mapa
	// =========================================================================
	var view = load('view', { c: [48.1486, 17.1077], z: 14 });
	var map = L.map('map', { zoomControl: true, doubleClickZoom: false, maxZoom: 21 }).setView(view.c, view.z);
	map.on('moveend', function () { var c = map.getCenter(); store('view', { c: [c.lat, c.lng], z: map.getZoom() }); });
	var osm = L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 21, maxNativeZoom: 19, attribution: '© OpenStreetMap' });
	var orthoLayer = L.tileLayer.wms(Sources.config.ortho.url, { layers: '1', format: 'image/jpeg', version: '1.3.0', maxZoom: 21, attribution: '© ÚGKK SR' });
	var cadLayer = L.tileLayer.wms('https://kataster.skgeodesy.sk/eskn/services/NR/kn_wms_norm/MapServer/WmsServer', { layers: '1,2,3,5,8,10', format: 'image/png', transparent: true, version: '1.3.0', maxZoom: 21, zIndex: 300, attribution: '© ÚGKK SR' });
	var cadELayer = L.tileLayer.wms('https://kataster.skgeodesy.sk/eskn/services/NR/uo_wms_norm/MapServer/WmsServer', { layers: '0,2', format: 'image/png', transparent: true, version: '1.3.0', maxZoom: 21, zIndex: 301, attribution: '© ÚGKK SR' });
	osm.addTo(map);
	cadLayer.addTo(map);
	L.control.layers({ 'Mapa (OSM)': osm, 'Ortofoto ZBGIS': orthoLayer }, { 'Kataster C': cadLayer, 'Parcely E': cadELayer }, { position: 'topright', collapsed: true }).addTo(map);
	L.control.scale({ imperial: false, position: 'bottomright' }).addTo(map);

	// dlaždice z ArcGIS MapServer /export (siete Bratislavy)
	var ArcTile = L.TileLayer.extend({
		getTileUrl: function (c) {
			var size = this.getTileSize(), nw = c.scaleBy(size), se = nw.add(size);
			var crs = this._map.options.crs;
			var p1 = crs.project(this._map.unproject(nw, c.z)), p2 = crs.project(this._map.unproject(se, c.z));
			return this._url + '/export?bbox=' + [p1.x, p2.y, p2.x, p1.y].map(function (v) { return v.toFixed(2); }).join(',') +
				'&bboxSR=3857&imageSR=3857&size=' + size.x + ',' + size.y + '&dpi=96&format=png32&transparent=true&f=image';
		}
	});
	var baTiles = {};
	function baOverlay(theme) {
		if (!baTiles[theme.key]) baTiles[theme.key] = new ArcTile(Sources.baServiceUrl(theme), { maxZoom: 21, maxNativeZoom: 21, zIndex: 400, opacity: 0.95, attribution: '© Hlavné mesto SR Bratislava', tileSize: 512 });
		return baTiles[theme.key];
	}
	function updateBaOverlays() {
		Sources.BA_THEMES.forEach(function (t) {
			var on = $('th_' + t.key).checked && $('baShow').checked, lyr = baOverlay(t);
			if (on && !map.hasLayer(lyr)) lyr.addTo(map);
			if (!on && map.hasLayer(lyr)) map.removeLayer(lyr);
		});
	}

	var areaLayer = L.layerGroup().addTo(map);
	var drawLayer = L.layerGroup().addTo(map);
	var plotLayer = L.layerGroup().addTo(map);
	var originMarker = null;

	map.on('mousemove', function (e) {
		var j = toJtsk(e.latlng);
		$('coords').textContent = 'Y ' + fmt(-j[0], 2) + '   X ' + fmt(-j[1], 2);
	});

	// =========================================================================
	// Kreslenie územia
	// =========================================================================
	var tool = null, drawPts = [], rectStart = null;
	var hints = {
		rect: 'Kliknite na prvý a potom na protiľahlý roh obdĺžnika (Esc = zrušiť)',
		poly: 'Klikaním pridávajte body, dvojklik alebo klik na prvý bod ukončí polygón (Backspace = späť, Esc = zrušiť)',
		plot: 'Kliknutím na parcelu ju pridáte do pozemku, ďalším kliknutím odoberiete (Esc = koniec)',
		origin: 'Kliknite na miesto lokálneho počiatku'
	};
	function setTool(t) {
		tool = t;
		drawPts = [];
		rectStart = null;
		drawLayer.clearLayers();
		['toolRect', 'toolPoly', 'toolPlot', 'toolOrigin'].forEach(function (id) { $(id).classList.remove('active'); });
		var btn = { rect: 'toolRect', poly: 'toolPoly', plot: 'toolPlot', origin: 'toolOrigin' }[t];
		if (btn) $(btn).classList.add('active');
		$('mapHint').style.display = t ? 'block' : 'none';
		$('mapHint').textContent = t ? hints[t] : '';
		map.getContainer().style.cursor = t ? 'crosshair' : '';
	}
	$('toolRect').onclick = function () { setTool(tool === 'rect' ? null : 'rect'); };
	$('toolPoly').onclick = function () { setTool(tool === 'poly' ? null : 'poly'); };
	$('toolPlot').onclick = function () { setTool(tool === 'plot' ? null : 'plot'); };
	$('toolOrigin').onclick = function () { setTool(tool === 'origin' ? null : 'origin'); };
	$('btnPlotPick').onclick = function () { setTool('plot'); };
	$('btnOriginPick').onclick = function () { setTool('origin'); };

	function rectRing(a, b) {
		return [[Math.min(a[0], b[0]), Math.min(a[1], b[1])], [Math.max(a[0], b[0]), Math.min(a[1], b[1])], [Math.max(a[0], b[0]), Math.max(a[1], b[1])], [Math.min(a[0], b[0]), Math.max(a[1], b[1])], [Math.min(a[0], b[0]), Math.min(a[1], b[1])]];
	}
	function finishArea(ring) {
		ring = Geo.closeRing(ring);
		if (ring.length < 4 || Geo.area(ring) < 1) { setStatus('Územie je príliš malé', 'err'); return; }
		state.area = ring;
		state.terrain = null;
		updateTerrainBox();
		setTool(null);
		drawArea();
		saveUi();
	}
	map.on('click', function (e) {
		var j = toJtsk(e.latlng);
		if (tool === 'rect') {
			if (!rectStart) { rectStart = j; return; }
			finishArea(rectRing(rectStart, j));
		} else if (tool === 'poly') {
			if (drawPts.length >= 3) {
				var p0 = map.latLngToContainerPoint(toLatLng(drawPts[0])), pc = map.latLngToContainerPoint(e.latlng);
				if (p0.distanceTo(pc) < 10) { finishArea(drawPts.slice()); return; }
			}
			drawPts.push(j);
			redrawTemp(null);
		} else if (tool === 'origin') {
			setOrigin([Math.round(j[0] * 100) / 100, Math.round(j[1] * 100) / 100]);
			setTool(null);
		} else if (tool === 'plot') {
			pickParcel(j);
		}
	});
	map.on('dblclick', function () { if (tool === 'poly' && drawPts.length >= 3) finishArea(drawPts.slice()); });
	map.on('mousemove', function (e) {
		if (tool === 'rect' && rectStart) {
			drawLayer.clearLayers();
			L.polygon(rectRing(rectStart, toJtsk(e.latlng)).map(toLatLng), { color: '#e0522d', weight: 2, dashArray: '5,4', fillOpacity: 0.05, interactive: false }).addTo(drawLayer);
		} else if (tool === 'poly' && drawPts.length) {
			redrawTemp(toJtsk(e.latlng));
		}
	});
	function redrawTemp(cursor) {
		drawLayer.clearLayers();
		var pts = drawPts.concat(cursor ? [cursor] : []).map(toLatLng);
		if (pts.length >= 2) L.polyline(pts, { color: '#e0522d', weight: 2, dashArray: '5,4', interactive: false }).addTo(drawLayer);
		drawPts.forEach(function (p, i) { L.circleMarker(toLatLng(p), { radius: i ? 3 : 5, color: '#e0522d', fillOpacity: 1, interactive: false }).addTo(drawLayer); });
	}
	document.addEventListener('keydown', function (e) {
		if (e.target && /INPUT|TEXTAREA|SELECT/.test(e.target.tagName)) return;
		if (e.key === 'Escape') setTool(null);
		if (e.key === 'Backspace' && tool === 'poly' && drawPts.length) { drawPts.pop(); redrawTemp(null); e.preventDefault(); }
	});

	function areaCenter() {
		var b = Geo.bounds(state.area);
		return [Math.round((b.minE + b.maxE) / 2), Math.round((b.minN + b.maxN) / 2)];
	}
	function effectiveOrigin() { return state.origin || (state.area ? areaCenter() : null); }

	// hranice Bratislavy (obálka údajov technickej mapy)
	var baBox = (function () {
		var a = proj4('EPSG:3857', 'EPSG:5514', [1886304, 6106959]), b = proj4('EPSG:3857', 'EPSG:5514', [1925363, 6154234]);
		var c = proj4('EPSG:3857', 'EPSG:5514', [1886304, 6154234]), d = proj4('EPSG:3857', 'EPSG:5514', [1925363, 6106959]);
		return Geo.bounds([a, b, c, d]);
	})();
	function areaInBa() {
		if (!state.area) return true;
		var b = Geo.bounds(state.area);
		return !(b.maxE < baBox.minE || b.minE > baBox.maxE || b.maxN < baBox.minN || b.minN > baBox.maxN);
	}

	function drawArea() {
		areaLayer.clearLayers();
		if (!state.area) {
			$('areaBadge').textContent = 'nevybrané';
			$('areaInfo').textContent = 'Nástrojom ▭ Obdĺžnik alebo ⬡ Polygón nakreslite do mapy územie, pre ktoré sa podklady stiahnu.';
		} else {
			L.polygon(state.area.map(toLatLng), { color: '#e0522d', weight: 2, fillOpacity: 0.04, interactive: false }).addTo(areaLayer);
			var b = Geo.bounds(state.area), a = Geo.area(state.area);
			$('areaBadge').textContent = a > 10000 ? fmt(a / 10000, 2) + ' ha' : fmt(a, 0) + ' m²';
			$('areaInfo').textContent = 'Rozmer ' + fmt(b.maxE - b.minE, 1) + ' × ' + fmt(b.maxN - b.minN, 1) + ' m · ' + (state.area.length - 1) + ' vrcholov';
		}
		$('baOutside').style.display = areaInBa() ? 'none' : 'block';
		updateOriginInfo();
	}
	$('btnClearArea').onclick = function () { state.area = null; state.terrain = null; updateTerrainBox(); drawArea(); saveUi(); };

	function setOrigin(o) {
		state.origin = o;
		if (originMarker) { map.removeLayer(originMarker); originMarker = null; }
		if (o) {
			originMarker = L.marker(toLatLng(o), { draggable: true, title: 'Lokálny počiatok', icon: L.divIcon({ className: '', html: '<div style="font:700 22px/22px Segoe UI;color:#e0522d;text-shadow:0 0 3px #fff">✛</div>', iconSize: [22, 22], iconAnchor: [11, 11] }) }).addTo(map);
			originMarker.on('dragend', function () { var j = toJtsk(originMarker.getLatLng()); state.origin = [Math.round(j[0] * 100) / 100, Math.round(j[1] * 100) / 100]; updateOriginInfo(); saveUi(); });
		}
		updateOriginInfo();
		refreshReduceCount();
		saveUi();
	}
	function updateOriginInfo() {
		var o = effectiveOrigin();
		$('originInfo').innerHTML = o ? 'Počiatok' + (state.origin ? '' : ' <span class="muted">(stred územia)</span>') + ': <span class="num">Y ' + fmt(-o[0], 2) + '  X ' + fmt(-o[1], 2) + '</span>' : 'Počiatok: <span class="muted">stred územia</span>';
	}
	$('btnOriginClear').onclick = function () { setOrigin(null); };

	// =========================================================================
	// Pozemok – výber parciel kliknutím
	// =========================================================================
	async function pickParcel(j) {
		var hit = null, i;
		for (i = 0; i < state.plot.length; i++) {
			if (Geo.pointInPolygon(state.plot[i].rings[0], j[0], j[1])) { hit = i; break; }
		}
		if (hit !== null) { state.plot.splice(hit, 1); drawPlot(); saveUi(); return; }
		setBusy(true);
		setStatus('Hľadám parcelu…');
		try {
			var feats = await Sources.wfsFeatures(Sources.config.cadastreC, { minE: j[0] - 1, minN: j[1] - 1, maxE: j[0] + 1, maxN: j[1] + 1 }, { count: 50 });
			var f = feats.filter(function (x) { return x.rings[0] && Geo.pointInPolygon(x.rings[0], j[0], j[1]); })[0];
			if (!f) { setStatus('Na tomto mieste nie je parcela registra C', 'err'); return; }
			if (!state.plot.some(function (p) { return p.id === f.id; })) state.plot.push({ id: f.id, label: f.label, rings: f.rings, officialArea: f.officialArea });
			drawPlot();
			saveUi();
			setStatus('Parcela ' + f.label + ' pridaná', 'ok');
		} catch (e) {
			setStatus('Parcelu sa nepodarilo načítať: ' + e.message, 'err');
		} finally { setBusy(false); }
	}
	function drawPlot() {
		plotLayer.clearLayers();
		var list = $('plotList');
		list.innerHTML = '';
		state.plot.forEach(function (p, idx) {
			p.rings.forEach(function (r, i) { L.polygon(r.map(toLatLng), { color: '#1f5fbf', weight: 2, fillOpacity: i ? 0 : 0.18, interactive: false }).addTo(plotLayer); });
			var s = document.createElement('span');
			s.className = 'p';
			s.innerHTML = esc(p.label || p.id) + ' <span class="muted">' + fmt(Geo.area(p.rings[0]), 0) + ' m²</span><button title="odobrať" data-i="' + idx + '">×</button>';
			list.appendChild(s);
		});
		if (state.plot.length > 1) {
			Geo.outerBoundary(state.plot.map(function (p) { return p.rings[0]; })).forEach(function (r) {
				L.polygon(r.map(toLatLng), { color: '#1f5fbf', weight: 3.5, fill: false, interactive: false }).addTo(plotLayer);
			});
		}
		var total = 0;
		state.plot.forEach(function (p) { total += Geo.area(p.rings[0]); });
		$('plotBadge').textContent = state.plot.length ? state.plot.length + ' parc. · ' + fmt(total, 0) + ' m²' : 'nevybraný';
		[].forEach.call(list.querySelectorAll('button[data-i]'), function (b) {
			b.onclick = function () { state.plot.splice(Number(b.getAttribute('data-i')), 1); drawPlot(); saveUi(); };
		});
	}
	$('btnPlotClear').onclick = function () { state.plot = []; drawPlot(); saveUi(); };

	// =========================================================================
	// Vyhľadávanie
	// =========================================================================
	function parseCoords(qs) {
		var m = qs.replace(/,(?=\s)/g, ' ').replace(/;/g, ' ').trim().split(/\s+/).map(function (v) { return Number(v.replace(',', '.')); });
		if (m.length === 2 && !isNaN(m[0]) && !isNaN(m[1])) {
			if (Math.abs(m[0]) < 90 && Math.abs(m[1]) < 180) return L.latLng(m[0], m[1]);
			if (Math.abs(m[0]) > 1000 && Math.abs(m[1]) > 1000) return toLatLng(Geo.normalizeJtsk(m[0], m[1]));
		}
		return null;
	}
	function doSearch() {
		var qs = $('q').value.trim(), box = $('searchResults');
		if (!qs) return;
		var c = parseCoords(qs);
		if (c) { map.setView(c, 18); box.style.display = 'none'; return; }
		setStatus('Hľadám…');
		fetch(Sources.config.geocoder + '?format=json&limit=8&countrycodes=sk&accept-language=sk&q=' + encodeURIComponent(qs))
			.then(function (r) { return r.json(); }).then(function (list) {
				box.innerHTML = '';
				if (!list.length) box.innerHTML = '<div class="muted">Nič sa nenašlo.</div>';
				list.forEach(function (it) {
					var d = document.createElement('div');
					d.textContent = it.display_name;
					d.onclick = function () {
						box.style.display = 'none';
						if (it.boundingbox) { var bb = it.boundingbox.map(Number); map.fitBounds([[bb[0], bb[2]], [bb[1], bb[3]]], { maxZoom: 18 }); }
						else map.setView([+it.lat, +it.lon], 17);
					};
					box.appendChild(d);
				});
				box.style.display = 'block';
				setStatus('Pripravené.');
			}).catch(function (e) { setStatus('Vyhľadávanie zlyhalo: ' + e.message, 'err'); });
	}
	$('btnFind').onclick = doSearch;
	$('q').addEventListener('keydown', function (e) { if (e.key === 'Enter') doSearch(); });
	document.addEventListener('click', function (e) { if (!$('search').contains(e.target)) $('searchResults').style.display = 'none'; });

	// =========================================================================
	// Technická mapa Bratislava – témy a vrstvy
	// =========================================================================
	function buildThemes() {
		Sources.BA_THEMES.forEach(function (t) {
			var host = t.group === 'siete' ? $('baSiete') : $('baTm');
			var div = document.createElement('div');
			div.className = 'theme';
			div.id = 'theme_' + t.key;
			div.innerHTML = '<div class="head"><label><input type="checkbox" id="th_' + t.key + '"' + (t.group === 'siete' && t.key !== 'kolektor' && t.key !== 'produkt' ? ' checked' : '') + '>' +
				'<span class="sw" style="background:rgb(' + t.color.join(',') + ')"></span>' + esc(t.title) + '</label>' +
				'<button class="link" data-k="' + t.key + '">vrstvy ▾</button></div><div class="lyr" id="lyr_' + t.key + '"><span class="muted">načítavam…</span></div>';
			host.appendChild(div);
		});
		[].forEach.call(document.querySelectorAll('.theme .head button'), function (b) {
			b.onclick = function () { toggleThemeLayers(Sources.themeByKey(b.getAttribute('data-k'))); };
		});
		Sources.BA_THEMES.forEach(function (t) { $('th_' + t.key).addEventListener('change', function () { updateBaOverlays(); saveUi(); }); });
	}
	async function toggleThemeLayers(theme) {
		var div = $('theme_' + theme.key), box = $('lyr_' + theme.key);
		div.classList.toggle('open');
		if (!div.classList.contains('open') || state.baMeta[theme.key]) return;
		try {
			var meta = await Sources.baMeta(theme);
			state.baMeta[theme.key] = meta;
			renderThemeLayers(theme);
		} catch (e) {
			box.innerHTML = '<span style="color:var(--err)">' + esc(e.message) + '</span>';
		}
	}
	var kindNames = { line: 'línie', area: 'plochy', symbol: 'body', text: 'popisy' };
	function renderThemeLayers(theme) {
		var meta = state.baMeta[theme.key], box = $('lyr_' + theme.key), sel = state.baLayers[theme.key];
		box.innerHTML = '';
		meta.layers.forEach(function (L2) {
			var on = sel ? sel.indexOf(L2.id) >= 0 : L2.defaultOn;
			var lab = document.createElement('label');
			lab.innerHTML = '<input type="checkbox" data-id="' + L2.id + '"' + (on ? ' checked' : '') + '> ' + esc(L2.name) + ' <span class="k">' + (kindNames[L2.kind] || '') +
				(L2.renderer.classes.length ? ' · ' + L2.renderer.classes.length + ' typov' : '') + '</span>';
			box.appendChild(lab);
		});
		if (!meta.layers.length) box.innerHTML = '<span class="muted">Služba nemá vrstvy na export.</span>';
		[].forEach.call(box.querySelectorAll('input'), function (inp) {
			inp.onchange = function () {
				state.baLayers[theme.key] = [].filter.call(box.querySelectorAll('input'), function (x) { return x.checked; }).map(function (x) { return Number(x.getAttribute('data-id')); });
				saveUi();
			};
		});
	}
	function selectedLayerIds(theme, meta) {
		var sel = state.baLayers[theme.key];
		if (sel) return sel;
		return meta.layers.filter(function (l) { return l.defaultOn; }).map(function (l) { return l.id; });
	}

	// =========================================================================
	// Terén
	// =========================================================================
	function reductionParams() {
		return {
			enabled: $('redOn').checked, mode: Number(radio('redMode')),
			keepPercent: Number($('redKeep').value), nearPercent: Number($('redNear').value), farPercent: Number($('redFar').value),
			origin: effectiveOrigin() || [0, 0]
		};
	}
	function updateRedUi() {
		$('redBox').style.display = $('redOn').checked ? 'block' : 'none';
		var dist = radio('redMode') === '1';
		$('redUniform').style.display = dist ? 'none' : 'flex';
		$('redDistance').style.display = dist ? 'block' : 'none';
		$('redKeepVal').textContent = $('redKeep').value + ' %';
		$('redNearVal').textContent = $('redNear').value + ' %';
		$('redFarVal').textContent = $('redFar').value + ' %';
	}
	var refreshReduceCount = debounce(function () {
		if (!state.terrain) return;
		var k = Geo.reducePoints(state.terrain, reductionParams());
		$('redCount').textContent = k.length.toLocaleString('sk-SK');
		if (preview.visible) { preview.pts = samplePreview(k); render3d(); }
	}, 150);
	['redOn', 'redKeep', 'redNear', 'redFar'].forEach(function (id) {
		$(id).addEventListener('input', function () { updateRedUi(); refreshReduceCount(); });
		$(id).addEventListener('change', function () { updateRedUi(); refreshReduceCount(); saveUi(); });
	});
	[].forEach.call(document.querySelectorAll('input[name=redMode]'), function (el) { el.addEventListener('change', function () { updateRedUi(); refreshReduceCount(); saveUi(); }); });

	function updateTerrainBox() {
		var t = state.terrain;
		$('terrainBox').style.display = t ? 'block' : 'none';
		$('terrainBadge').textContent = t ? t.count.toLocaleString('sk-SK') + ' bodov' : 'nenačítaný';
		if (t) {
			$('terrainInfo').textContent = t.source + ' · rozostup ' + fmt(t.spacing, 2) + ' m · výšky ' + fmt(t.minZ, 2) + ' – ' + fmt(t.maxZ, 2) + ' m n. m.' + (t.grid ? '' : ' · nepravidelné body (bez vrstevníc)');
			refreshReduceCount();
		} else {
			preview.visible = false;
			$('preview3d').style.display = 'none';
		}
	}

	async function loadTerrain() {
		var area = state.area;
		if (!area) throw new Error('Najprv vyberte územie');
		var src = radio('terSrc'), raw;
		if (src === 'file') {
			var files = $('terFile').files;
			if (!files || !files.length) throw new Error('Vyberte súbor(y) s terénom');
			var parts = [];
			for (var i = 0; i < files.length; i++) {
				setStatus('Čítam ' + files[i].name + '…');
				parts.push(await Sources.terrainFromFile(files[i]));
			}
			raw = Geo.mergeTerrains(parts);
		} else {
			if (!areaInBa()) throw new Error('Výškový model mesta pokrýva len Bratislavu – pre iné územie použite súbor DMR 5.0');
			raw = await Sources.baElevation(Geo.expand(Geo.bounds(area), Number($('terRes').value)), Number($('terRes').value), setStatus);
		}
		var t = Geo.clipTerrain(raw, area, raw.spacing * 0.75);
		if (!t.count) throw new Error('Terén neobsahuje body vo vybranom území (súbor je z iného miesta alebo v inej sústave?)');
		state.terrain = t;
		updateTerrainBox();
		if (!(Number($('h0').value) > 0)) suggestH0();
		return t;
	}
	$('btnLoadTerrain').onclick = async function () {
		setBusy(true);
		try {
			var t = await loadTerrain();
			$('itTerrain').checked = true;
			saveUi();
			setStatus('Terén načítaný: ' + t.count.toLocaleString('sk-SK') + ' bodov', 'ok');
		} catch (e) { setStatus(e.message, 'err'); } finally { setBusy(false); }
	};
	function suggestH0() {
		var o = effectiveOrigin();
		if (!state.terrain || !o) return false;
		var z = Geo.heightAt(state.terrain, o[0], o[1]);
		if (!isFinite(z)) return false;
		$('h0').value = (Math.round(z * 100) / 100).toFixed(2);
		saveUi();
		return true;
	}
	$('btnH0').onclick = function () { if (!suggestH0()) setStatus('Najprv načítajte terén', 'err'); };

	// --- náhľad 3D ---
	var preview = { pts: null, yaw: -0.6, pitch: 0.9, zoom: 1, exag: 1.5, visible: false };
	function samplePreview(keep) {
		var t = state.terrain, step = Math.max(1, Math.ceil(keep.length / 15000)), pts = [];
		for (var i = 0; i < keep.length; i += step) pts.push([t.e[keep[i]], t.n[keep[i]], t.z[keep[i]]]);
		return pts;
	}
	$('btnPreview3d').onclick = function () {
		if (!state.terrain) return;
		preview.visible = !preview.visible;
		$('preview3d').style.display = preview.visible ? 'block' : 'none';
		if (preview.visible) { preview.pts = samplePreview(Geo.reducePoints(state.terrain, reductionParams())); render3d(); }
	};
	function render3d() {
		var cv = $('preview3d'), dpr = window.devicePixelRatio || 1;
		var w = cv.clientWidth, h = cv.clientHeight;
		cv.width = w * dpr;
		cv.height = h * dpr;
		var ctx = cv.getContext('2d');
		ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
		ctx.clearRect(0, 0, w, h);
		var pts = preview.pts;
		if (!pts || !pts.length) return;
		var minE = Infinity, maxE = -Infinity, minN = Infinity, maxN = -Infinity, minZ = Infinity, maxZ = -Infinity;
		pts.forEach(function (p) { minE = Math.min(minE, p[0]); maxE = Math.max(maxE, p[0]); minN = Math.min(minN, p[1]); maxN = Math.max(maxN, p[1]); minZ = Math.min(minZ, p[2]); maxZ = Math.max(maxZ, p[2]); });
		var cE = (minE + maxE) / 2, cN = (minN + maxN) / 2, cZ = (minZ + maxZ) / 2;
		var size = Math.max(maxE - minE, maxN - minN) || 1;
		var s = Math.min(w, h) / size * 0.8 * preview.zoom;
		var cy = Math.cos(preview.yaw), sy = Math.sin(preview.yaw), cp = Math.cos(preview.pitch), sp = Math.sin(preview.pitch);
		var proj = pts.map(function (p) {
			var x = p[0] - cE, y = p[1] - cN, z = (p[2] - cZ) * preview.exag;
			var rx = x * cy - y * sy, ry = x * sy + y * cy;
			return [w / 2 + rx * s, h / 2 - (z * cp - ry * sp) * s, ry * cp + z * sp, (p[2] - minZ) / ((maxZ - minZ) || 1)];
		});
		proj.sort(function (a, b) { return b[2] - a[2]; });
		var r = Math.max(1.2, Math.min(3, s * Math.sqrt(size * size / pts.length) * 0.6));
		proj.forEach(function (q) {
			var t = q[3];
			ctx.fillStyle = 'rgb(' + Math.round(60 + 150 * t) + ',' + Math.round(120 + 60 * (1 - Math.abs(t - 0.5) * 2)) + ',' + Math.round(70 + 40 * (1 - t)) + ')';
			ctx.fillRect(q[0] - r / 2, q[1] - r / 2, r, r);
		});
		ctx.fillStyle = '#1f2421';
		ctx.font = '11px Segoe UI, sans-serif';
		ctx.fillText('ťahaním otáčajte · koliesko = zoom · dvojklik = prevýšenie ×' + preview.exag, 8, h - 8);
	}
	(function () {
		var cv = $('preview3d'), drag = null;
		cv.addEventListener('mousedown', function (e) { drag = { x: e.clientX, y: e.clientY }; cv.style.cursor = 'grabbing'; });
		window.addEventListener('mouseup', function () { drag = null; cv.style.cursor = 'grab'; });
		window.addEventListener('mousemove', function (e) {
			if (!drag) return;
			preview.yaw += (e.clientX - drag.x) * 0.01;
			preview.pitch = Math.max(0.1, Math.min(1.5, preview.pitch + (e.clientY - drag.y) * 0.01));
			drag = { x: e.clientX, y: e.clientY };
			render3d();
		});
		cv.addEventListener('wheel', function (e) { e.preventDefault(); preview.zoom *= e.deltaY < 0 ? 1.1 : 0.9; render3d(); }, { passive: false });
		cv.addEventListener('dblclick', function () { preview.exag = preview.exag >= 4 ? 1 : preview.exag + 0.5; render3d(); });
	})();

	// =========================================================================
	// Export
	// =========================================================================
	function logResults(list) {
		var ul = $('log');
		ul.innerHTML = '';
		if (!list.length) { ul.innerHTML = '<li class="muted">Nič nebolo vybrané.</li>'; return; }
		list.forEach(function (it) {
			var li = document.createElement('li');
			li.innerHTML = '<b class="' + (it.ok ? 'ok' : 'err') + '">' + (it.ok ? '✓' : '✗') + ' ' + esc(it.item) + '</b> <span class="muted">' + esc(it.message || '') + '</span>';
			ul.appendChild(li);
		});
	}

	function stamp() {
		var d = new Date();
		function p(n) { return (n < 10 ? '0' : '') + n; }
		return d.getFullYear() + p(d.getMonth() + 1) + p(d.getDate()) + '_' + p(d.getHours()) + p(d.getMinutes());
	}

	async function doExport() {
		var errors = [];
		if (!state.area && state.plot.length) {
			var pb = null;
			state.plot.forEach(function (p) { pb = Geo.boundsUnion(pb, Geo.bounds(p.rings[0])); });
			state.area = Geo.bboxRing(Geo.expand(pb, 30));
			drawArea();
		}
		if (!state.area) throw new Error('Najprv vyberte územie (▭ Obdĺžnik / ⬡ Polygón) alebo pozemok');
		var area = state.area, bbox = Geo.bounds(area), origin = effectiveOrigin();
		var job = {
			area: area, origin: origin, h0: Number($('h0').value) || 0, coords: radio('coords'), textHeight: Number($('textH').value) || 0.5,
			clip: $('optClip').checked, separate: $('optSeparate').checked, drawArea: $('optArea').checked, originMarker: $('optOrigin').checked,
			layers: settings.layers, ba: [], baOpts: { splitByType: $('baSplit').checked, splitConnections: $('baConn').checked, symbolSize: Number($('symSize').value) || 0.5 },
			xyzPositive: $('xyzPositive').checked, usesUgkk: false, usesBa: false
		};
		var sqkm = Geo.area(area) / 1e6;

		async function step(name, fn) {
			try { await fn(); } catch (e) { errors.push({ item: name, ok: false, message: e.message }); }
		}

		if ($('itCadC').checked) await step('Kataster – parcely C', async function () {
			setStatus('Sťahujem parcely C…');
			job.cadastreC = await Sources.wfsFeatures(Sources.config.cadastreC, Geo.expand(bbox, 2));
			job.usesUgkk = true;
		});
		if ($('itCadE').checked) await step('Kataster – parcely E', async function () {
			setStatus('Sťahujem parcely E…');
			job.cadastreE = await Sources.wfsFeatures(Sources.config.cadastreE, Geo.expand(bbox, 2));
			job.usesUgkk = true;
		});
		if (state.plot.length) {
			var plotRings = state.plot.map(function (p) { return p.rings[0]; });
			if ($('itPlot').checked) { job.plotOutline = Geo.outerBoundary(plotRings); job.usesUgkk = true; }
			if ($('itPlotReport').checked) await step('Report o pozemku', async function () {
				setStatus('Hľadám susedné parcely…');
				var pbb = Geo.expand(Geo.bounds([].concat.apply([], plotRings)), 25), around = [], err = '';
				try { around = await Sources.wfsFeatures(Sources.config.cadastreC, pbb); } catch (e) { err = e.message; }
				var ids = state.plot.map(function (p) { return p.id; });
				var neighbours = around.filter(function (f) {
					if (ids.indexOf(f.id) >= 0 || !f.rings.length) return false;
					return plotRings.some(function (r) { return Geo.ringsTouch(f.rings[0], r, 0.05); });
				});
				job.plot = state.plot;
				job.plotNeighbours = neighbours.length;
				job.plotReport = Exporter.plotReport(state.plot, neighbours, job.plotOutline || Geo.outerBoundary(plotRings), err);
				job.usesUgkk = true;
			});
		}
		if ($('itBuildings').checked) await step('Budovy ZBGIS', async function () {
			setStatus('Sťahujem budovy ZBGIS…');
			job.buildings = await Sources.wfsFeatures(Sources.config.buildings, Geo.expand(bbox, 2), { typeHint: /Building/i });
			job.usesUgkk = true;
		});

		var themes = Sources.BA_THEMES.filter(function (t) { return $('th_' + t.key).checked; });
		if (themes.length && !areaInBa()) {
			errors.push({ item: 'Technická mapa Bratislava', ok: false, message: 'územie je mimo Bratislavy – preskočené' });
		} else if (themes.length) {
			if (sqkm > 4) errors.push({ item: 'Technická mapa Bratislava', ok: false, message: 'územie má ' + fmt(sqkm, 1) + ' km² – siete sa sťahujú len do 4 km²' });
			else {
				var modes = {};
				for (var i = 0; i < themes.length; i++) {
					var t = themes[i];
					await step(t.title + ' (BA)', async function () {
						var meta = state.baMeta[t.key] || (state.baMeta[t.key] = await Sources.baMeta(t));
						var res = await Sources.baDownload(t, selectedLayerIds(t, meta), bbox, setStatus);
						job.ba.push(res);
						job.usesBa = true;
						modes[res.mode] = 1;
					});
				}
				var mtxt = { native: 'služba je priamo v S-JTSK', server: 'server mesta (transformácia uvedená v službe: S-JTSK → JTSK03 → WGS84)', local: 'lokálne proj4 (JTSK03 → WGS84, presnosť cca do 1 m)' };
				job.baModes = Object.keys(modes).map(function (m) { return mtxt[m] || m; }).join('; ');
			}
		}

		if ($('itTerrain').checked) await step('Terén', async function () {
			if (!state.terrain) { setStatus('Načítavam terén…'); await loadTerrain(); }
			var t = state.terrain, keep = Geo.reducePoints(t, reductionParams());
			job.terrain = { t: t, keep: keep, interval: Number($('terInterval').value), majorEvery: Number($('terMajor').value) };
			if ($('terContours').checked && t.grid) {
				setStatus('Počítam vrstevnice…');
				await new Promise(function (r) { setTimeout(r, 10); });
				var cs = Geo.contours(t, job.terrain.interval, { minLength: t.spacing * 2 });
				if (job.clip) {
					var cb = Geo.bounds(area), clipped = [];
					cs.forEach(function (c) { Geo.clipPath(c.path, area, cb).forEach(function (p) { clipped.push({ level: c.level, path: p }); }); });
					cs = clipped;
				}
				job.terrain.contours = cs;
			}
			if ($('terFaces').checked) {
				if (keep.length > 150000) errors.push({ item: 'Terén 3D (3DFACE)', ok: false, message: keep.length.toLocaleString('sk-SK') + ' bodov je na 3D sieť priveľa – zredukujte na menej ako 150 000' });
				else {
					setStatus('Triangulujem terén…');
					await new Promise(function (r) { setTimeout(r, 10); });
					job.terrain.faces = Exporter.terrainFaces(t, keep, area);
				}
			}
			if (t.source.indexOf('Bratislava') >= 0) job.usesBa = true; else job.usesUgkk = true;
		});

		if ($('itOrtho').checked) await step('Ortofoto', async function () {
			job.ortho = await Sources.orthoImage(bbox, $('orthoHQ').checked, setStatus);
			job.usesUgkk = true;
		});

		setStatus('Zapisujem súbory…');
		await new Promise(function (r) { setTimeout(r, 10); });
		var out = Exporter.build(job);
		var zip = new JSZip();
		out.files.forEach(function (f) { zip.file(f.name, f.data); });
		setStatus('Balím ZIP…');
		var blob = await zip.generateAsync({ type: 'blob', compression: 'DEFLATE', compressionOptions: { level: 6 } });
		var name = 'podklady_' + stamp() + '.zip';
		var a = document.createElement('a');
		a.href = URL.createObjectURL(blob);
		a.download = name;
		document.body.appendChild(a);
		a.click();
		setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 4000);
		logResults(out.log.concat(errors));
		window.PMD_lastExport = { files: out.files.map(function (f) { return f.name; }), log: out.log.concat(errors), size: blob.size, name: name };
		return { name: name, size: blob.size, errors: errors.length };
	}

	$('btnExport').onclick = async function () {
		setBusy(true);
		setTool(null);
		try {
			var r = await doExport();
			setStatus('Stiahnuté: ' + r.name + ' (' + fmt(r.size / 1048576, 1) + ' MB)' + (r.errors ? ' – ' + r.errors + ' položiek s chybou, pozri Výsledok' : ''), r.errors ? 'err' : 'ok');
		} catch (e) {
			setStatus(e.message, 'err');
			console.error(e);
		} finally { setBusy(false); saveUi(); }
	};

	// =========================================================================
	// Nastavenia (modálne okno)
	// =========================================================================
	function openSettings() {
		var tb = $('layerRows');
		tb.innerHTML = '';
		Object.keys(layerLabels).forEach(function (k) {
			var L2 = settings.layers[k];
			var tr = document.createElement('tr');
			tr.innerHTML = '<td>' + esc(layerLabels[k]) + '</td><td><input type="text" data-k="' + k + '" data-f="name" value="' + esc(L2.name) + '"></td>' +
				'<td><input type="number" min="1" max="255" data-k="' + k + '" data-f="color" value="' + L2.color + '"></td>';
			tb.appendChild(tr);
		});
		$('setProxy').value = settings.proxy;
		$('setProxyKey').value = settings.proxyKey || '';
		$('settingsModal').classList.add('open');
	}
	$('btnSettings').onclick = openSettings;
	$('btnCancelSettings').onclick = function () { $('settingsModal').classList.remove('open'); };
	$('btnResetSettings').onclick = function () { settings = JSON.parse(JSON.stringify(defaultSettings)); openSettings(); };
	$('btnSaveSettings').onclick = function () {
		[].forEach.call(document.querySelectorAll('#layerRows input'), function (inp) {
			var k = inp.getAttribute('data-k'), f = inp.getAttribute('data-f');
			settings.layers[k][f] = f === 'color' ? Math.max(1, Math.min(255, Number(inp.value) || 7)) : (inp.value.trim() || defaultSettings.layers[k].name);
		});
		settings.proxy = $('setProxy').value.trim();
		settings.proxyKey = $('setProxyKey').value.trim();
		store('settings', settings);
		applySettings();
		$('settingsModal').classList.remove('open');
		setStatus('Nastavenia uložené', 'ok');
	};
	[].forEach.call(document.querySelectorAll('.tabs button'), function (b) {
		b.onclick = function () {
			[].forEach.call(document.querySelectorAll('.tabs button'), function (x) { x.classList.remove('active'); $(x.getAttribute('data-tab')).style.display = 'none'; });
			b.classList.add('active');
			$(b.getAttribute('data-tab')).style.display = 'block';
		};
	});
	$('btnTest').onclick = async function () {
		var box = $('testResults'), cfg = Sources.config;
		box.innerHTML = '';
		var probes = [
			['Kataster C (WFS)', cfg.cadastreC.url + '?SERVICE=WFS&VERSION=2.0.0&REQUEST=GetCapabilities', 'xml'],
			['Kataster E (WFS)', cfg.cadastreE.url + '?SERVICE=WFS&VERSION=2.0.0&REQUEST=GetCapabilities', 'xml'],
			['Budovy ZBGIS (WFS)', cfg.buildings.url + '?SERVICE=WFS&VERSION=2.0.0&REQUEST=GetCapabilities', 'xml'],
			['Ortofoto (WMS)', cfg.ortho.url + '?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetCapabilities', 'xml'],
			['Bratislava – vodovody', Sources.baServiceUrl(Sources.themeByKey('voda')) + '?f=json', 'json'],
			['Bratislava – výškový model', cfg.baRoot + Sources.encPath(cfg.baElevation) + '?f=json', 'json']
		];
		for (var i = 0; i < probes.length; i++) {
			var div = document.createElement('div');
			div.className = 'test-item';
			box.appendChild(div);
			var t0 = Date.now();
			try {
				var txt = await Sources.getText(probes[i][1], 30000);
				var ex = Geo.ogcException(txt);
				if (ex) throw new Error(ex);
				var info = probes[i][2] === 'xml' ? (Geo.capabilityNames(txt).slice(0, 3).join(', ') || txt.length + ' B') : (JSON.parse(txt).currentVersion ? 'ArcGIS ' + JSON.parse(txt).currentVersion : 'OK');
				div.innerHTML = '<b class="ok">✓ ' + esc(probes[i][0]) + '</b> <span class="muted">' + (Date.now() - t0) + ' ms · ' + esc(info) + '</span>';
			} catch (e) {
				div.innerHTML = '<b class="err">✗ ' + esc(probes[i][0]) + '</b> <span class="muted">' + esc(e.message) + '</span>';
			}
		}
		var p = document.createElement('div');
		p.className = 'test-item muted';
		p.textContent = Sources.proxyAvailable() ? 'Spojenie cez proxy: ' + Sources.config.proxy : 'Proxy nie je k dispozícii – priame spojenie z prehliadača.';
		box.appendChild(p);
	};

	// =========================================================================
	// Štart
	// =========================================================================
	buildThemes();
	restoreUi();
	updateRedUi();
	updateBaOverlays();
	['itCadC', 'itCadE', 'itPlot', 'itBuildings', 'itOrtho', 'orthoHQ', 'itPlotReport', 'baSplit', 'baConn', 'itTerrain', 'terContours', 'terFaces', 'xyzPositive', 'optClip', 'optSeparate', 'optArea', 'optOrigin', 'terInterval', 'terMajor', 'terRes', 'h0', 'textH', 'symSize']
		.forEach(function (id) { $(id).addEventListener('change', saveUi); });
	[].forEach.call(document.querySelectorAll('input[name=coords], input[name=terSrc]'), function (el) { el.addEventListener('change', saveUi); });
	$('baShow').addEventListener('change', function () { updateBaOverlays(); saveUi(); });
	$('terFile').addEventListener('change', function () { setRadio('terSrc', 'file'); saveUi(); });
	drawArea();
	drawPlot();
	if (state.origin) setOrigin(state.origin);
	if (state.area && !load('view', null)) map.fitBounds(L.polygon(state.area.map(toLatLng)).getBounds());

	window.PMD = { state: state, doExport: doExport, map: map, toLatLng: toLatLng, toJtsk: toJtsk, finishArea: finishArea };
})();
