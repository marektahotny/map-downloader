#include "GeoData.hpp"
#include "Json.hpp"
#include "Terrain.hpp"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <cmath>
using namespace Geo;
int main(){
  // JSON
  JsonValue v; std::string err;
  assert(JsonValue::Parse("{\"a\":[1,2,{\"b\":\"x\\u010d\"}],\"c\":true,\"d\":-1.5e3}", v, &err));
  assert(v["a"].Size()==3 && v["a"].At(2)["b"].AsString()=="x\xc4\x8d" && v["c"].AsBool() && v["d"].AsNumber()==-1500);
  printf("json ok: %s\n", v.Serialize().c_str());
  // GML
  const char* gml = "<wfs:FeatureCollection numberReturned=\"1\"><wfs:member><cp:CadastralParcel gml:id=\"CP.1\"><cp:geometry><gml:MultiSurface><gml:surfaceMember><gml:Polygon><gml:exterior><gml:LinearRing><gml:posList srsDimension=\"2\">-500000 -1200000 -499990 -1200000 -499990 -1199990 -500000 -1199990 -500000 -1200000</gml:posList></gml:LinearRing></gml:exterior></gml:Polygon></gml:surfaceMember></gml:MultiSurface></cp:geometry><cp:label>123/4</cp:label><cp:referencePoint><gml:Point><gml:pos>-499995 -1199995</gml:pos></gml:Point></cp:referencePoint></cp:CadastralParcel></wfs:member></wfs:FeatureCollection>";
  std::vector<Feature> fs; 
  assert(ParseGmlFeatures(gml, "label,text", fs, err));
  assert(fs.size()==1 && fs[0].label=="123/4" && fs[0].rings.size()==1 && fs[0].rings[0].size()==5 && fs[0].hasRefPoint);
  printf("gml ok id=%s e=%f\n", fs[0].id.c_str(), fs[0].rings[0][1].e);
  // swapped & positive
  PointEN p = NormalizeJtsk(1200000, 500000); assert(p.e==-500000 && p.n==-1200000);
  // ArcGrid
  std::string ag = "ncols 4\nnrows 3\nxllcorner -500004\nyllcorner -1200003\ncellsize 1\nNODATA_value -9999\n1 2 3 4\n5 6 7 8\n9 10 11 -9999\n";
  Terrain t; assert(ParseArcGrid(ag, t, err)); assert(t.points.size()==11);
  printf("arcgrid ok first e=%.1f n=%.1f z=%.1f spacing=%.2f\n", t.points[0].e, t.points[0].n, t.points[0].z, t.spacing);
  // TIFF float32 uncompressed 3x2 with geotags
  std::string tif; auto u16=[&](unsigned v){tif.push_back(v&255);tif.push_back(v>>8);}; auto u32=[&](unsigned v){for(int i=0;i<4;i++)tif.push_back((v>>(8*i))&255);};
  tif="II"; u16(42); u32(8);
  int ntags=9; u16(ntags); size_t tagStart=tif.size(); size_t dataStart=8+2+ntags*12+4;
  size_t scaleOff=dataStart, tieOff=scaleOff+24, pixOff=tieOff+48;
  auto tag=[&](unsigned id,unsigned type,unsigned cnt,unsigned val){u16(id);u16(type);u32(cnt);u32(val);};
  tag(256,3,1,3); tag(257,3,1,2); tag(258,3,1,32); tag(259,3,1,1); tag(273,4,1,(unsigned)pixOff); tag(278,3,1,2); tag(339,3,1,3); tag(33550,12,3,(unsigned)scaleOff); tag(33922,12,6,(unsigned)tieOff);
  u32(0);
  auto f64=[&](double d){unsigned char b[8];memcpy(b,&d,8);for(int i=0;i<8;i++)tif.push_back(b[i]);};
  f64(1);f64(1);f64(0); f64(0);f64(0);f64(0);f64(-500000);f64(-1199998);f64(0);
  for(int i=0;i<6;i++){float f=100+i;unsigned char b[4];memcpy(b,&f,4);for(int k=0;k<4;k++)tif.push_back(b[k]);}
  Terrain tt; BBox fb; assert(ParseTerrainAuto(tif, fb, tt, err) || (printf("err %s\n",err.c_str()),0));
  assert(tt.points.size()==6); printf("tiff ok p0 e=%.1f n=%.1f z=%.1f\n", tt.points[0].e, tt.points[0].n, tt.points[0].z);
  // reduction
  Terrain g; g.regularGrid=true; g.spacing=1;
  for(int y=0;y<100;y++)for(int x=0;x<100;x++){PointENZ q; q.e=-500000+x; q.n=-1200000+y; q.z=0; g.points.push_back(q);} FinalizeTerrain(g); g.regularGrid=true; g.spacing=1;
  ReductionParams rp; rp.keepPercent=25; auto k=ReducePoints(g,rp); printf("uniform 25%% -> %zu\n", k.size());
  rp.mode=1; rp.origin=PointEN(-499950,-1199950); rp.nearPercent=100; rp.farPercent=5; k=ReducePoints(g,rp); printf("distance -> %zu\n", k.size());
  HeightIndex hi; hi.Build(g); double z; assert(hi.HeightAt(-499950.3,-1199950.2,z));
  // XYZ positive convention
  Terrain x; assert(ParseXyz("500000.0 1200000.0 150.2\n500001 1200000 151\n",x,err)); assert(x.points[0].e==-500000 && x.points[0].n==-1200000);
  printf("WMS %s\n", WmsGetMapUrl("https://a/b/get","1","1.1.1","EPSG:5514","image/jpeg",BBox{-500100,-1200100,-500000,-1200000},400,400).c_str());
  printf("WFS %s\n", WfsGetFeatureUrl("https://x/ows","cp:CadastralParcel","urn:ogc:def:crs:EPSG::5514",BBox{-500100,-1200100,-500000,-1200000},5000).c_str());
  Ring sq={{0,0},{10,0},{10,10},{0,10},{0,0}}; auto c=Centroid(sq); assert(fabs(c.e-5)<1e-9);
  { // terrain mesh
    TerrainMesh::Input in; in.terrain=&g; in.polygon={{-499990,-1199990},{-499920,-1199990},{-499920,-1199920},{-499990,-1199920},{-499990,-1199990}};
    in.reduction.enabled=true; in.reduction.keepPercent=25; in.h0=0;
    TerrainMesh::Output o; assert(TerrainMesh::Build(in,o));
    printf("mesh boundary=%zu interior=%zu\n", o.boundary.size(), o.interior.size());
    assert(o.boundary.size()>4 && o.interior.size()>500 && o.boundary.front().e==o.boundary.back().e);
  }
  printf("ALL OK\n");
}
