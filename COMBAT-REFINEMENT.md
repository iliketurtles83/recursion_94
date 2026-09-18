# Combat refinement — 2026-09-18

Addresses the four-depth playtest. No new damage system, network dependency,
runtime asset, or new lighting pass. Normal hits still cost 18 Energy, boosted
hits 20.7, and base boost drain remains 1 Energy/second. Existing upgrades,
pause/quit safeguards, settings and local records remain intact.

## Changes

- Firewalls now have dark crimson shells, red cutting teeth and paired warning
  marks. The shell/blades match the existing 1.46-unit collision radius.
- The old depth multiplier stopped increasing early. It now rises smoothly,
  formation density and regular/special-enemy firing increase, and boss health
  keeps growing to account for accumulated upgrades. No adaptive punishment
  based on how well the player is doing.
- Leviathan attacks more often, summons reinforcements faster, and its height
  sweep commits to the player's height. Depth 2 introduces delayed flank fans;
  depth 3 adds a second sweep row; depth 4 adds staggered flank shots. The
  sweep always leaves a vertical escape route. Boss/sniper shots do not home.
- Prism's real body starts in any of three seeded slots and swaps slots every
  two volleys. Bodies cross visibly over 1.25 seconds, with separate height and
  depth arcs. The real iris carries two pale mechanical eyelids, not a floating
  white diamond. Health, projectile origin and hit detection follow the real
  identity; shield tethers no longer point it out. Crossings finish before the
  next attack's target is committed.
- The HUD retains Energy, Links/special readiness, survival time, score/combo,
  weapon-tier pips, depth/round, boss health/shield pips and an evade meter.
  Removed repeated boss instructions, patrol labels, persistent modifier text,
  long boost warnings and repeated LOCK labels. Modifier arrival banners still
  appear briefly. Controls/settings and the run report retain detailed info.
- `OPEN` used to mean the safe firewall lane. Small inward brackets now mark
  that lane without text. Tips advance on action or after five active seconds;
  all expire by twenty seconds. Pausing does not consume that timer. F1 restarts
  tips; they are suppressed during boss combat.

## Measured balance

Leviathan at ordinary exposed phase, full distance difficulty, before the
prolonged-fight modifier (phase changes further increase pressure):

| Depth | Core health | Volley interval |
| --- | ---: | ---: |
| 1 | 238 | 2.13 s |
| 2 | 321 | 1.72 s |
| 4 | 476 | 1.23 s |
| 8 | 782 | 0.80 s |

Cadence has deliberate safety floors: 0.8 seconds for most patterns, 1.65 for
wall-heavy Core/Mantis, and at least 2.25 after a Prism shuffle. Health/pressure
continue growing beyond those floors. This is a balance starting point, not a
claim that every upgrade build or player skill level has been human-playtested.

## Verification

- Warning-clean C11 builds (`-Wall -Wextra -Wpedantic -Werror` test builds).
- Existing gameplay regressions and 372 behavioral assertions pass, including
  deterministic Prism starting slots, continuous switching with aligned hit
  detection, unchanged health while switching, and depth scaling through 64.
- Three-minute sustained simulations for all five bosses at depths 1, 4, 8:
  45 simulated minutes with all reinforcements left alive. Every boss volley
  matched an empty-pool reference (no silent truncation); all had at least
  0.65 seconds of warning plus projectile flight time. Peak 103/128 shots.
- Production-loop smoke: 502 frames, zero failures; keyboard/controller menus,
  pause/settings, quit cancellation, upgrades, recursion, death and restart.
  Additional tutorial checks cover timeout, early completion, boss suppression,
  frozen-time behavior and F1 restart.
- Environment generator: 2,048 zones, no reported errors.
- Rendered all five bosses, firewalls, Prism before/during/after switching and
  the compact HUD at 1280x720 and 960x600; inspected the resulting screenshots.
- UPX integrity test passes. Standalone Windows EXE: **1,012,736 bytes**,
  **461,824 bytes below 1,474,560**; only 3,584 bytes above the previous release.
- SHA-256: `b2b678c5768be6f220f0a012b07d09e86ba8f360058a06c384687af366ef6725`.

This pass does not change audio synthesis. The historical audio golden-hash
mismatch documented in PLAYABILITY.md remains a separate pre-existing issue.
