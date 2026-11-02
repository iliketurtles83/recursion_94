#ifndef CT_TUNNELDETAIL_H
#define CT_TUNNELDETAIL_H
#include "core/chunks.h"
#include "core/path.h"
// Append solid wall hardware. All parts stay outside the flight lane.
void tunnel_detail_build(LineGeo *g,const PathSystem *path,DVec3 origin,double s0);
#endif
