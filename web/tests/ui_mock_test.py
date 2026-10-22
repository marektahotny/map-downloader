"""
Headless test webového nástroja proti simulovaným službám (ÚGKK WFS/WMS, ArcGIS mesta Bratislava).
Spustenie:  python3 web/tests/ui_mock_test.py   (potrebuje playwright, pillow, numpy, pyproj, ezdxf)
"""
import io, json, math, os, re, sys, threading, zipfile, tempfile, functools
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from urllib.parse import urlparse, parse_qs, unquote

import numpy as np
from PIL import Image
from pyproj import Transformer
from playwright.sync_api import sync_playwright

WEB = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
JTSK = ('+proj=krovak +lat_0=49.5 +lon_0=24.83333333333333 +alpha=30.28813972222222 +k=0.9999 +x_0=0 +y_0=0 '
        '+ellps=bessel +towgs84=485.021,169.465,483.839,7.786342,4.397554,4.102655,0 +units=m +no_defs')
to3857 = Transformer.from_crs(JTSK, 'EPSG:3857', always_xy=True)
to5514 = Transformer.from_crs('EPSG:3857', JTSK, always_xy=True)

E0, N0 = -577000.0, -1277000.0           # stred testovacieho územia (Bratislava)
W, H = 200.0, 150.0
AREA = [[E0 - W/2, N0 - H/2], [E0 + W/2, N0 - H/2], [E0 + W/2, N0 + H/2], [E0 - W/2, N0 + H/2], [E0 - W/2, N0 - H/2]]
calls = {'wfs': 0, 'wms': 0, 'query': 0, 'elev': 0, 'dt_rejected': 0, 'paged': 0}

PNG1 = io.BytesIO(); Image.new('RGBA', (1, 1), (0, 0, 0, 0)).save(PNG1, 'PNG'); PNG1 = PNG1.getvalue()


def parcels_gml(bbox, uo=False):
    minE, minN, maxE, maxN = bbox
    out = []
    step = 20.0
    e = math.floor(minE / step) * step
    k = 0
    while e < maxE:
        n = math.floor(minN / step) * step
        while n < maxN:
            k += 1
            ring = [(e, n), (e + step, n), (e + step, n + step), (e, n + step), (e, n)]
            pos = ' '.join('%.2f %.2f' % p for p in ring)
            lbl = ('E-%d' if uo else '%d/%d') % ((int(-e) % 1000, int(-n) % 1000) if not uo else (k,))
            out.append('<wfs:member><cp:CadastralParcel gml:id="CP.%d.%d"><cp:areaValue uom="m2">400</cp:areaValue>'
                       '<cp:geometry><gml:MultiSurface><gml:surfaceMember><gml:Polygon><gml:exterior><gml:LinearRing>'
                       '<gml:posList srsDimension="2">%s</gml:posList></gml:LinearRing></gml:exterior></gml:Polygon></gml:surfaceMember></gml:MultiSurface></cp:geometry>'
                       '<cp:label>%s</cp:label><cp:referencePoint><gml:Point><gml:pos>%.2f %.2f</gml:pos></gml:Point></cp:referencePoint></cp:CadastralParcel></wfs:member>'
                       % (int(-e), int(-n), pos, lbl, e + 10, n + 10))
            n += step
        e += step
    return ('<?xml version="1.0" encoding="UTF-8"?><wfs:FeatureCollection xmlns:wfs="http://www.opengis.net/wfs/2.0" xmlns:gml="http://www.opengis.net/gml/3.2" '
            'xmlns:cp="urn:x-inspire:specification:gmlas:CadastralParcels:3.0" numberReturned="%d">%s</wfs:FeatureCollection>' % (len(out), ''.join(out)))


def buildings_gml(bbox):
    minE, minN, maxE, maxN = bbox
    cE, cN = (minE + maxE) / 2, (minN + maxN) / 2
    ring = [(cE - 8, cN - 6), (cE + 8, cN - 6), (cE + 8, cN + 6), (cE - 8, cN + 6), (cE - 8, cN - 6)]
    pos = ' '.join('%.2f %.2f' % p for p in ring)
    return ('<wfs:FeatureCollection numberReturned="1"><wfs:member><bu-core2d:Building gml:id="BU.1"><gml:posList>%s</gml:posList>'
            '<bu-base:numberOfFloorsAboveGround>3</bu-base:numberOfFloorsAboveGround></bu-core2d:Building></wfs:member></wfs:FeatureCollection>' % pos)


def jpeg(w, h):
    a = np.zeros((h, w, 3), np.uint8)
    a[..., 0] = np.linspace(40, 200, w)[None, :]
    a[..., 1] = np.linspace(80, 160, h)[:, None]
    a[..., 2] = 90
    b = io.BytesIO(); Image.fromarray(a).save(b, 'JPEG', quality=80); return b.getvalue()


def elev_tiff(bbox, w, h):
    minE, minN, maxE, maxN = bbox
    xs = np.linspace(minE, maxE, w, endpoint=False) + (maxE - minE) / w / 2
    ys = np.linspace(maxN, minN, h, endpoint=False) - (maxN - minN) / h / 2
    X, Y = np.meshgrid(xs, ys)
    Z = (140 + 0.05 * (X - E0) + 3 * np.sin((Y - N0) / 20.0)).astype(np.float32)
    Z[0, 0] = -9999
    b = io.BytesIO(); Image.fromarray(Z, mode='F').save(b, 'TIFF'); return b.getvalue()


LAYERS = [
    {'id': 0, 'name': 'Plynovody', 'type': 'Feature Layer', 'geometryType': 'esriGeometryPolyline', 'minScale': 750000,
     'fields': [{'name': 'OBJECTID', 'type': 'esriFieldTypeOID'}, {'name': 'Shape', 'type': 'esriFieldTypeGeometry'},
                {'name': 'Typ', 'type': 'esriFieldTypeInteger', 'domain': {'type': 'codedValue', 'codedValues': [{'code': 6320, 'name': 'neurčený plynovod'}]}},
                {'name': 'Potrubie', 'type': 'esriFieldTypeInteger'}, {'name': 'Shape_Length', 'type': 'esriFieldTypeDouble'}],
     'drawingInfo': {'renderer': {'type': 'uniqueValue', 'field1': 'Typ', 'uniqueValueInfos': [
         {'value': '6340', 'label': 'potrubie strednotlak', 'symbol': {'type': 'esriSLS', 'style': 'esriSLSSolid', 'color': [51, 89, 204, 255], 'width': 1}},
         {'value': '6330', 'label': 'plynovod nízkotlak', 'symbol': {'type': 'esriSLS', 'style': 'esriSLSDash', 'color': [122, 167, 224, 255], 'width': 1}}]}},
     'advancedQueryCapabilities': {'supportsPagination': True}},
    {'id': 1, 'name': 'Prvky plynovodov', 'type': 'Feature Layer', 'geometryType': 'esriGeometryPoint',
     'fields': [{'name': 'OBJECTID', 'type': 'esriFieldTypeOID'}, {'name': 'Typ', 'type': 'esriFieldTypeInteger'}],
     'drawingInfo': {'renderer': {'type': 'uniqueValue', 'field1': 'Typ', 'uniqueValueInfos': [{'value': '360', 'label': 'domový regulátor', 'symbol': {'type': 'esriPMS'}}]}}},
    {'id': 2, 'name': 'Prvky plynovodov (vizuál)', 'type': 'Feature Layer', 'geometryType': 'esriGeometryPoint', 'fields': [], 'drawingInfo': {'renderer': {'type': 'simple'}}},
    {'id': 3, 'name': 'Menovitý priemer - plyn (DN)', 'type': 'Feature Layer', 'geometryType': 'esriGeometryPoint',
     'fields': [{'name': 'OBJECTID', 'type': 'esriFieldTypeOID'}, {'name': 'Popis', 'type': 'esriFieldTypeString'}, {'name': 'DN', 'type': 'esriFieldTypeDouble'}],
     'drawingInfo': {'renderer': {'type': 'simple', 'symbol': {'type': 'esriSMS', 'color': [0, 0, 0, 255]}}}},
    {'id': 4, 'name': 'Výšky plynovodov', 'type': 'Feature Layer', 'geometryType': 'esriGeometryPoint',
     'fields': [{'name': 'OBJECTID', 'type': 'esriFieldTypeOID'}, {'name': 'Vyska', 'type': 'esriFieldTypeDouble'}],
     'drawingInfo': {'renderer': {'type': 'simple', 'symbol': {'type': 'esriSMS', 'color': [0, 0, 0, 255]}}}},
    {'id': 5, 'name': 'Objekty', 'type': 'Feature Layer', 'geometryType': 'esriGeometryPolygon',
     'fields': [{'name': 'OBJECTID', 'type': 'esriFieldTypeOID'}],
     'drawingInfo': {'renderer': {'type': 'simple', 'symbol': {'type': 'esriSFS', 'color': [200, 200, 0, 255]}}}},
]


def ba_features(layer_id):
    feats = []
    if layer_id == 0:
        for i in range(5):
            n = N0 - 60 + i * 30
            feats.append({'attributes': {'OBJECTID': i + 1, 'Typ': [6340, 6330, 6320][i % 3], 'Potrubie': 1 if i == 4 else 2},
                          'geometry': {'paths': [[[E0 - 300, n], [E0, n + 5], [E0 + 300, n]]]}})
    elif layer_id == 1:
        feats = [{'attributes': {'OBJECTID': 1, 'Typ': 360}, 'geometry': {'x': E0 + 3, 'y': N0 + 4}},
                 {'attributes': {'OBJECTID': 2, 'Typ': 360}, 'geometry': {'x': E0 + 500, 'y': N0}}]
    elif layer_id == 3:
        feats = [{'attributes': {'OBJECTID': 1, 'Popis': 'STL DN 100', 'DN': 100}, 'geometry': {'x': E0 - 20, 'y': N0 - 30}},
                 {'attributes': {'OBJECTID': 2, 'Popis': '', 'DN': 150}, 'geometry': {'x': E0 + 20, 'y': N0 - 30}}]
    elif layer_id == 4:
        feats = [{'attributes': {'OBJECTID': 1, 'Vyska': 139.456}, 'geometry': {'x': E0, 'y': N0 + 10}}]
    elif layer_id == 5:
        feats = [{'attributes': {'OBJECTID': 1}, 'geometry': {'rings': [[[E0 + 90, N0], [E0 + 120, N0], [E0 + 120, N0 + 20], [E0 + 90, N0 + 20], [E0 + 90, N0]]]}}]
    return feats


def to_sr(f, sr):
    if sr == 5514:
        return f
    g = json.loads(json.dumps(f['geometry']))
    def c(p):
        x, y = to3857.transform(p[0], p[1]); return [x, y]
    if 'paths' in g: g['paths'] = [[c(p) for p in path] for path in g['paths']]
    if 'rings' in g: g['rings'] = [[c(p) for p in r] for r in g['rings']]
    if 'x' in g: g['x'], g['y'] = c([g['x'], g['y']])
    return {'attributes': f['attributes'], 'geometry': g}


def ba_response(path, qs):
    m = re.search(r'/services/(.+?)/MapServer(/.*)?$', path)
    if not m:
        if 'ImageServer/exportImage' in path:
            calls['elev'] += 1
            bb = [float(v) for v in qs['bbox'][0].split(',')]
            w, h = [int(v) for v in qs['size'][0].split(',')]
            return 200, 'image/tiff', elev_tiff(bb, w, h)
        if 'ImageServer' in path:
            return 200, 'application/json', json.dumps({'currentVersion': 10.81, 'pixelType': 'F32'}).encode()
        return 404, 'text/plain', b'nie'
    svc, rest = m.group(1), m.group(2) or ''
    if rest == '':
        return 200, 'application/json', json.dumps({
            'currentVersion': 10.81, 'spatialReference': {'wkid': 102100, 'latestWkid': 3857}, 'maxRecordCount': 2,
            'copyrightText': 'Digitálna technická mapa (©) Hlavné mesto SR Bratislava',
            'datumTransformations': [
                {'geoTransforms': [{'wkid': 8364, 'name': 'S-JTSK_[JTSK03]_To_S-JTSK_1', 'transformForward': False}, {'wkid': 8368, 'name': 'S-JTSK_[JTSK03]_To_WGS_1984_1', 'transformForward': True}]},
                {'geoTransforms': [{'wkid': 8368, 'name': 'S-JTSK_[JTSK03]_To_WGS_1984_1', 'transformForward': False}, {'wkid': 8364, 'name': 'S-JTSK_[JTSK03]_To_S-JTSK_1', 'transformForward': True}]}],
            'layers': [{'id': l['id'], 'name': l['name']} for l in LAYERS]}).encode()
    if rest == '/layers':
        return 200, 'application/json', json.dumps({'layers': LAYERS}).encode()
    mq = re.match(r'/(\d+)/query$', rest)
    if mq:
        calls['query'] += 1
        lid = int(mq.group(1))
        out_sr = int(qs.get('outSR', ['3857'])[0])
        if 'datumTransformation' in qs:
            if 'Kanaliz' in svc:      # simulácia servera, ktorý transformáciu odmietne
                calls['dt_rejected'] += 1
                return 200, 'application/json', json.dumps({'error': {'code': 400, 'message': 'Invalid datumTransformation', 'details': []}}).encode()
            dt = json.loads(qs['datumTransformation'][0])
            assert dt['geoTransforms'][0]['wkid'] == 8368 and dt['geoTransforms'][0]['transformForward'] is False, dt
        elif out_sr == 5514:
            raise AssertionError('outSR 5514 bez transformácie')
        assert qs.get('inSR', [''])[0] == '3857', qs.get('inSR')
        feats = [to_sr(f, out_sr) for f in ba_features(lid)]
        off = int(qs.get('resultOffset', ['0'])[0]); cnt = int(qs.get('resultRecordCount', ['1000'])[0])
        page = feats[off:off + cnt]
        if off: calls['paged'] += 1
        return 200, 'application/json', json.dumps({'objectIdFieldName': 'OBJECTID', 'features': page, 'exceededTransferLimit': off + cnt < len(feats)}).encode()
    if '/export' in rest:
        return 200, 'image/png', PNG1
    return 404, 'text/plain', b'?'


def proxy_target(url):
    u = urlparse(url)
    qs = {k: v for k, v in parse_qs(u.query, keep_blank_values=True).items()}
    qsl = {k.upper(): v[0] for k, v in qs.items()}
    if u.hostname == 'geoportal.bratislava.sk':
        return ba_response(unquote(u.path), qs)
    if u.hostname == 'inspirews.skgeodesy.sk' or (u.hostname == 'zbgisws.skgeodesy.sk' and 'buildings' in u.path):
        req = qsl.get('REQUEST', '')
        if req == 'GetCapabilities':
            name = 'bu-core2d:Building' if 'buildings' in u.path else 'cp:CadastralParcel'
            return 200, 'text/xml', ('<wfs:WFS_Capabilities><FeatureTypeList><FeatureType><Name>%s</Name></FeatureType><FeatureType><Name>bu-core2d:BuildingPart</Name></FeatureType></FeatureTypeList></wfs:WFS_Capabilities>' % name).encode()
        calls['wfs'] += 1
        bb = [float(v) for v in qsl['BBOX'].split(',')[:4]]
        if 'buildings' in u.path:
            return 200, 'text/xml', buildings_gml(bb).encode()
        return 200, 'text/xml; charset=utf-8', parcels_gml(bb, uo='cp_uo' in u.path).encode('utf-8')
    if u.hostname == 'zbgisws.skgeodesy.sk':
        calls['wms'] += 1
        if qsl.get('REQUEST') == 'GetCapabilities':
            return 200, 'text/xml', b'<WMT_MS_Capabilities/>'
        return 200, 'image/jpeg', jpeg(int(qsl['WIDTH']), int(qsl['HEIGHT']))
    return 403, 'text/plain', b'Nepovolena adresa'


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=WEB, **k)

    def log_message(self, *a):
        pass

    def do_GET(self):
        u = urlparse(self.path)
        if u.path == '/proxy.php':
            target = parse_qs(u.query)['url'][0]
            try:
                code, ct, body = proxy_target(target)
            except AssertionError as e:
                code, ct, body = 500, 'text/plain', ('ASSERT ' + str(e)).encode()
                print('MOCK ASSERT', e, target[:200])
            self.send_response(code)
            self.send_header('Content-Type', ct)
            self.send_header('X-PMD-Proxy', '1')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        return super().do_GET()


def main():
    srv = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    port = srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    errors = []
    out_dir = tempfile.mkdtemp()
    with sync_playwright() as p:
        b = p.chromium.launch()
        ctx = b.new_context(accept_downloads=True, viewport={'width': 1400, 'height': 900})
        page = ctx.new_page()
        page.on('console', lambda m: m.type == 'error' and errors.append(m.text))
        page.on('pageerror', lambda e: errors.append(str(e)))
        # externé dlaždice (OSM, WMS) a geokóder
        page.route(re.compile(r'^https://'), lambda r: r.fulfill(status=200, content_type='image/png', body=PNG1))
        page.goto('http://127.0.0.1:%d/index.html' % port)
        page.wait_for_function('window.PMD && window.PMD.map')
        page.evaluate('(a) => PMD.finishArea(a)', AREA)
        assert '3,00 ha' in page.inner_text('#areaBadge'), page.inner_text('#areaBadge')

        # výber parcely kliknutím
        page.click('#toolPlot')
        ll = page.evaluate('([e,n]) => { var l = PMD.toLatLng([e,n]); return [l.lat, l.lng]; }', [E0 + 5, N0 + 5])
        page.evaluate('(ll) => PMD.map.fire("click", {latlng: L.latLng(ll[0], ll[1])})', ll)
        page.wait_for_function('PMD.state.plot.length === 1', timeout=10000)
        ll2 = page.evaluate('([e,n]) => { var l = PMD.toLatLng([e,n]); return [l.lat, l.lng]; }', [E0 + 25, N0 + 5])
        page.evaluate('(ll) => PMD.map.fire("click", {latlng: L.latLng(ll[0], ll[1])})', ll2)
        page.wait_for_function('PMD.state.plot.length === 2', timeout=10000)
        page.keyboard.press('Escape')

        # voľby
        for cid in ['itCadE', 'itBuildings', 'itOrtho', 'th_stavby', 'optArea']:
            page.check('#' + cid)
        # vrstvy jednej témy
        page.click('#theme_plyn .head button')
        page.wait_for_selector('#lyr_plyn input[data-id="0"]', timeout=10000)
        assert page.locator('#lyr_plyn input').count() == 5, page.inner_html('#lyr_plyn')   # (vizuál) vynechaný

        # terén z výškového modelu mesta
        page.click('#btnLoadTerrain')
        page.wait_for_function("document.getElementById('terrainBadge').textContent.indexOf('bodov') > 0", timeout=30000)
        print('terén:', page.inner_text('#terrainBadge'), '|', page.inner_text('#terrainInfo'), '| h0 =', page.input_value('#h0'))
        page.click('#btnPreview3d')
        page.wait_for_timeout(300)

        with page.expect_download(timeout=120000) as dl:
            page.click('#btnExport')
        d = dl.value
        zpath = os.path.join(out_dir, d.suggested_filename)
        d.save_as(zpath)
        page.wait_for_timeout(500)
        status = page.inner_text('#status')
        log = page.inner_text('#log')
        print('STATUS:', status)
        print('LOG:\n' + log)
        page.evaluate('PMD.map.fitBounds(L.polygon(PMD.state.area.map(PMD.toLatLng)).getBounds())')
        page.evaluate("document.getElementById('scroll').scrollTop = 0")
        page.wait_for_timeout(300)
        page.screenshot(path=os.path.join(out_dir, 'ui.png'), full_page=False)

        # terén zo súboru ASC (DMR) – iný zdroj
        asc = os.path.join(out_dir, 'dmr.asc')
        with open(asc, 'w') as f:
            f.write('ncols 60\nnrows 50\nxllcorner %.1f\nyllcorner %.1f\ncellsize 5\nNODATA_value -9999\n' % (E0 - 150, N0 - 125))
            for r in range(50):
                f.write(' '.join('%.2f' % (150 + 0.1 * c + 0.05 * r) for c in range(60)) + '\n')
        page.set_input_files('#terFile', asc)
        page.click('#btnLoadTerrain')
        page.wait_for_function("document.getElementById('terrainInfo').textContent.indexOf('dmr.asc') >= 0", timeout=20000)
        print('ASC terén:', page.inner_text('#terrainBadge'), '|', page.inner_text('#terrainInfo'))

        page.set_viewport_size({'width': 390, 'height': 844})
        page.wait_for_timeout(300)
        page.screenshot(path=os.path.join(out_dir, 'ui_mobile.png'), full_page=False)
        b.close()

    print('calls', calls)
    z = zipfile.ZipFile(zpath)
    names = z.namelist()
    print('ZIP:', names)
    import ezdxf
    for n in names:
        if n.endswith('.dxf'):
            fn = os.path.join(out_dir, n); open(fn, 'wb').write(z.read(n))
            doc = ezdxf.readfile(fn)
            aud = doc.audit()
            ms = doc.modelspace()
            per = {}
            for e in ms:
                per.setdefault(e.dxf.layer, {}).setdefault(e.dxftype(), 0)
                per[e.dxf.layer][e.dxftype()] += 1
            print('DXF', n, 'audit errors', len(aud.errors))
            for k in sorted(per): print('   ', k, per[k])
            assert len(aud.errors) == 0
    xyz = z.read('teren.xyz').decode().strip().splitlines()
    print('XYZ', len(xyz), xyz[0])
    assert 'situacia.dxf' in names and 'teren.xyz' in names and 'ortofoto.jpg' in names and 'ortofoto.jgw' in names
    assert 'report_pozemok.txt' in names and 'teren_3D.dxf' in names
    assert calls['dt_rejected'] >= 1 and calls['paged'] >= 1
    sit = ezdxf.readfile(os.path.join(out_dir, 'situacia.dxf'))
    lnames = {l.dxf.name for l in sit.layers}
    for need in ['KN_parcely_C', 'KN_cisla_C', 'KN_parcely_E', 'KN_POZEMOK', 'ZBGIS_budovy', 'BA_PLYN_potrubie_strednotlak', 'BA_PLYN_plynovod_nizkotlak_PRIP',
                 'BA_PLYN_P_domovy_regulator', 'BA_PLYN_T_menovity_priemer_plyn_dn', 'BA_KANAL_potrubie_strednotlak', 'TEREN_vrstevnice', 'POCIATOK']:
        assert any(n.startswith(need[:31]) for n in lnames), (need, sorted(lnames))
    # súradnice lokálne – územie okolo 0,0
    ext = [e for e in sit.modelspace() if e.dxftype() == 'POLYLINE' and e.dxf.layer == 'KN_POZEMOK'][0]
    pts = [v.dxf.location for v in ext.vertices]
    print('pozemok lokálne:', [(round(p.x, 2), round(p.y, 2)) for p in pts])
    assert all(abs(p.x) < 60 and abs(p.y) < 60 for p in pts)
    # siete z režimu "server" (Plyn) a "lokálne proj4" (Kanalizácia) musia ležať na rovnakom mieste
    def first_line(layer_prefix):
        for e in sit.modelspace():
            if e.dxftype() == 'POLYLINE' and e.dxf.layer.startswith(layer_prefix):
                return [tuple(v.dxf.location)[:2] for v in e.vertices]
    a, c = first_line('BA_PLYN_potrubie_strednotlak'), first_line('BA_KANAL_potrubie_strednotlak')
    print('plyn', a[:2], 'kanal', c[:2])
    assert abs(a[0][0] - c[0][0]) < 0.01 and abs(a[0][1] - c[0][1]) < 0.01
    texts = [e.dxf.text for e in sit.modelspace() if e.dxftype() == 'TEXT']
    assert 'STL DN 100' in texts and 'DN 150' in texts and '139,46' in texts, texts[:40]
    rep = z.read('report_pozemok.txt').decode('utf-8-sig')
    print(rep)
    assert 'Spolu: 2 parciel' in rep and 'Susedné parcely' in rep
    if errors:
        print('CONSOLE ERRORS:', errors)
        sys.exit(1)
    print('UI TEST OK ->', out_dir)


if __name__ == '__main__':
    main()
