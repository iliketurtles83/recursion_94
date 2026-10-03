// Parameter registry: every tunable is registered once (name, group, default,
// range) and drives both the debug UI and ini save/load. The registry stores
// pointers into config structs owned elsewhere; it owns no tunable state itself.
#ifndef CT_PARAMS_H
#define CT_PARAMS_H

#include <stdbool.h>

typedef enum ParamType { PARAM_FLOAT, PARAM_INT, PARAM_BOOL } ParamType;

typedef struct Param {
    const char *name;        // unique key, used in ini files
    const char *group;       // UI grouping / ini section
    ParamType   type;
    void       *ptr;
    float       def, min, max;
} Param;

#define PARAMS_MAX 256

typedef struct ParamRegistry {
    Param items[PARAMS_MAX];
    int   count;
} ParamRegistry;

void   params_clear(ParamRegistry *reg);
// Registration writes the default into *ptr.
void   params_float(ParamRegistry *reg, const char *group, const char *name, float *ptr, float def, float min, float max);
void   params_int(ParamRegistry *reg, const char *group, const char *name, int *ptr, int def, int min, int max);
void   params_bool(ParamRegistry *reg, const char *group, const char *name, bool *ptr, bool def);
// Remove all params of a group (used when switching environments).
void   params_remove_group_prefix(ParamRegistry *reg, const char *prefix);

Param *params_find(ParamRegistry *reg, const char *name);
float  params_get(const Param *p);
void   params_set(Param *p, float v);   // clamps to range
void   params_reset_defaults(ParamRegistry *reg);

bool   params_save_ini(const ParamRegistry *reg, const char *path);
// Returns number of keys applied, -1 if the file could not be opened. Unknown keys are ignored.
int    params_load_ini(ParamRegistry *reg, const char *path);

#endif
