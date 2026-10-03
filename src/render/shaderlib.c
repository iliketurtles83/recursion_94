#include "shaderlib.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static long long file_mtime(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (long long)st.st_mtime * 1000000000ll + (long long)st.st_mtim.tv_nsec;
}

static void path_join(char *out, size_t n, const char *dir, const char *name) { snprintf(out, n, "%s/%s", dir, name); }

// Reads a shader file, expanding #include "x" lines (one level deep is enough here,
// but recursion is supported up to a small depth). Updates *mtime with the max of all files.
static char *load_expanded(const char *dir, const char *name, long long *mtime, int depth)
{
    char path[1024];
    path_join(path, sizeof(path), dir, name);
    long long mt = file_mtime(path);
    if (mt > *mtime) *mtime = mt;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "shaderlib: cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = (char *)malloc((size_t)len + 1);
    size_t rd = fread(src, 1, (size_t)len, f);
    src[rd] = 0;
    fclose(f);
    if (depth > 4) return src;

    size_t cap = (size_t)len * 2 + 1024, outLen = 0;
    char *out = (char *)malloc(cap);
    const char *p = src;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t lineLen = eol ? (size_t)(eol - p + 1) : strlen(p);
        const char *inc = NULL;
        if (strncmp(p, "#include", 8) == 0) inc = strchr(p, '"');
        char *piece = NULL;
        size_t pieceLen = lineLen;
        if (inc && inc < p + lineLen) {
            const char *end = strchr(inc + 1, '"');
            char incName[128] = { 0 };
            if (end && (size_t)(end - inc - 1) < sizeof(incName)) {
                memcpy(incName, inc + 1, (size_t)(end - inc - 1));
                piece = load_expanded(dir, incName, mtime, depth + 1);
                if (piece) pieceLen = strlen(piece);
            }
        }
        const char *srcPiece = piece ? piece : p;
        if (outLen + pieceLen + 2 >= cap) { cap = (outLen + pieceLen + 2) * 2; out = (char *)realloc(out, cap); }
        memcpy(out + outLen, srcPiece, pieceLen);
        outLen += pieceLen;
        if (piece) out[outLen++] = '\n';
        free(piece);
        p += lineLen;
    }
    out[outLen] = 0;
    free(src);
    return out;
}

static bool compile_entry(ShaderLib *lib, ShaderEntry *e)
{
    long long mt = 0;
    char *vs = load_expanded(lib->dir, e->vsName, &mt, 0);
    char *fs = load_expanded(lib->dir, e->fsName, &mt, 0);
    // sl_poll watches common.glsl for every shader; count it here too to avoid a spurious reload.
    char common[1024];
    path_join(common, sizeof(common), lib->dir, "common.glsl");
    long long cm = file_mtime(common);
    e->mtime = cm > mt ? cm : mt;
    if (!vs || !fs) { free(vs); free(fs); return false; }
    Shader sh = LoadShaderFromMemory(vs, fs);
    free(vs); free(fs);
    if (sh.id == 0 || sh.id == rlGetShaderIdDefault()) {
        fprintf(stderr, "shaderlib: compile failed for %s / %s (keeping previous)\n", e->vsName, e->fsName);
        return false;
    }
    if (e->ok) UnloadShader(e->shader);
    e->shader = sh;
    e->ok = true;
    e->version++;
    e->nlocs = 0;
    return true;
}

void sl_init(ShaderLib *lib, const char *dir)
{
    memset(lib, 0, sizeof(*lib));
    snprintf(lib->dir, sizeof(lib->dir), "%s", dir);
}

int sl_load(ShaderLib *lib, const char *vsName, const char *fsName)
{
    for (int i = 0; i < lib->count; i++)
        if (strcmp(lib->e[i].vsName, vsName) == 0 && strcmp(lib->e[i].fsName, fsName) == 0) return i;
    if (lib->count >= SL_MAX) return -1;
    ShaderEntry *e = &lib->e[lib->count];
    memset(e, 0, sizeof(*e));
    snprintf(e->vsName, sizeof(e->vsName), "%s", vsName);
    snprintf(e->fsName, sizeof(e->fsName), "%s", fsName);
    if (!compile_entry(lib, e)) {
        e->shader = LoadShaderFromMemory(NULL, NULL);   // default shader as placeholder
        e->ok = false;
    }
    return lib->count++;
}

Shader sl_shader(const ShaderLib *lib, int id) { return lib->e[id].shader; }

int sl_loc(ShaderLib *lib, int id, const char *name)
{
    if (id < 0) return -1;
    ShaderEntry *e = &lib->e[id];
    for (int i = 0; i < e->nlocs; i++)
        if (e->locs[i].name == name || strcmp(e->locs[i].name, name) == 0) return e->locs[i].loc;
    int loc = GetShaderLocation(e->shader, name);
    if (e->nlocs < SL_MAX_LOCS) { e->locs[e->nlocs].name = name; e->locs[e->nlocs].loc = loc; e->nlocs++; }
    return loc;
}

void sl_poll(ShaderLib *lib, double now)
{
    if (now - lib->lastPoll < 0.5) return;
    lib->lastPoll = now;
    for (int i = 0; i < lib->count; i++) {
        ShaderEntry *e = &lib->e[i];
        char path[1024];
        long long mt = 0, m;
        path_join(path, sizeof(path), lib->dir, e->vsName); m = file_mtime(path); if (m > mt) mt = m;
        path_join(path, sizeof(path), lib->dir, e->fsName); m = file_mtime(path); if (m > mt) mt = m;
        path_join(path, sizeof(path), lib->dir, "common.glsl"); m = file_mtime(path); if (m > mt) mt = m;
        if (mt > e->mtime) {
            long long keep = mt;
            if (compile_entry(lib, e)) { lib->reloadCount++; printf("shaderlib: reloaded %s / %s\n", e->vsName, e->fsName); }
            e->mtime = keep;   // don't retry a broken file until it changes again
        }
    }
}

void sl_unload_all(ShaderLib *lib)
{
    for (int i = 0; i < lib->count; i++) if (lib->e[i].ok) UnloadShader(lib->e[i].shader);
    lib->count = 0;
}

void sl_f1(ShaderLib *lib, int id, const char *name, float v) { int l = sl_loc(lib, id, name); if (l >= 0) SetShaderValue(lib->e[id].shader, l, &v, SHADER_UNIFORM_FLOAT); }
void sl_f2(ShaderLib *lib, int id, const char *name, float x, float y) { int l = sl_loc(lib, id, name); float v[2] = { x, y }; if (l >= 0) SetShaderValue(lib->e[id].shader, l, v, SHADER_UNIFORM_VEC2); }
void sl_f3(ShaderLib *lib, int id, const char *name, float x, float y, float z) { int l = sl_loc(lib, id, name); float v[3] = { x, y, z }; if (l >= 0) SetShaderValue(lib->e[id].shader, l, v, SHADER_UNIFORM_VEC3); }
void sl_f4(ShaderLib *lib, int id, const char *name, float x, float y, float z, float w) { int l = sl_loc(lib, id, name); float v[4] = { x, y, z, w }; if (l >= 0) SetShaderValue(lib->e[id].shader, l, v, SHADER_UNIFORM_VEC4); }
void sl_v3(ShaderLib *lib, int id, const char *name, Vector3 v) { sl_f3(lib, id, name, v.x, v.y, v.z); }
void sl_i1(ShaderLib *lib, int id, const char *name, int v) { int l = sl_loc(lib, id, name); if (l >= 0) SetShaderValue(lib->e[id].shader, l, &v, SHADER_UNIFORM_INT); }
void sl_vec4v(ShaderLib *lib, int id, const char *name, const float *data, int count) { int l = sl_loc(lib, id, name); if (l >= 0) SetShaderValueV(lib->e[id].shader, l, data, SHADER_UNIFORM_VEC4, count); }
void sl_tex(ShaderLib *lib, int id, const char *name, Texture2D tex) { int l = sl_loc(lib, id, name); if (l >= 0) SetShaderValueTexture(lib->e[id].shader, l, tex); }
void sl_mat(ShaderLib *lib, int id, const char *name, Matrix m) { int l = sl_loc(lib, id, name); if (l >= 0) SetShaderValueMatrix(lib->e[id].shader, l, m); }
