/*
 * Pilon Map Downloader – zápis DXF R12 (AC1009), kódová stránka ANSI_1250
 * R12 načíta Archicad 23 aj všetky bežné CAD programy. Jednotky: metre.
 */
(function (root) {
	'use strict';

	// --- farby AutoCAD ACI --------------------------------------------------------
	var ACI = [[0, 0, 0], [255, 0, 0], [255, 255, 0], [0, 255, 0], [0, 255, 255], [0, 0, 255], [255, 0, 255], [0, 0, 0], [128, 128, 128], [192, 192, 192]];
	(function () {
		var vals = [255, 255, 204, 204, 153, 153, 127, 127, 76, 76];
		for (var i = 10; i < 250; i++) {
			var h = Math.floor((i - 10) / 10) * 15, sub = (i - 10) % 10, v = vals[sub] / 255, s = sub % 2 ? 0.5 : 1;
			var c = v * s, x = c * (1 - Math.abs((h / 60) % 2 - 1)), m = v - c, r, g, b;
			if (h < 60) { r = c; g = x; b = 0; } else if (h < 120) { r = x; g = c; b = 0; } else if (h < 180) { r = 0; g = c; b = x; }
			else if (h < 240) { r = 0; g = x; b = c; } else if (h < 300) { r = x; g = 0; b = c; } else { r = c; g = 0; b = x; }
			ACI[i] = [Math.round((r + m) * 255), Math.round((g + m) * 255), Math.round((b + m) * 255)];
		}
		[51, 91, 132, 173, 214, 255].forEach(function (v, k) { ACI[250 + k] = [v, v, v]; });
	})();

	function rgbToAci(rgb) {
		if (!rgb) return 7;
		var r = rgb[0], g = rgb[1], b = rgb[2];
		if (Math.max(r, g, b) < 40 || Math.min(r, g, b) > 235) return 7;
		var best = 7, bd = Infinity;
		for (var i = 1; i < 256; i++) {
			if (i === 7) continue;
			var c = ACI[i], dr = c[0] - r, dg = c[1] - g, db = c[2] - b;
			var d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
			if (d < bd) { bd = d; best = i; }
		}
		return best;
	}

	// --- cp1250 ------------------------------------------------------------------
	var CP1250 = '€�‚�„…†‡�‰Š‹ŚŤŽŹ�‘’“”•–—�™š›śťžź' +
		' ˇ˘Ł¤Ą¦§¨©Ş«¬­®Ż°±˛ł´µ¶·¸ąş»Ľ˝ľż' +
		'ŔÁÂĂÄĹĆÇČÉĘËĚÍÎĎĐŃŇÓÔŐÖ×ŘŮÚŰÜÝŢß' +
		'ŕáâăäĺćçčéęëěíîďđńňóôőö÷řůúűüýţ˙';
	var ENC = {};
	for (var ci = 0; ci < CP1250.length; ci++) if (CP1250[ci] !== '�') ENC[CP1250[ci]] = 0x80 + ci;

	function encode1250(str) {
		var buf = new Uint8Array(str.length + 16), len = 0;
		function put(b) {
			if (len >= buf.length) { var nb = new Uint8Array(buf.length * 2); nb.set(buf); buf = nb; }
			buf[len++] = b;
		}
		for (var i = 0; i < str.length; i++) {
			var code = str.charCodeAt(i);
			if (code < 0x80) { put(code); continue; }
			var b = ENC[str[i]];
			if (b !== undefined) { put(b); continue; }
			var esc = '\\U+' + ('0000' + code.toString(16).toUpperCase()).slice(-4);
			for (var j = 0; j < esc.length; j++) put(esc.charCodeAt(j));
		}
		return buf.subarray(0, len);
	}

	// --- DXF -----------------------------------------------------------------------
	function layerName(s) {
		var map = { 'á': 'a', 'ä': 'a', 'č': 'c', 'ď': 'd', 'é': 'e', 'ě': 'e', 'í': 'i', 'ĺ': 'l', 'ľ': 'l', 'ň': 'n', 'ó': 'o', 'ô': 'o', 'ŕ': 'r', 'ř': 'r', 'š': 's', 'ť': 't', 'ú': 'u', 'ů': 'u', 'ý': 'y', 'ž': 'z' };
		var out = String(s).replace(/[^\u0000-\u007f]/g, function (c) {
			var l = c.toLowerCase(), r = map[l] || '_';
			return c === l ? r : r.toUpperCase();
		}).replace(/[^A-Za-z0-9_\-$]+/g, '_').replace(/_+/g, '_').replace(/^_+|_+$/g, '');
		return (out || 'VRSTVA').substr(0, 31);
	}

	var LTYPES = {
		CONTINUOUS: { desc: 'Solid line', pattern: [] },
		DASHED: { desc: '__ __ __', pattern: [1.0, -0.5] },
		DOT: { desc: '. . . .', pattern: [0.0, -0.4] },
		DASHDOT: { desc: '__ . __ .', pattern: [1.2, -0.3, 0.0, -0.3] }
	};

	function Dxf(opts) {
		opts = opts || {};
		this.offE = opts.offsetE || 0;	// x = E - offE
		this.offN = opts.offsetN || 0;
		this.offZ = opts.offsetZ || 0;
		this.layers = { '0': { color: 7, ltype: 'CONTINUOUS' } };
		this.order = ['0'];
		this.ents = [];
		this.ext = { minX: Infinity, minY: Infinity, maxX: -Infinity, maxY: -Infinity };
		this.count = 0;
	}

	Dxf.rgbToAci = rgbToAci;
	Dxf.layerName = layerName;
	Dxf.encode1250 = encode1250;

	Dxf.prototype.layer = function (name, color, ltype) {
		var n = layerName(name);
		if (!this.layers[n]) {
			this.layers[n] = { color: color || 7, ltype: LTYPES[ltype] ? ltype : 'CONTINUOUS' };
			this.order.push(n);
		}
		return n;
	};

	Dxf.prototype.hasContent = function () { return this.count > 0; };

	function f(v) {
		var s = (Math.round(v * 10000) / 10000).toString();
		return s.indexOf('e') >= 0 ? v.toFixed(4) : s;
	}

	Dxf.prototype._xy = function (p) {
		var x = (p[0] !== undefined ? p[0] : p.e) - this.offE, y = (p[1] !== undefined ? p[1] : p.n) - this.offN;
		if (x < this.ext.minX) this.ext.minX = x;
		if (x > this.ext.maxX) this.ext.maxX = x;
		if (y < this.ext.minY) this.ext.minY = y;
		if (y > this.ext.maxY) this.ext.maxY = y;
		return [x, y];
	};

	Dxf.prototype._push = function (s) { this.ents.push(s); this.count++; };

	Dxf.prototype.polyline = function (layer, pts, closed, elevation) {
		if (!pts || pts.length < 2) return;
		var n = pts.length;
		// uzavretý polygón bez zopakovaného bodu
		if (closed && n > 2 && pts[0][0] === pts[n - 1][0] && pts[0][1] === pts[n - 1][1]) n--;
		var z = elevation !== undefined && elevation !== null ? elevation - this.offZ : 0;
		var s = '0\nPOLYLINE\n8\n' + layer + '\n66\n1\n10\n0\n20\n0\n30\n' + f(z) + '\n70\n' + (closed ? 1 : 0) + '\n';
		for (var i = 0; i < n; i++) {
			var q = this._xy(pts[i]);
			s += '0\nVERTEX\n8\n' + layer + '\n10\n' + f(q[0]) + '\n20\n' + f(q[1]) + '\n30\n' + f(z) + '\n';
		}
		s += '0\nSEQEND\n8\n' + layer + '\n';
		this._push(s);
	};

	Dxf.prototype.line = function (layer, a, b) {
		var p = this._xy(a), q = this._xy(b);
		this._push('0\nLINE\n8\n' + layer + '\n10\n' + f(p[0]) + '\n20\n' + f(p[1]) + '\n30\n0\n11\n' + f(q[0]) + '\n21\n' + f(q[1]) + '\n31\n0\n');
	};

	Dxf.prototype.point = function (layer, p, z) {
		var q = this._xy(p);
		this._push('0\nPOINT\n8\n' + layer + '\n10\n' + f(q[0]) + '\n20\n' + f(q[1]) + '\n30\n' + f(z !== undefined ? z - this.offZ : 0) + '\n');
	};

	Dxf.prototype.circle = function (layer, p, r) {
		var q = this._xy(p);
		this._push('0\nCIRCLE\n8\n' + layer + '\n10\n' + f(q[0]) + '\n20\n' + f(q[1]) + '\n30\n0\n40\n' + f(r) + '\n');
	};

	// align: 'left' (vľavo dole) | 'center' (stred)
	Dxf.prototype.text = function (layer, p, height, str, rotation, align) {
		str = String(str === undefined || str === null ? '' : str).replace(/[\r\n]+/g, ' ').trim();
		if (!str) return;
		var q = this._xy(p);
		var s = '0\nTEXT\n8\n' + layer + '\n10\n' + f(q[0]) + '\n20\n' + f(q[1]) + '\n30\n0\n40\n' + f(height) + '\n1\n' + str + '\n';
		if (rotation) s += '50\n' + f(rotation) + '\n';
		s += '7\nSTANDARD\n';
		if (align === 'center') s += '72\n1\n11\n' + f(q[0]) + '\n21\n' + f(q[1]) + '\n31\n0\n73\n2\n';
		this._push(s);
	};

	Dxf.prototype.face = function (layer, a, b, c) {
		var A = this._xy(a), B = this._xy(b), C = this._xy(c), oz = this.offZ;
		this._push('0\n3DFACE\n8\n' + layer +
			'\n10\n' + f(A[0]) + '\n20\n' + f(A[1]) + '\n30\n' + f(a[2] - oz) +
			'\n11\n' + f(B[0]) + '\n21\n' + f(B[1]) + '\n31\n' + f(b[2] - oz) +
			'\n12\n' + f(C[0]) + '\n22\n' + f(C[1]) + '\n32\n' + f(c[2] - oz) +
			'\n13\n' + f(C[0]) + '\n23\n' + f(C[1]) + '\n33\n' + f(c[2] - oz) + '\n');
	};

	Dxf.prototype.toString = function () {
		var ext = this.ext;
		if (!isFinite(ext.minX)) ext = { minX: 0, minY: 0, maxX: 1, maxY: 1 };
		var s = '999\nPilon Map Downloader\n0\nSECTION\n2\nHEADER\n' +
			'9\n$ACADVER\n1\nAC1009\n9\n$DWGCODEPAGE\n3\nANSI_1250\n' +
			'9\n$INSBASE\n10\n0\n20\n0\n30\n0\n' +
			'9\n$EXTMIN\n10\n' + f(ext.minX) + '\n20\n' + f(ext.minY) + '\n30\n0\n' +
			'9\n$EXTMAX\n10\n' + f(ext.maxX) + '\n20\n' + f(ext.maxY) + '\n30\n0\n' +
			'9\n$LIMMIN\n10\n' + f(ext.minX) + '\n20\n' + f(ext.minY) + '\n' +
			'9\n$LIMMAX\n10\n' + f(ext.maxX) + '\n20\n' + f(ext.maxY) + '\n' +
			'9\n$LTSCALE\n40\n1\n9\n$TEXTSTYLE\n7\nSTANDARD\n9\n$CLAYER\n8\n0\n' +
			'0\nENDSEC\n0\nSECTION\n2\nTABLES\n';
		// LTYPE
		var lt = Object.keys(LTYPES);
		s += '0\nTABLE\n2\nLTYPE\n70\n' + lt.length + '\n';
		lt.forEach(function (name) {
			var p = LTYPES[name].pattern, total = 0;
			p.forEach(function (v) { total += Math.abs(v); });
			s += '0\nLTYPE\n2\n' + name + '\n70\n0\n3\n' + LTYPES[name].desc + '\n72\n65\n73\n' + p.length + '\n40\n' + f(total) + '\n';
			p.forEach(function (v) { s += '49\n' + f(v) + '\n'; });
		});
		s += '0\nENDTAB\n';
		// LAYER
		var self = this;
		s += '0\nTABLE\n2\nLAYER\n70\n' + this.order.length + '\n';
		this.order.forEach(function (name) {
			var L = self.layers[name];
			s += '0\nLAYER\n2\n' + name + '\n70\n0\n62\n' + L.color + '\n6\n' + L.ltype + '\n';
		});
		s += '0\nENDTAB\n';
		// STYLE
		s += '0\nTABLE\n2\nSTYLE\n70\n1\n0\nSTYLE\n2\nSTANDARD\n70\n0\n40\n0\n41\n1\n50\n0\n71\n0\n42\n1\n3\ntxt\n4\n\n0\nENDTAB\n';
		s += '0\nENDSEC\n0\nSECTION\n2\nBLOCKS\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n';
		return s + this.ents.join('') + '0\nENDSEC\n0\nEOF\n';
	};

	Dxf.prototype.toBytes = function () {
		var str = this.toString().replace(/\n/g, '\r\n');
		return encode1250(str);
	};

	if (typeof module !== 'undefined' && module.exports) module.exports = Dxf;
	else root.Dxf = Dxf;
})(this);
