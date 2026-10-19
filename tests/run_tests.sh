#!/bin/sh
# Unit tests of the platform independent code (GeoData, Json, Terrain)
set -e
cd "$(dirname "$0")"
g++ -std=c++14 -O1 -I../addon/Src geo_tests.cpp ../addon/Src/GeoData.cpp ../addon/Src/Json.cpp ../addon/Src/Terrain.cpp -o /tmp/pmd_tests
/tmp/pmd_tests
