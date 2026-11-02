#ifndef CT_SHIPMODEL_H
#define CT_SHIPMODEL_H
#include "linegeo.h"
void shipmodel_build(LineGeo *hull, LineGeo *lights, float engineIntensity);
void shipmodel_nozzles(LineGeo *geo, float boost);
void shipmodel_flames(LineGeo *geo);
#endif
