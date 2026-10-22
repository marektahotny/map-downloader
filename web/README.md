# Pilon Map Downloader – webový nástroj (pre Archicad 23)

Samostatný nástroj, ktorý v mape vyberie územie a stiahne podklady ako súbory na ručný import do Archicadu 23
(alebo do iného CAD programu). Nepotrebuje doplnok Archicadu ani Graphisoft MDID.

## Čo stiahne

| Podklad | Zdroj | Výstup |
|---|---|---|
| Katastrálna mapa – parcely C (hranice + čísla) | ÚGKK SR, INSPIRE WFS | `situacia.dxf` – hladiny `KN_parcely_C`, `KN_cisla_C` |
| Parcely registra E | ÚGKK SR, INSPIRE WFS | `KN_parcely_E`, `KN_cisla_E` |
| Hranica stavebného pozemku (zlúčené parcely) + report | ÚGKK SR | `KN_POZEMOK`, `report_pozemok.txt` + `.xml` (výmery, susedné parcely) |
| Budovy ZBGIS | ÚGKK SR | `ZBGIS_budovy` |
| Ortofoto | ÚGKK SR, WMS | `ortofoto.jpg` + `ortofoto.jgw` + `ortofoto_info.txt` |
| **Inžinierske siete Bratislavy** – vodovod, kanalizácia, plyn, teplovod, elektrina, telekomunikácie, kolektory, produktovody | Hlavné mesto SR Bratislava (ArcGIS) | hladiny `BA_<SIEŤ>_<typ vedenia>`, prípojky `…_PRIP`, prvky `BA_<SIEŤ>_P_…`, popisy/DN/výšky `BA_<SIEŤ>_T_…` |
| Polohopis technickej mapy Bratislavy – stavby, doprava, zeleň, výškopis | Hlavné mesto SR Bratislava | hladiny `BA_STAVBY_…`, `BA_DOPRAVA_…` … |
| Terén – výškový model Bratislavy (1 m) alebo vlastný súbor DMR 5.0 (GeoTIFF / ASC / XYZ) | mesto / súbor | `teren.xyz` (s redukciou bodov), `teren_3D.dxf` (3DFACE), vrstevnice v `situacia.dxf` |
| Bod počiatku, návod, licencie | – | `bod_pociatku.txt`, `NAVOD_import_Archicad23.txt`, `zdroje_a_licencie.txt` |

DXF je verzia R12 (AC1009, kódová stránka 1250), jednotky metre. Súradnice buď **lokálne** (počiatok 0,0 v zvolenom bode,
výšky relatívne k ±0,000), alebo **S-JTSK** (EPSG:5514), alebo obe sady naraz.

## Spustenie

Mapové servery nedovoľujú prehliadaču sťahovať údaje priamo (CORS), preto nástroj potrebuje malú proxy. Sú dve možnosti:

**A) Na vlastnom PC (bez inštalácie)** – rozbaľte ZIP a dvakrát kliknite na `start.cmd`. Otvorí sa okno PowerShellu
(nechajte ho otvorené) a prehliadač s adresou `http://localhost:8765/`.

**B) Na webhostingu s PHP** (napr. WebSupport) – nahrajte obsah priečinka cez FileZilla do podpriečinka webu,
napr. `/mapy/`. Proxy `proxy.php` púšťa len adresy ÚGKK SR a mesta Bratislava. Ak nechcete, aby ju používal ktokoľvek,
vyplňte v `proxy.php` premennú `$ACCESS_KEY` a rovnaký kľúč zadajte v nástroji (⚙ › Služby). Hosting musí mať PHP cURL
a povolené odchádzajúce HTTPS spojenia (bezplatné hostingy ako InfinityFree to často blokujú).

## Postup

1. Nájdite miesto (adresa alebo súradnice), nástrojom **▭ Obdĺžnik** alebo **⬡ Polygón** vyberte územie.
2. Voliteľne **▣ Pozemok** – klikaním vyberte parcely stavebného pozemku.
3. Zaškrtnite podklady. Pri sieťach Bratislavy cez „vrstvy ▾“ vyberiete jednotlivé vrstvy služby.
4. **Terén**: „Načítať terén“, nastavte redukciu bodov, vrstevnice a 3D sieť; „z terénu“ doplní výšku ±0,000 v bode počiatku.
5. **Stiahnuť podklady (ZIP)**. Import do Archicadu popisuje súbor `NAVOD_import_Archicad23.txt` v balíku.

## Dôležité o údajoch

- Kataster a ZBGIS: © ÚGKK SR, CC BY 4.0, informatívne (nenahrádzajú výpis z KN ani geometrický plán).
- Technická mapa Bratislavy: © Hlavné mesto SR Bratislava. Verejné GIS údaje sa aktualizujú približne raz ročne a sú
  **len informatívne – neslúžia na právne účely**. Existenciu a polohu sietí záväzne potvrdzujú ich správcovia.
  Aktuálne CAD údaje (DXF/DWG/DGN, denne aktualizované) mesto predáva podľa cenníka – dtmb@bratislava.sk.
  Na profesionálne použitie si podmienky overte u mesta.
- Siete mesta sú publikované vo Web Mercator; nástroj žiada server o S-JTSK s transformáciou, ktorú služba uvádza
  (S-JTSK → JTSK03 → WGS84). Ak ju server odmietne, prepočet sa urobí lokálne (presnosť približne do 1 m) –
  použitý spôsob je uvedený v `zdroje_a_licencie.txt`.

## Vývoj

- `js/geo.js` – geometria, GML, terén, redukcia, vrstevnice · `js/dxf.js` – zápis DXF R12 · `js/sources.js` – služby ·
  `js/export.js` – zostavenie balíka · `js/app.js` – rozhranie
- Testy: `node web/tests/geo.test.js`, `python3 web/tests/ui_mock_test.py` (prehliadač proti simulovaným službám)
- Knižnice v `vendor/`: Leaflet 1.9.4, proj4 2.12.1, JSZip 3.10.1, geotiff.js 2.1.3, Delaunator 5.0.1
