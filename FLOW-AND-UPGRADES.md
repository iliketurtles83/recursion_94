# Boss flow, music and useful salvage — 2026-09-18

## What changed

- A 2.2-second boss clearance replaces the instantaneous enemy wipe. Ships keep
  their identities and current positions, stop firing and peel outward/upward
  into the distance. Their final distant silhouette shrinks smoothly away.
  Hostile projectiles visibly dissipate within .65 seconds without dealing
  damage or awarding grazes. Player shots can finish travelling. The boss only
  spawns after departing ships free the fixed pool; no heap allocations or
  overwritten live entities. Weapon-chain timers survive this forced handoff.
- Music continues through salvage selection, with a calmer arrangement target.
  Flight, enemies, camera, world, survival timer and recursion collapse remain
  frozen. Pause/settings/quit screens retain their existing audio pause behavior;
  returning to salvage resumes music while simulation remains frozen.
- Link Harvester is a thirteenth upgrade: +1 Link per ordinary kill per level,
  to +3. Works with pulse/charge kills and respects the 100-Link cap. Beam and
  bomb kills remain inside the no-refund guard.
- Piercing Pulse now also adds +25% pulse damage per level, up to +75%, on
  ordinary enemies, boss nodes and exposed cores. Focused Charge and Twin
  Pulse already applied their increased damage to bosses; the cards now say so.
  No armor-piercing immunity bypass: the core is still shielded until its
  shield nodes are destroyed.
- Seeded choices prefer a resource option, offense, and an open option. Capped
  categories fall back to other uncapped choices; all offers remain distinct.
  Cards include actual before/after costs, damage, income or cooldown values.

Max Link Harvester + Link Capacitor: 4 Links/basic kill, 35-Link beam and
70-Link bomb. From zero, that's 9 or 18 basic kills versus 50 or 100 without
either upgrade. Other sources of Links can reduce that further.

## When does it get genuinely difficult?

Each depth is five bosses. There is no hardcoded "impossible after depth X"
switch, and no automated test can determine a human player's skill ceiling.
For a player comfortable with depth 4, depths 4-6 should demand better movement
and upgrade choices; depth 8+ is the expected endurance range. This is an
estimate, not a human-playtested guarantee, especially with the new economy.

The previous combat pass's difficulty formulas are unchanged by this update:

| Depth | Bosses cleared to enter depth | Leviathan health | Exposed volley interval |
| --- | ---: | ---: | ---: |
| 1 | 0 | 238 | 2.13 s |
| 2 | 5 | 321 | 1.72 s |
| 4 | 15 | 476 | 1.23 s |
| 8 | 35 | 782 | 0.80 s |
| 16 | 75 | 1,424 | 0.80 s |

Intervals assume full distance difficulty, ordinary exposed phase and less
than 65 seconds in that fight. Enraged/singularity phases and prolonged fights
increase pressure sooner. Depth 2 adds delayed crossfire, depth 3 adds another
height-sweep row, depth 4 adds staggered flank shots. Modifiers rotate instead
of accumulating indefinitely. Reinforcement pressure and enemy durability
also increase.

Important limitation: fire rates do not speed up forever. Most patterns bottom
out at .8s, wall-heavy bosses at 1.65s; Prism retains a 2.25s post-shuffle window.
The later ramp increasingly comes from growing health, denser encounters and
longer exposure to hazards. The 13 upgrade tracks cap at level 3: 39 upgrade
picks can max every track (depth 8, after boss 4, if choosing upgrades rather
than fallback caches). Enemy pools and cadence
floors prevent unbounded screen saturation. It is possible for a skilled
player to survive beyond depth 8; this is intentional, but deep-run playtesting
is still necessary to distinguish satisfying challenge from pure attrition.

## Verification

- Warning-clean Windows builds, with test builds using `-Werror`.
- Existing gameplay checks plus **549 behavioral assertions** pass.
- Real projectile flight against exposed cores: basic pulse 1 damage; Piercing
  3 pulse 1.75; Piercing 3 + Twin 3 both bolts 3.5; Focused Charge 3 shot 5.
  Shielded cores remain immune. Tested harvesting tiers, caps and no special
  refunds, and seeded resource/offense offers with capped-category fallback.
- Full 48-enemy arena departure: identities survive the first frame, ships
  visibly move away, no new fire/spawns/rewards, incoming shots dissipate,
  the complete five-part boss spawns, and the weapon chain is preserved.
- Previous 45 simulated minutes of sustained boss fights rechecked: no
  truncated volleys and retained warning windows. Peak 103/128 projectiles.
- 502-frame production-loop smoke: zero failures. Includes live salvage-music
  state, nested settings/quit pause and return-to-salvage resume, frozen survival,
  upgrades, recursion, death and restart. Uses isolated preference storage.
- 2,048-zone environment validation passes. Rendered and inspected departure
  stages and 960x600 salvage cards, including resource and boss-damage previews.
- Music/effects volume tests pass. Unchanged pre-existing audio golden mismatch
  remains `a2b83674276419d1`; audio synthesis code itself was not modified.
- UPX integrity passes. EXE **1,015,296 bytes**, below the 1,474,560-byte limit
  by **459,264 bytes**. Increase from the prior release: **2,560 bytes**.
- SHA-256: `0cf280dd0e2043bfdb96be7484acb10060dc45f3c170f26aa2aecb262ad94d3b`.
