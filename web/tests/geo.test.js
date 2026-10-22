// node web/tests/geo.test.js
var assert = require('assert');
var path = require('path');
var Geo = require(path.join(__dirname, '../js/geo.js'));
var Dxf = require(path.join(__dirname, '../js/dxf.js'));
var fs = require('fs');
var ok = 0;
function t(name, fn) { fn(); ok++; console.log('ok  ' + name); }

t('normalizeJtsk', function () {
	assert.deepStrictEqual(Geo.normalizeJtsk(1200000, 500000), [-500000, -1200000]);
	assert.deepStrictEqual(Geo.normalizeJtsk(-500000, -1200000), [-500000, -1200000]);
	assert.deepStrictEqual(Geo.normalizeJtsk(-1200000, -500000), [-500000, -1200000]);
});

t('gml INSPIRE CP', function () {
	var gml = '<wfs:FeatureCollection numberReturned="2"><wfs:member><cp:CadastralParcel gml:id="CP.1"><cp:areaValue uom="m2">512</cp:areaValue>' +
		'<cp:geometry><gml:MultiSurface><gml:surfaceMember><gml:Polygon><gml:exterior><gml:LinearRing><gml:posList srsDimension="2">' +
		'-1200000 -500000 -1200000 -499990 -1200010 -499990 -1200010 -500000 -1200000 -500000</gml:posList></gml:LinearRing></gml:exterior></gml:Polygon></gml:surfaceMember></gml:MultiSurface></cp:geometry>' +
		'<cp:label>1234/5</cp:label><cp:referencePoint><gml:Point><gml:pos>-1200005 -499995</gml:pos></gml:Point></cp:referencePoint></cp:CadastralParcel></wfs:member>' +
		'<wfs:member><cp:CadastralParcel gml:id="CP.2"><gml:posList>-499990 -1200000 -499980 -1200000 -499980 -1200010 -499990 -1200000</gml:posList><cp:label>12&amp;3</cp:label></cp:CadastralParcel></wfs:member></wfs:FeatureCollection>';
	var f = Geo.parseGml(gml);
	assert.strictEqual(f.length, 2);
	assert.strictEqual(f[0].id, 'CP.1');
	assert.strictEqual(f[0].label, '1234/5');
	assert.strictEqual(f[0].officialArea, 512);
	assert.deepStrictEqual(f[0].refPoint, [-499995, -1200005]);
	assert.deepStrictEqual(f[0].rings[0][0], [-500000, -1200000]);
	assert.strictEqual(Math.round(Geo.area(f[0].rings[0])), 100);
	assert.strictEqual(f[1].label, '12&3');
	assert.throws(function () { Geo.parseGml('<ows:ExceptionReport><ows:Exception><ows:ExceptionText>zly bbox</ows:ExceptionText></ows:Exception></ows:ExceptionReport>'); }, /zly bbox/);
	assert.deepStrictEqual(Geo.parseGml('<wfs:FeatureCollection numberReturned="0"></wfs:FeatureCollection>'), []);
});

t('outerBoundary', function () {
	var a = [[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]], b = [[10, 0], [20, 0], [20, 10], [10, 10], [10, 0]];
	var o = Geo.outerBoundary([a, b]);
	assert.strictEqual(o.length, 1);
	assert.strictEqual(Math.round(Geo.area(o[0])), 200);
	assert.ok(Geo.ringsTouch(a, b, 0.05));
});

t('clipPath', function () {
	var poly = [[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]];
	var parts = Geo.clipPath([[-5, 5], [15, 5]], poly);
	assert.strictEqual(parts.length, 1);
	assert.deepStrictEqual(parts[0], [[0, 5], [10, 5]]);
	parts = Geo.clipPath([[-5, 2], [5, 2], [5, 20], [8, 20], [8, 5]], poly);
	assert.strictEqual(parts.length, 2);
	assert.strictEqual(Geo.clipPath([[20, 20], [30, 30]], poly).length, 0);
	// L-polygón
	var L = [[0, 0], [10, 0], [10, 4], [4, 4], [4, 10], [0, 10], [0, 0]];
	parts = Geo.clipPath([[-1, 7], [11, 7]], L);
	assert.strictEqual(parts.length, 1);
	assert.ok(Math.abs(Geo.length(parts[0]) - 4) < 1e-9);
});

function gridTerrain(cols, rows, res, fn) {
	var n = cols * rows, E = new Float64Array(n), N = new Float64Array(n), Z = new Float64Array(n), c = 0;
	for (var y = 0; y < rows; y++) for (var x = 0; x < cols; x++) { E[c] = -500000 + x * res; N[c] = -1200000 + y * res; Z[c] = fn(x * res, y * res); c++; }
	return Geo.makeTerrain(E, N, Z, c, 'test');
}

t('terrain grid + reduction', function () {
	var tr = gridTerrain(101, 101, 1, function (x, y) { return 150 + x * 0.1; });
	assert.ok(tr.grid);
	assert.strictEqual(tr.spacing, 1);
	assert.strictEqual(tr.count, 10201);
	var k = Geo.reducePoints(tr, { enabled: true, mode: 0, keepPercent: 25 });
	assert.strictEqual(k.length, 51 * 51);
	var kd = Geo.reducePoints(tr, { enabled: true, mode: 1, nearPercent: 100, farPercent: 6, origin: [-500000, -1200000] });
	assert.ok(kd.length < tr.count && kd.length > 51 * 51 / 4);
	assert.ok(Math.abs(Geo.heightAt(tr, -499950.5, -1199950) - 154.95) < 1e-6);
	var poly = [[-500000, -1200000], [-499950, -1200000], [-499950, -1199950], [-500000, -1199950], [-500000, -1200000]];
	var cl = Geo.clipTerrain(tr, poly);
	assert.strictEqual(cl.count, 51 * 51);
	var xyz = Geo.toXyz(tr, [0], [500000, 1200000, -150]);
	assert.strictEqual(xyz, '0.000 0.000 0.000\r\n');
});

t('contours', function () {
	var tr = gridTerrain(41, 41, 1, function (x, y) { return Math.hypot(x - 20, y - 20); });
	var cs = Geo.contours(tr, 5);
	var levels = {};
	cs.forEach(function (c) { levels[c.level] = (levels[c.level] || 0) + 1; });
	assert.deepStrictEqual(Object.keys(levels).map(Number).sort(function (a, b) { return a - b; }), [5, 10, 15, 20, 25]);
	// kružnica r=10 = jedna uzavretá línia s obvodom ~62.8
	var c10 = cs.filter(function (c) { return c.level === 10; });
	assert.strictEqual(c10.length, 1);
	var L = Geo.length(c10[0].path);
	assert.ok(Math.abs(L - 2 * Math.PI * 10) < 1.0, 'dĺžka ' + L);
	var p = c10[0].path;
	assert.ok(Math.hypot(p[0][0] - p[p.length - 1][0], p[0][1] - p[p.length - 1][1]) < 1e-9);
});

t('arcgrid + xyz parse', function () {
	var asc = 'ncols 3\nnrows 2\nxllcorner -500000\nyllcorner -1200000\ncellsize 1\nNODATA_value -9999\n1 2 3\n4 5 -9999\n';
	var tr = Geo.parseArcGrid(asc, 'a');
	assert.strictEqual(tr.count, 5);
	assert.ok(tr.grid);
	// horný riadok = severnejší
	assert.strictEqual(Geo.heightAt(tr, -499999.5, -1199998.5), 1);
	assert.strictEqual(Geo.heightAt(tr, -499999.5, -1199999.5), 4);
	var x = Geo.parseXyz('500000.5 1200000.5 100\n500001.5 1200000.5 101\n# x\n', 'x');
	assert.strictEqual(x.count, 2);
	assert.deepStrictEqual([x.e[0], x.n[0]], [-500000.5, -1200000.5]);
});

t('dxf R12', function () {
	var d = new Dxf({ offsetE: -500000, offsetN: -1200000 });
	var L = d.layer('BA_PLYN_potrubie nízkotlak', Dxf.rgbToAci([122, 167, 224]), 'DASHED');
	assert.strictEqual(L, 'BA_PLYN_potrubie_nizkotlak');
	d.polyline(L, [[-500000, -1200000], [-499990, -1200000], [-499990, -1199990]], false);
	d.polyline(L, [[-500000, -1200000], [-499990, -1200000], [-499990, -1199990], [-500000, -1200000]], true);
	d.text(L, [-499995, -1199995], 0.5, 'Číslo ľščťžýáíé ∑', 30, 'center');
	d.face(L, [-500000, -1200000, 150], [-499999, -1200000, 151], [-500000, -1199999, 152]);
	d.circle(L, [-500000, -1200000], 0.3);
	d.point(L, [-500000, -1200000], 150);
	var b = d.toBytes();
	var s = Buffer.from(b).toString('latin1');
	assert.ok(s.indexOf('AC1009') > 0);
	assert.ok(s.indexOf('\\U+2211') > 0);
	assert.ok(b.indexOf(0xC8) > 0);	// Č v cp1250
	assert.strictEqual(Dxf.rgbToAci([255, 0, 0]), 1);
	assert.strictEqual(Dxf.rgbToAci([0, 0, 255]), 5);
	fs.writeFileSync(path.join(require('os').tmpdir(), 'pmd_test.dxf'), Buffer.from(b));
});

console.log('ALL OK (' + ok + ')');
