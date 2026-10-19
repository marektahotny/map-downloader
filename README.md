# Pilon Map Downloader – doplnok pre Archicad 23

Paleta v Archicade 23 (Windows) na stiahnutie a vloženie mapových a 3D podkladov SR (ÚGKK SR):

- **Katastrálna mapa – parcely C** a **parcely registra E** (INSPIRE WFS) → čiary + čísla parciel vo vlastných vrstvách
- **Ortofoto** ZBGIS (WMS, ~0,25 m/px, voliteľne vysoká kvalita do 16 000 px) → obrázok v správnej polohe
- **Terén DMR 5.0** (služba WCS alebo import súboru XYZ / ASC / TIFF) → mesh v lokálnom počiatku, redukcia bodov (rovnomerná alebo klesajúca hustota od počiatku), export XYZ + `bod_pociatku.txt`, 3D náhľad
- **Lokálny počiatok** (klik do mapy, načítanie/zápis z/do umiestnenia projektu), **výška 0,00 = m n. m.**
- Vkladanie **v S-JTSK** a/alebo **v lokálnom počiatku**, cieľ **pôdorys** alebo **pracovný list**
- Nastavenie vrstiev, pier, typov čiar, výšky textov; editovateľná konfigurácia služieb + **Test služieb**

Súradnice: S-JTSK / Křovák East North (EPSG:5514), prepočet WGS84 ↔ S-JTSK s parametrami JTSK03.

## Inštalácia

1. Stiahnite `PilonMapDownloader.apx` z GitHub Actions (artefakt `PilonMapDownloader-AC23-win64`) alebo z Releases.
2. Skopírujte ho do priečinka doplnkov, napr. `C:\Program Files\GRAPHISOFT\ARCHICAD 23\Add-Ons\`
   alebo ho načítajte cez **Možnosti › Správca doplnkov › Pridať**.
3. V Archicade: menu **Pilon › Map Downloader**.

Nastavenia a log: `%APPDATA%\PilonMapDownloader\` (`settings.json`, `services.json`, `log.txt`).

## Služby

Predvolené adresy služieb sú v `addon/Src/Commands.cpp` (`kDefaultServices`) a dajú sa prepísať v palete
(**⚙ Nastavenie › Služby**). Tlačidlo **Test služieb** vypíše odpoveď každej služby a názvy vrstiev /
typov prvkov / pokrytí z GetCapabilities – podľa nich sa dá konfigurácia upraviť.

> DMR 5.0 ÚGKK SR verejne poskytuje ako veľké balíky na stiahnutie a výrezy cez aplikáciu MAPKA.
> Ak služba terénu nie je dostupná, použite **Zo súboru…** (XYZ, ASC grid alebo nekomprimovaný GeoTIFF).

## Zostavenie

Automaticky cez GitHub Actions (`.github/workflows/build.yml`): stiahne oficiálny
[API DevKit 23.3006](https://github.com/GRAPHISOFT/archicad-api-devkit/releases/tag/23.3006), doinštaluje toolset v141
a zostaví `PilonMapDownloader.apx`.

Lokálne (Windows, Visual Studio 2017 alebo VS 2019/2022 s komponentom „MSVC v141“):

```
python tools\embed_html.py
msbuild addon\PilonMapDownloader.vcxproj /p:Configuration=Release /p:Platform=x64 /p:DevKitDir=C:\cesta\API.Development.Kit.WIN.23.3006
```

Testy platformovo nezávislej časti (Linux/macOS): `tests/run_tests.sh`.

## Zdroje údajov

© ÚGKK SR (CC BY 4.0), geokódovanie © OpenStreetMap (Nominatim). Údaje sú informatívne, bez záruky.
