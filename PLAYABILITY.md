# RECURSION_94 playability expansion

## Play

Launch `bin/recursion94.exe`, choose a seed, and press Enter / A. Everything
needed to play is in the executable. Your first flight provides short,
action-driven tips; each hint times out after 5 active seconds and the tutorial
ends within 20 seconds. F1 restarts or hides it. Tips stay hidden in boss fights.
Five bosses lead to the black
hole. Beat the Leviathan, choose salvage, and emerge into a harder universe.
The campaign remains endless.

| Action | Keyboard / mouse | Controller |
| --- | --- | --- |
| Move | WASD or arrows | Left stick / D-pad |
| Pulse / charge | Tap / hold and release J or left mouse | A or RT |
| Beam | K | X |
| Bomb | L | Y |
| Boost | Space | RB |
| Evade | Left Shift, with a movement direction | LB + stick |
| Pause | P or on-screen pause button | Start |
| Settings | F2 or pause menu | Pause, navigate to Settings, A |
| Salvage choice | 1 / 2 / 3, mouse, or arrows + Enter | Stick / D-pad + A |
| Restart after death | R / Enter | A |
| Quit confirmation / back | Escape | B |

Keyboard actions can be rebound in settings. Arrow keys remain menu/navigation
fallbacks. Enter, Escape, F, F1/F2/F3, R, Y/N, and 1/2/3 are reserved for UI
actions. Assigning an already-used key swaps the two bindings. Controller
bindings are fixed. Left-stick deadzone is 18%. Menus work with stick or D-pad.
Losing window focus pauses an active run.

## Upgrades

Every defeated boss offers three deterministic choices. Levels are permanent
within the current run and reset on death/restart. All thirteen cap at level 3;
once capped they leave the offer pool. Card 1 prefers resource income/cost
upgrades, card 2 offense, card 3 any available upgrade. Capped categories fall
back to distinct uncapped options. Cards show current-to-next values.
The choice screen freezes flight and survival time, but keeps music playing.
Actual pause, settings and quit-confirmation screens still pause music.

| Upgrade | Effect per level / tradeoff |
| --- | --- |
| Piercing pulse | +25% pulse damage, including bosses, and +1 additional target; no repeated hits on one target |
| Focused charge | +50% charged damage; reduces maximum simultaneous locks to 2 |
| Branching beam | +1 nearby arc at half beam damage |
| Link capacitor | Beam costs 5 fewer Links; bomb costs 10 fewer |
| Efficient boost | 20% less boost drain; guided-fire risk remains |
| Vector thrusters | 0.4s shorter evade cooldown and quicker steering response |
| Graze collector | +2 Links per successful close dodge, once per bullet |
| Chain memory | +0.6s to the time available between chained kills |
| Wide acquisition | Wider charged-shot acquisition cone |
| Twin pulse | Two bolts, each at 70% / 85% / 100% of ordinary damage |
| Rapid charge | 25% faster charge building; tap detection remains based on actual hold time |
| Bomb shelter | 0.4s protection after a bomb clears hostile shots |
| Link harvester | +1 Link per pulse/charged-shot kill; no beam/bomb refunds |

Damage upgrades apply to shield nodes and exposed boss cores, not just ordinary
enemies; shielded cores still require destroying their nodes. At level 3,
Piercing Pulse deals 175% of its unmodified damage. With level-3 Twin Pulse,
both bolts connecting deal 350% total. Focused Charge already increases damage
against bosses. None of these changes increases incoming player damage.

With Link Harvester 3 and Link Capacitor 3, an ordinary kill grants 4 Links,
beam costs 35 and bomb costs 70: from zero, 9 / 18 basic kills respectively,
instead of 50 / 100 with neither upgrade. This excludes boost, grazes, stronger
enemy rewards and saved Links. Special attacks cannot finance themselves.

The normal close-dodge reward is one Link. Invulnerable passes do not generate
graze rewards. Evade grants 0.23s protection and starts with a 3.2s cooldown,
which can fall to 2s with upgrades. It uses the existing invulnerability and
Energy system. At full upgrade capacity, salvage offers Links, a starting
weapon chain, or score instead of becoming an empty menu.

## Encounters

At boss arrival, surviving enemies peel outward and recede over 2.2 seconds
instead of disappearing. Their shots dissipate harmlessly over .65 seconds.
The next arena waits for the pool to clear; departed enemies give no rewards.
The weapon-chain timer is preserved through this clearance and boss approach.

The Core's lattice has a moving safe corridor. The Bastion's four shield
nodes are cannons; destroying one removes its contribution to the next volley.
The Wraith displays two decoys; its real core has two pale iris eyelids built
into the model. Its starting slot is seeded (left, middle or right), and all
three bodies cross to new slots every two volleys. Health and targeting follow
the same real body. No floating diamond identifies it.
The Mantis alternates broad claw-bank sweeps. The Leviathan mixes lattice,
aimed fan and vertical-wave patterns with gravity and its health phases.
Leviathan's height wave commits to your altitude rather than alternating fixed
top/bottom lanes. Later depths add crossfire and a second height row while
leaving vertical escape room. Health and formation pressure keep scaling;
attack cadence has a safety floor. Firewall projectiles use dark crimson armor
and red blades; pale brackets indicate the safe opening (formerly `OPEN`).
Later depths add secondary attacks to some patterns. After 65 seconds in a
boss fight, projectile/cadence and reinforcement pressure gradually rise.

Snipers mark and commit to a target before their fast shot. Carriers deploy
two small chasers at a time. Support craft halve damage to nearby ordinary
enemies; destroy the support to remove that protection. Ordinary formations
alternate 14s patrol, 10s assault and 8s recovery stretches. Surviving ships
retreat during recovery; already-fired projectiles remain dangerous.

Only one recursion modifier is active: crossfire adds flanking shots, gravity
tides add modest steering currents, and escort fleets introduce more support
craft. The modifier rotates on each black-hole crossing instead of accumulating
an unlimited number of simultaneous hazards.

## Personal bests and settings

Windows preferences are stored in `%LOCALAPPDATA%/Recursion94/pilot-v1.txt`.
Linux uses `$XDG_CONFIG_HOME/Recursion94` or `$HOME/Recursion94`. The game can
still run if this optional file cannot be written. `RECURSION_PROFILE_DIR`
overrides the base directory for isolated testing.

Survival uses active simulation time. Pausing, changing settings, choosing an
upgrade, black-hole collapse and staying on the death screen add no time.
The result screen includes bosses cleared, best chain, hits, near misses and
the final damage source. Local records are convenience data and are editable;
they are not online anti-cheat verification. `--boss` practice starts are
excluded. Normal restarts begin at the first round and can set records.

Steamworks integration, public leaderboards and account submission are not
included in this gameplay update. No server, account or network connection is
required to play.

## Verification

- Existing headless gameplay validation retains Energy damage, boost guidance,
  original special costs at level zero, invulnerability and endless progression.
- Added headless checks cover deterministic/distinct offers, caps and fallback
  salvage; timer exclusions; upgrade effects; one-time grazes; support removal;
  sniper commitment; carrier deployment; every boss pattern and safe corridor;
  cannon removal; overflow protection; and persistence through the Leviathan.
- `tools/playability_smoke.c` drives real production-loop keyboard and simulated
  controller inputs through pause, settings, rebinding, quit cancellation,
  salvage, collapse, death, restart and final quit. It captures screenshots.
- `tools/render_features.c` captures all five boss warnings/volleys, enemy roles,
  and 960x600 flight/settings/salvage screens using the production shaders.
- `tools/audio_controls_test.c` checks independent music/effects and full mute,
  and prints the pre-existing audio golden-hash result for baseline comparison.
- `tools/validate_windows.sh` and `tools/build_smoke.sh` cross-compile the checks
  with warnings treated as errors. Set `RAYLIB_WIN_SOURCE` if the library isn't
  at `../raylib-windows`; launch the resulting checks on Windows. Give the menu
  smoke test a fresh `RECURSION_PROFILE_DIR` on each run.

The release build packs and integrity-checks the EXE with UPX, then rejects
anything at or above 1,474,560 bytes. Timing and controller checks are automated;
human playtesting is still needed to tune enjoyment, enemy pressure and upgrade
balance across a wider range of player skill and physical controllers.

Latest release: Windows EXE **1,015,296 bytes**; SHA-256
`0cf280dd0e2043bfdb96be7484acb10060dc45f3c170f26aa2aecb262ad94d3b`.
Existing gameplay checks, 549 behavioral assertions, 2,048 generated zones, independent
audio controls, the 502-frame production-loop test, rendering and UPX integrity
all pass. The historical audio golden check still reports a mismatch: both the
original and updated source produce `a2b83674276419d1` on this toolchain. The new
volume controls did not change that baseline result.
