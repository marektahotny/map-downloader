#include "GeoData.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cmath>
int main(int argc,char**argv){ for(int i=1;i<argc;i++){ std::ifstream f(argv[i],std::ios::binary); std::stringstream ss; ss<<f.rdbuf(); Geo::Terrain t; std::string err;
 bool ok=Geo::ParseGeoTiff(ss.str(),Geo::BBox(),t,err); double s=0; for(auto&p:t.points) s+=p.z;
 printf("%-20s ok=%d n=%zu sum=%.3f first=(%.1f,%.1f,%.3f) last z=%.3f %s\n",argv[i],ok,t.points.size(),s, ok?t.points[0].e:0, ok?t.points[0].n:0, ok?t.points[0].z:0, ok?t.points.back().z:0, err.c_str()); } }
