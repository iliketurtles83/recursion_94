// Shader loading with a minimal #include "file" preprocessor and mtime-based
// hot reload. On compile failure the previous shader is kept.
#ifndef CT_SHADERLIB_H
#define CT_SHADERLIB_H

#include <stdbool.h>
#include "raylib.h"

#define SL_MAX       32
#define SL_MAX_LOCS  64

typedef struct ShaderLoc { const char *name; int loc; } ShaderLoc;

typedef struct ShaderEntry {
    char      vsName[64], fsName[64];
    Shader    shader;
    long long mtime;
    int       version;       // bumps on every successful (re)load
    bool      ok;
    ShaderLoc locs[SL_MAX_LOCS];
    int       nlocs;
} ShaderEntry;

typedef struct ShaderLib {
    char        dir[512];
    ShaderEntry e[SL_MAX];
    int         count;
    double      lastPoll;
    int         reloadCount;
} ShaderLib;

void   sl_init(ShaderLib *lib, const char *dir);
int    sl_load(ShaderLib *lib, const char *vsName, const char *fsName);
Shader sl_shader(const ShaderLib *lib, int id);
int    sl_loc(ShaderLib *lib, int id, const char *name);   // cached; name must be a string literal
void   sl_poll(ShaderLib *lib, double now);                 // every 0.5 s
void   sl_unload_all(ShaderLib *lib);

// Convenience setters (no-ops for missing uniforms).
void   sl_f1(ShaderLib *lib, int id, const char *name, float v);
void   sl_f2(ShaderLib *lib, int id, const char *name, float x, float y);
void   sl_f3(ShaderLib *lib, int id, const char *name, float x, float y, float z);
void   sl_f4(ShaderLib *lib, int id, const char *name, float x, float y, float z, float w);
void   sl_v3(ShaderLib *lib, int id, const char *name, Vector3 v);
void   sl_i1(ShaderLib *lib, int id, const char *name, int v);
void   sl_vec4v(ShaderLib *lib, int id, const char *name, const float *data, int count);
void   sl_tex(ShaderLib *lib, int id, const char *name, Texture2D tex);
void   sl_mat(ShaderLib *lib, int id, const char *name, Matrix m);

#endif
