#ifndef PALETTE_H
#define PALETTE_H

#include "raylib.h"
#include <stdint.h>

#define PALETTE_COUNT 12

typedef struct {
    const char *name;
    Color primary;       // Signature wireframe, corridor signage, primary HUD
    Color secondary;     // Mechanical trim, conduit pulses, secondary shields
    Color hot;           // High-energy flares, player charge shot, matrix heads
    Color threat;        // Enemy fire, chasers, boss tells (guaranteed high contrast)
    Color shadowBody;    // Dark grey / obsidian base for slabs and towers
    Color skyZenith;     // Deep atmosphere zenith (deep void)
    Color skyHorizon;    // Atmospheric horizon haze / distant twilight
} PaletteProfile;

// Shared deterministic hash for seed-based palette selection.
// Retains exact bit formula for 100% determinism with environment and demo hashes.
static inline uint32_t PaletteHash(uint32_t value) {
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16;
    return value;
}

static inline const PaletteProfile *GetPaletteForSeed(uint32_t seed) {
    static const PaletteProfile s_palettes[PALETTE_COUNT] = {
        // 0: CYBERPUNK 2094 (Electric Cyan / Hot Magenta / Solar Yellow)
        {
            "CYBERPUNK 2094",
            { 0, 235, 255, 255 },     // primary: electric cyan
            { 255, 35, 170, 255 },    // secondary: hot magenta
            { 255, 230, 70, 255 },    // hot: solar yellow
            { 255, 75, 20, 255 },     // threat: searing vermillion
            { 8, 10, 16, 255 },       // shadowBody: deep obsidian navy
            { 2, 3, 10, 255 },        // skyZenith: deep stygian void
            { 10, 24, 46, 255 }       // skyHorizon: midnight cyan-blue
        },
        // 1: EMERALD MATRIX (Phosphor Terminal / Vector Radar)
        {
            "EMERALD MATRIX",
            { 40, 255, 120, 255 },    // primary: radioactive jade
            { 20, 185, 150, 255 },    // secondary: cyber mint
            { 210, 255, 225, 255 },   // hot: incandescent white-green
            { 255, 60, 35, 255 },     // threat: high-contrast laser red
            { 6, 12, 8, 255 },        // shadowBody: pitch dark obsidian
            { 2, 5, 3, 255 },         // skyZenith: terminal abyss
            { 6, 26, 14, 255 }        // skyHorizon: phosphor twilight
        },
        // 2: AMBER CRT TERMINAL (Deus Ex / 1980s Cyberdeck)
        {
            "AMBER CRT TERMINAL",
            { 255, 175, 25, 255 },    // primary: phosphor amber
            { 210, 115, 15, 255 },    // secondary: burned gold
            { 255, 242, 175, 255 },   // hot: white-amber
            { 25, 215, 255, 255 },    // threat: electric cyan
            { 13, 10, 7, 255 },       // shadowBody: dark basalt charcoal
            { 6, 3, 2, 255 },         // skyZenith: warm abyss
            { 28, 14, 6, 255 }        // skyHorizon: smoldering amber haze
        },
        // 3: RED ALERT (Hostile Core / Virtual Boy)
        {
            "RED ALERT",
            { 255, 35, 65, 255 },     // primary: sinister crimson
            { 255, 125, 20, 255 },    // secondary: industrial hazard orange
            { 255, 235, 140, 255 },   // hot: blazing core white-yellow
            { 0, 240, 255, 255 },     // threat: electric cyan
            { 12, 6, 8, 255 },        // shadowBody: cold obsidian basalt
            { 5, 2, 4, 255 },         // skyZenith: stygian void
            { 32, 8, 14, 255 }        // skyHorizon: blood horizon
        },
        // 4: ARCTIC MONOLITH (Cold Titanium / Polar Cyber)
        {
            "ARCTIC MONOLITH",
            { 110, 225, 255, 255 },   // primary: pure ice cyan
            { 65, 115, 255, 255 },    // secondary: deep cobalt blue
            { 245, 255, 255, 255 },   // hot: blinding white
            { 255, 45, 75, 255 },     // threat: vivid flare crimson
            { 8, 10, 15, 255 },       // shadowBody: dark slate titanium
            { 2, 3, 8, 255 },         // skyZenith: deep cosmos
            { 12, 20, 38, 255 }       // skyHorizon: frosted cobalt
        },
        // 5: AMIGA COPPER 1994 (Second Reality Sunset)
        {
            "AMIGA COPPER 1994",
            { 255, 160, 40, 255 },    // primary: copper gold
            { 155, 45, 240, 255 },    // secondary: demoscene violet
            { 255, 235, 85, 255 },    // hot: solar flare
            { 20, 255, 145, 255 },    // threat: radioactive mint
            { 12, 7, 15, 255 },       // shadowBody: dark aubergine charcoal
            { 6, 2, 12, 255 },        // skyZenith: dark violet night
            { 34, 12, 26, 255 }       // skyHorizon: copper dusk
        },
        // 6: TOKYO OUTRUN (Nightdrive / Synthwave Sunset)
        {
            "TOKYO OUTRUN",
            { 185, 60, 255, 255 },    // primary: electric violet
            { 20, 245, 195, 255 },    // secondary: cyber mint
            { 255, 70, 130, 255 },    // hot: neon coral pink
            { 255, 220, 25, 255 },    // threat: high-voltage solar yellow
            { 10, 7, 15, 255 },       // shadowBody: dark violet obsidian
            { 5, 2, 10, 255 },        // skyZenith: deep abyss purple
            { 26, 10, 34, 255 }       // skyHorizon: neon dusk
        },
        // 7: VAPOR ARCHIVE (Dreamcast / Pastel Cyberpunk)
        {
            "VAPOR ARCHIVE",
            { 80, 180, 255, 255 },    // primary: sky blue
            { 255, 95, 215, 255 },    // secondary: orchid rose
            { 140, 255, 245, 255 },   // hot: iridescent cyan
            { 255, 100, 30, 255 },    // threat: searing tangerine
            { 8, 10, 16, 255 },       // shadowBody: dark steel slate
            { 3, 4, 12, 255 },        // skyZenith: twilight navy
            { 16, 18, 36, 255 }       // skyHorizon: pastel aurora
        },
        // 8: TOXIC SPILL (Chemical Hack / Industrial Alien)
        {
            "TOXIC SPILL",
            { 140, 255, 30, 255 },    // primary: acid lime
            { 120, 50, 235, 255 },    // secondary: deep bio-purple
            { 240, 255, 60, 255 },    // hot: radioactive yellow
            { 255, 50, 75, 255 },     // threat: blaze coral
            { 8, 12, 8, 255 },        // shadowBody: dark chemical slag
            { 3, 4, 3, 255 },         // skyZenith: dark toxic abyss
            { 14, 24, 12, 255 }       // skyHorizon: acid twilight
        },
        // 9: SOLAR ECLIPSE (Coronal Flare / Golden Dark)
        {
            "SOLAR ECLIPSE",
            { 255, 195, 50, 255 },    // primary: solar gold
            { 205, 90, 25, 255 },     // secondary: deep corona bronze
            { 255, 250, 210, 255 },   // hot: white-hot corona
            { 15, 230, 255, 255 },    // threat: laser cyan
            { 12, 9, 6, 255 },        // shadowBody: deep obsidian bronze
            { 4, 2, 2, 255 },         // skyZenith: eclipse black
            { 28, 14, 7, 255 }        // skyHorizon: coronal haze
        },
        // 10: CYBER-NOIR MONOLITH (Stark Minimalist / Star Wars Trench)
        {
            "CYBER-NOIR MONOLITH",
            { 220, 230, 245, 255 },   // primary: monolith white-silver
            { 90, 130, 160, 255 },    // secondary: steel cyan
            { 255, 255, 255, 255 },   // hot: pure laser white
            { 255, 25, 45, 255 },     // threat: searing plasma ruby
            { 9, 10, 12, 255 },       // shadowBody: pure matte charcoal
            { 2, 2, 4, 255 },         // skyZenith: deep void
            { 12, 14, 18, 255 }       // skyHorizon: cold industrial fog
        },
        // 11: NEO ARCADE 1994 (Classic Sega / Namco High-Energy)
        {
            "NEO ARCADE 1994",
            { 50, 210, 255, 255 },    // primary: electric sky
            { 255, 60, 90, 255 },     // secondary: hot crimson
            { 180, 255, 50, 255 },    // hot: high-volt lime
            { 255, 145, 0, 255 },     // threat: sunburst amber
            { 8, 9, 14, 255 },        // shadowBody: dark arcade obsidian
            { 3, 3, 10, 255 },        // skyZenith: deep cyber blue
            { 14, 16, 34, 255 }       // skyHorizon: laser twilight
        }
    };
    uint32_t index = (PaletteHash(seed ^ UINT32_C(0x94d31a7b)) % PALETTE_COUNT);
    return &s_palettes[index];
}

#endif // PALETTE_H
