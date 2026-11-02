#include "material_atlas.h"

#include <math.h>
#include <stdio.h>

// Shared integer hash used by both material recipes.
static uint32_t AtlasHash(uint32_t x) {
    x ^= x >> 16; x *= UINT32_C(0x7feb352d);
    x ^= x >> 15; x *= UINT32_C(0x846ca68b);
    return x ^ (x >> 16);
}

static float AtlasRandom01(int x, int y, uint32_t salt) {
    return (float)(AtlasHash((uint32_t)x * UINT32_C(1597334677) ^
                             (uint32_t)y * UINT32_C(3812015801) ^ salt) & 65535u) / 65535.0f;
}

// Periodic smoothstep value noise; period must be a power of two.
static float AtlasNoise(float x, float y, int period, uint32_t salt) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    int m = period - 1;
    float a = AtlasRandom01(ix & m, iy & m, salt);
    float b = AtlasRandom01((ix + 1) & m, iy & m, salt);
    float c = AtlasRandom01(ix & m, (iy + 1) & m, salt);
    float d = AtlasRandom01((ix + 1) & m, (iy + 1) & m, salt);
    return (a + (b - a) * fx) * (1.0f - fy) + (c + (d - c) * fx) * fy;
}

static unsigned char TruncByte(float x) { return (unsigned char)(x < 0 ? 0 : (x > 255 ? 255 : x)); }
static float Clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
static unsigned char RoundByte(float x) { return (unsigned char)(Clamp01(x) * 255.0f + 0.5f); }

static unsigned char detailAtlas[DETAIL_ATLAS_BYTES];
static unsigned char facadeAtlas[FACADE_ATLAS_BYTES];
static bool detailReady = false;
static bool facadeReady = false;

// Brushed/rolled metal with corrosion pits and sparse scratches.
static void GenerateDetailAtlas(unsigned char *out) {
    for (int y = 0; y < DETAIL_ATLAS_SIDE; y++) for (int x = 0; x < DETAIL_ATLAS_SIDE; x++) {
        uint32_t h = AtlasHash((uint32_t)x * 1597334677u ^ (uint32_t)y * 3812015801u ^ 94u);
        float broad = AtlasNoise(x / 32.0f, y / 32.0f, 8, 1051u);
        float corrosion = AtlasNoise(x / 8.0f, y / 8.0f, 32, 293u);
        float rolled = AtlasNoise(x / 64.0f, y / 2.0f, 128, 673u);
        float scratch = 0;
        for (int layer = 0; layer < 3; layer++) {
            int cy = y / (8 + layer * 8), cx = x / 32;
            uint32_t line = AtlasHash((uint32_t)cy * 977u ^ (uint32_t)cx * 577u ^ (uint32_t)layer);
            int ly = y % (8 + layer * 8), target = (int)(line % (uint32_t)(8 + layer * 8));
            if (ly == target && (x % 32) < (int)((line >> 8) & 31u) && (line & 3u) == 0) scratch = 1;
        }
        float pit = corrosion > 0.63f && (h & 31u) < 5u ? 1.0f : 0.0f;
        unsigned char *p = out + ((size_t)y * DETAIL_ATLAS_SIDE + (size_t)x) * 4;
        p[0] = TruncByte(91 + broad * 43 + rolled * 20 + (h & 15u) - scratch * 42 - pit * 35);
        p[1] = TruncByte(83 + corrosion * 60 + ((h >> 9) & 63u) - scratch * 27);
        p[2] = TruncByte(30 + broad * broad * 98 + corrosion * 50 + pit * 40);
        p[3] = TruncByte(24 + ((h >> 19) & 31u) + scratch * 175 + pit * 32);
    }
}

// Four 96x96 side-facade tiles: bolted armor plates, louver cassettes,
// diamond grip with hatches, and perforated machine casing.
static void GenerateFacadeAtlas(unsigned char *out) {
    const int tileSide = FACADE_ATLAS_SIDE / 2;
    for (int y = 0; y < FACADE_ATLAS_SIDE; y++) for (int x = 0; x < FACADE_ATLAS_SIDE; x++) {
        int tile = x / tileSide + 2 * (y / tileSide);
        int u = x % tileSide, v = y % tileSide;
        int pu = u * 128 / tileSide, pv = v * 128 / tileSide;
        int cu = u < tileSide / 2 ? u : tileSide - 1 - u;
        int cv = v < tileSide / 2 ? v : tileSide - 1 - v;
        float coarse = AtlasNoise((float)u / 22.0f, (float)v / 22.0f, 8, 1049u + (uint32_t)tile * 617u);
        float oxide = AtlasNoise((float)u / 7.0f, (float)v / 7.0f, 32, 3119u + (uint32_t)tile * 883u);
        float grain = AtlasRandom01(u, v, 997u + (uint32_t)tile * 1033u);
        float grain2 = AtlasRandom01(u, v, 2503u + (uint32_t)tile * 227u);
        float grain3 = AtlasRandom01(u, v, 7103u + (uint32_t)tile * 437u);
        float grain4 = AtlasRandom01(u, v, 12097u + (uint32_t)tile * 149u);
        float rolled = AtlasNoise((float)u / 30.0f, (float)v / 2.0f, 128, 607u + (uint32_t)tile * 61u);
        float relief = 0.51f + (coarse - 0.5f) * 0.11f;
        float roughness = 0.47f + oxide * 0.13f;
        float stain = 0.12f + oxide * 0.27f + coarse * 0.09f;
        float wear = 0.10f + grain4 * 0.07f;
        float seam = (cu < 3 || cv < 3) ? 1.0f : 0.0f;
        float fastener = 0.0f, slot = 0.0f, rib = 0.0f;

        if (tile == 0) {
            int row = pv / 32;
            int offset = (row & 1) ? 16 : 0;
            int px = (pu + offset) & 63, py = pv & 31;
            int ex = px < 32 ? px : 63 - px;
            int ey = py < 16 ? py : 31 - py;
            seam = (ex < 2 || ey < 2) ? 1.0f : 0.0f;
            rib = (ex >= 2 && ex < 6) || (ey >= 2 && ey < 5) ? 1.0f : 0.0f;
            int dx = ex - 9, dy = ey - 8;
            fastener = dx * dx + dy * dy < 16 ? 1.0f : 0.0f;
            slot = (px > 25 && px < 39 && py > 12 && py < 16) ? 1.0f : 0.0f;
        } else if (tile == 1) {
            int px = pu & 63, py = pv & 63;
            int ex = px < 32 ? px : 63 - px;
            int ey = py < 32 ? py : 63 - py;
            seam = (ex < 3 || ey < 3) ? 1.0f : 0.0f;
            int inside = px > 8 && px < 55 && py > 9 && py < 55;
            slot = inside && ((py + 1) % 8) < 5 ? 1.0f : 0.0f;
            rib = inside && ((py + 1) % 8) >= 5 ? 1.0f : 0.0f;
            int dx = ex - 5, dy = ey - 5;
            fastener = dx * dx + dy * dy < 9 ? 1.0f : 0.0f;
        } else if (tile == 2) {
            int px = pu & 63, py = pv & 63;
            int d1 = (px + py) & 15, d2 = (px - py + 128) & 15;
            rib = (d1 < 2 || d2 < 2) ? 0.65f : 0.0f;
            int hatch = (px > 13 && px < 51 && py > 13 && py < 51);
            seam = (px == 13 || px == 50 || py == 13 || py == 50) ? 1.0f : 0.0f;
            slot = hatch && ((px - 17) % 7 < 3) && py > 21 && py < 43 ? 0.65f : 0.0f;
            fastener = ((px - 9) * (px - 9) + (py - 9) * (py - 9) < 10) ? 1.0f : 0.0f;
        } else {
            int px = pu & 63, py = pv & 63;
            int dx = px - 32, dy = py - 32;
            int r2 = dx * dx + dy * dy;
            int hx = (px + 4) % 12 - 6, hy = (py + 4) % 12 - 6;
            slot = (hx * hx + hy * hy < 8) ? 1.0f : 0.0f;
            rib = (r2 > 600 && r2 < 800) || px < 5 || py < 5 ? 1.0f : 0.0f;
            seam = (px < 2 || py < 2) ? 1.0f : 0.0f;
            fastener = (px - 7) * (px - 7) + (py - 7) * (py - 7) < 9 ? 1.0f : 0.0f;
        }

        uint32_t scratchHash = AtlasHash((uint32_t)(u / 26) * 977u ^
                                         (uint32_t)(v / 9) * 577u ^ (uint32_t)tile * 301u);
        float scratch = (v % 9 == (int)(scratchHash % 9u) &&
                         u % 26 < (int)((scratchHash >> 9) % 22u) &&
                         (scratchHash & 3u) == 0u) ? 1.0f : 0.0f;
        float pit = oxide > 0.64f && grain3 > 0.77f ? 1.0f : 0.0f;
        relief += rib * 0.11f + fastener * 0.19f - seam * 0.22f - slot * 0.24f;
        relief += (grain - 0.5f) * 0.044f + (rolled - 0.5f) * 0.050f - pit * 0.075f;
        roughness += seam * 0.12f + slot * 0.16f + oxide * 0.14f;
        roughness += (grain2 - 0.5f) * 0.21f - fastener * 0.23f;
        stain += seam * 0.15f + (slot > 0.0f ? 0.09f : 0.0f) + pit * 0.27f;
        stain += (grain3 - 0.5f) * 0.13f;
        wear += rib * 0.13f + fastener * 0.50f + scratch * 0.62f + pit * 0.09f;
        wear += (grain4 - 0.5f) * 0.105f;
        unsigned char *p = out + ((size_t)y * FACADE_ATLAS_SIDE + (size_t)x) * 4;
        p[0] = RoundByte(relief);
        p[1] = RoundByte(roughness);
        p[2] = RoundByte(stain);
        p[3] = RoundByte(wear);
    }
}

const unsigned char *GetDetailAtlasBytes(void) {
    if (!detailReady) { GenerateDetailAtlas(detailAtlas); detailReady = true; }
    return detailAtlas;
}

const unsigned char *GetFacadeAtlasBytes(void) {
    if (!facadeReady) { GenerateFacadeAtlas(facadeAtlas); facadeReady = true; }
    return facadeAtlas;
}

uint32_t MaterialAtlasHash(const unsigned char *bytes, int count) {
    uint32_t h = UINT32_C(2166136261);
    for (int i = 0; i < count; i++) h = (h ^ bytes[i]) * UINT32_C(16777619);
    return h;
}

bool ValidateMaterialAtlases(void) {
    uint32_t detail = MaterialAtlasHash(GetDetailAtlasBytes(), DETAIL_ATLAS_BYTES);
    uint32_t facade = MaterialAtlasHash(GetFacadeAtlasBytes(), FACADE_ATLAS_BYTES);
    bool valid = detail == DETAIL_ATLAS_FNV1A && facade == FACADE_ATLAS_FNV1A;
    printf("ATLAS: detail fnv1a=%08x (expected %08x) facade fnv1a=%08x (expected %08x) %s\n",
           (unsigned int)detail, (unsigned int)DETAIL_ATLAS_FNV1A,
           (unsigned int)facade, (unsigned int)FACADE_ATLAS_FNV1A, valid ? "PASS" : "FAIL");
    return valid;
}
