#include "params.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void params_clear(ParamRegistry *reg) { reg->count = 0; }

static Param *add(ParamRegistry *reg, const char *group, const char *name, ParamType type, void *ptr, float def, float min, float max)
{
    Param *existing = params_find(reg, name);
    Param *p = existing;
    if (!p) {
        if (reg->count >= PARAMS_MAX) { fprintf(stderr, "params: registry full, dropping %s\n", name); return NULL; }
        p = &reg->items[reg->count++];
    }
    p->name = name; p->group = group; p->type = type; p->ptr = ptr;
    p->def = def; p->min = min; p->max = max;
    params_set(p, def);
    return p;
}

void params_float(ParamRegistry *reg, const char *group, const char *name, float *ptr, float def, float min, float max) { add(reg, group, name, PARAM_FLOAT, ptr, def, min, max); }
void params_int(ParamRegistry *reg, const char *group, const char *name, int *ptr, int def, int min, int max) { add(reg, group, name, PARAM_INT, ptr, (float)def, (float)min, (float)max); }
void params_bool(ParamRegistry *reg, const char *group, const char *name, bool *ptr, bool def) { add(reg, group, name, PARAM_BOOL, ptr, def ? 1.0f : 0.0f, 0.0f, 1.0f); }

void params_remove_group_prefix(ParamRegistry *reg, const char *prefix)
{
    size_t n = strlen(prefix);
    int w = 0;
    for (int i = 0; i < reg->count; i++)
        if (strncmp(reg->items[i].group, prefix, n) != 0) reg->items[w++] = reg->items[i];
    reg->count = w;
}

Param *params_find(ParamRegistry *reg, const char *name)
{
    for (int i = 0; i < reg->count; i++)
        if (strcmp(reg->items[i].name, name) == 0) return &reg->items[i];
    return NULL;
}

float params_get(const Param *p)
{
    switch (p->type) {
    case PARAM_FLOAT: return *(const float *)p->ptr;
    case PARAM_INT:   return (float)*(const int *)p->ptr;
    case PARAM_BOOL:  return *(const bool *)p->ptr ? 1.0f : 0.0f;
    }
    return 0.0f;
}

void params_set(Param *p, float v)
{
    if (v < p->min) v = p->min;
    if (v > p->max) v = p->max;
    switch (p->type) {
    case PARAM_FLOAT: *(float *)p->ptr = v; break;
    case PARAM_INT:   *(int *)p->ptr = (int)(v + (v >= 0 ? 0.5f : -0.5f)); break;
    case PARAM_BOOL:  *(bool *)p->ptr = v >= 0.5f; break;
    }
}

void params_reset_defaults(ParamRegistry *reg)
{
    for (int i = 0; i < reg->count; i++) params_set(&reg->items[i], reg->items[i].def);
}

bool params_save_ini(const ParamRegistry *reg, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    const char *group = NULL;
    for (int i = 0; i < reg->count; i++) {
        const Param *p = &reg->items[i];
        if (!group || strcmp(group, p->group) != 0) {
            fprintf(f, "%s[%s]\n", group ? "\n" : "", p->group);
            group = p->group;
        }
        if (p->type == PARAM_FLOAT) {
            // Shortest representation that reads back to the same float.
            float v = params_get(p);
            char buf[32];
            for (int prec = 6; prec <= 9; prec++) {
                snprintf(buf, sizeof(buf), "%.*g", prec, v);
                if (strtof(buf, NULL) == v) break;
            }
            fprintf(f, "%s=%s\n", p->name, buf);
        }
        else fprintf(f, "%s=%d\n", p->name, (int)params_get(p));
    }
    fclose(f);
    return true;
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

int params_load_ini(ParamRegistry *reg, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512];
    int applied = 0;
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';' || *s == '[') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = trim(s), *val = trim(eq + 1);
        Param *p = params_find(reg, key);
        if (!p) continue;
        params_set(p, strtof(val, NULL));
        applied++;
    }
    fclose(f);
    return applied;
}
