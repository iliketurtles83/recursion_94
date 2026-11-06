#ifndef GAMEPLAY_H
#define GAMEPLAY_H

#include "music_clock.h"
#include "raylib.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_ENEMIES 48
#define MAX_ENEMY_PROJECTILES 192   // room for a Core lattice wall (~90) on top of the fight
#define MAX_PLAYER_PROJECTILES 32
#define MAX_COMBAT_PARTICLES 96
#define MAX_LOCK_TARGETS 6
#define CORE_TRANSITION_DURATION 5.2f
#define BOSSES_PER_RECURSION 5
#define MAX_CHAIN_LINKS 100
// Specials (decisions 2026-11-05): beam and bomb cost the same, so the pool
// holds two. Both slow enemies and their shots to half tempo with the music
// (the beam during, the bomb after); the ship is not slowed.
#define BEAM_LINK_COST 50
#define BOMB_LINK_COST 50
#define STRETCH_SCALE 0.5f          // enemy and music tempo during a time stretch
#define BEAM_STEPS 8.0f             // beam length in 16ths of (stretched) music: half a bar
#define BOMB_STRETCH_STEPS 16.0f    // stretch after the bomb's wave: one bar
#define BOMB_WAVE_SPEED 200.0f      // arena units/s
#define BOMB_WAVE_REACH 135.0f
#define BOSS_LATTICE_GAP 3.0f       // Core wall: no shot centre within this of the lane
#define ENEMY_SHOT_HIT_RADIUS 0.6f  // enemy shot vs ship (clarity pass 2026-11-06: was 0.85)
#define SNIPER_MARK_SECONDS 1.05f   // sniper: aim committed, then the rail (level 1; shorter later)
#define MAX_RAILS 4
#define RAIL_HIT_RADIUS 0.6f        // the rail hits a ship this close to its line at the ship's plane
#define RAIL_SHOW_SECONDS 0.22f
#define CARRIER_BAY_SECONDS 0.8f    // carrier: bays open, then two chasers launch
#define SUPPORT_RADIUS 10.0f        // arena units: ordinary enemies this close to a support take half damage

// Pickup weapons (decisions 2026-11-05): one slot, fired with I, ammo-limited.
// Capsules drop from a wiped group or a role group; the type is drawn from the
// seed. Ammo carries between stages and resets on death (a new run).
#define MAX_CAPSULES 3
#define MAX_ARCS 4
#define ARC_MAX_HITS 6              // first target + 5 jumps, one per 16th
#define ARC_JUMP_RADIUS 13.0f       // arena units from the last target
#define MAX_DRILLS 6
#define MIRROR_CATCH_RADIUS 2.0f    // a rolling ship catches shots this close (hit radius is 0.6)

// Barrel roll (roadmap step 7): i-frames only in the middle of the roll, so
// spamming rolls (0.7 s + 0.35 s cooldown) covers about a third of the time,
// not two thirds. A near miss (graze) pays chain links: a shot or rail that
// crosses the ship's plane inside GRAZE_RADIUS without hurting it, including
// one the roll's i-frames let through. Not during post-hit invulnerability.
#define ROLL_IFRAME_START 0.2f      // roll progress (0..1) where the i-frames start
#define ROLL_IFRAME_END 0.7f        // ... and end
#define GRAZE_RADIUS 1.5f           // arena units (hit radius 0.6)
#define GRAZE_LINKS 1
#define GRAZE_SCORE 50
#define GRAZE_COOLDOWN 0.2f         // seconds between paying grazes (a lattice wall pays a few, not dozens)
#define GRAZE_STREAK_SECONDS 1.5f   // grazes this close together climb the scale

typedef enum {
    WEAPON_NONE = 0,
    WEAPON_ARC,     // chain lightning: jumps target to target on 16ths, ignores support shields
    WEAPON_DRILL,   // sticks, ticks on 16ths, detonates on a bar line: armour and bosses
    WEAPON_MIRROR,  // passive: a barrel roll catches enemy shots and returns them to the shooter
    WEAPON_COUNT
} PickupWeapon;

typedef enum {
    ENEMY_DRIFTER = 0,
    ENEMY_CHASER,
    ENEMY_SPLITTER,
    ENEMY_BOSS_NODE,
    ENEMY_BOSS_CORE,
    // Roles from collaborator commit 886ba1b, each on its own depth and height
    // band, with its own outline and group:
    ENEMY_SNIPER,    // mirrored pair, high and far back; marks an aim point, then one fast shot
    ENEMY_CARRIER,   // alone, low and mid-depth, heavy; launches two small chasers at a time
    ENEMY_SUPPORT    // behind its group; halves damage to ordinary enemies near it, never fires
} EnemyType;

typedef enum {
    ENEMY_APPROACH = 0,
    ENEMY_HOVER,
    ENEMY_TELEGRAPH,
    ENEMY_ATTACK,
    ENEMY_ABSORBED   // boss arrival: pulled into the boss, explodes there (no reward, no boss damage)
} EnemyPhase;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    Vector3 basePosition;
    Vector3 size;
    Vector3 aim;    // sniper: the committed aim point while it telegraphs
    EnemyType type;
    EnemyPhase phase;
    float age;
    float phaseTime;
    float health;
    float fireTimer;
    float hurt;     // 1 on a hit that does not kill, decays fast: the white hit flash
    int identity;
    int generation;
    int group;      // formation / role group id (0 = none: bosses); wiping a group can drop a capsule
    int bombMark;   // the bomb that last touched it (boss parts take its chip once)
    bool deleting;  // touched by the bomb's wave: frozen, white, deleted on the next 16th
    bool active;
    bool locked;
} Enemy;

// What fired a shot: each source has its own shape (one hit radius for all).
typedef enum {
    SHOT_ORB = 0,   // drifters: a small orb
    SHOT_NEEDLE,    // splitters: short needles
    SHOT_BOSS       // bosses: the big orb, their signature
} EnemyShotKind;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float life;
    unsigned char kind;   // EnemyShotKind
    bool homing; // Mild boost guidance; retains lock until the shot expires.
    bool grazed; // already counted as a near miss
    bool active;
} EnemyProjectile;

// Sniper railgun: instant along the committed line; shown briefly.
typedef struct {
    Vector3 from, aim;
    float life;
    bool active;
} RailShot;

// Charged shots home toward their enemy slot. Tap shots use targetEnemy = -1
// and continue straight along the reticle lane.
typedef struct {
    Vector3 position;
    Vector3 velocity;
    float speed;
    float damage;
    float life;
    int targetEnemy;
    bool charged;
    bool active;
} PlayerProjectile;

typedef struct {
    Vector3 position;
    float age;
    PickupWeapon type;
    bool active;
} WeaponCapsule;

// Ring of recent groups: a group is wiped when every member spawned into it
// was killed (one that escapes, expires or is absorbed spoils it).
#define GROUP_RING 64
typedef struct {
    int id;
    short spawned, killed;
    bool role;       // sniper pair, carrier, support group: always drops when wiped
} EnemyGroup;

// One arc: points[0] is the muzzle, points[k + 1] where hit k landed.
typedef struct {
    Vector3 points[ARC_MAX_HITS + 1];
    int slots[ARC_MAX_HITS];
    int hits;
    float timer;   // to the next jump
    float fade;    // after the last jump: the bolt fades out
    bool done;
    bool active;
} ArcBolt;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    Vector3 offset;    // from the target's centre once stuck
    int target;
    float age;
    float stuck;       // seconds since it stuck (0 = in flight)
    float tickTimer;
    bool active;
} DrillRound;

typedef struct {
    Vector3 position;
    Vector3 origin;
    Vector3 velocity;
    float life;
    float maxLife;
    Color color;
    bool active;
    bool shockwave;
} CombatParticle;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float energy;
    float invulnerability;
    bool rolling;          // barrel roll in progress (the mirror is armed for all of it)
    bool dodging;          // the roll's i-frame window: no damage while set
    float chargeTime;
    float bank;
    Vector2 pull;          // Leviathan gravity (arena units/s); game.c hands it to the engine as drift
    bool fireHeldLast;
} PlayerState;

// Per-frame input. The ship position comes from the engine's flight model,
// converted into arena units (x lateral, y up, z = -distance ahead).
typedef struct {
    float playerX, playerY;
    float velocityX, velocityY;
    bool fireHeld;
    bool beamPressed;
    bool bombPressed;
    bool rolling;          // barrel roll in progress (zero-initialised input = not rolling)
    float rollProgress;    // 0..1 through the roll (the engine's ct_ship_roll_progress)
    bool weaponPressed;    // I: fire the pickup weapon
} GameplayInput;

typedef struct {
    float minX, maxX, minY, maxY;
} GameplayArena;

typedef struct {
    int tapShots;
    int chargeShots;
    int enemiesDestroyed;
    int enemiesAbsorbed;   // survivors that reached the arriving boss and exploded
    bool beamFired;
    bool bombFired;
    bool playerHit;
    bool weaponTierAdvanced;
    bool bossStarted;
    bool bossDefeated;
    bool recursionCompleted;
    // Pickup weapons. Notes are scale degrees for the synth (arc jumps climb the
    // arpeggio; drill ticks and mirror catches have their own registers).
    int weaponNotes;
    signed char weaponNoteDegree[8];
    unsigned char weaponNoteKind[8];   // PickupWeapon of the note
    bool weaponCollected;
    bool weaponFired;
    bool weaponEmpty;      // I pressed with nothing to fire
    int drillDetonations;
    int railShots;
    int beamTicks;
    int capsulesDropped;
    int enemyVolleys;      // enemy and boss volleys fired this step (one sound each)
    float enemyVolleyX;    // arena x of the last volley, for stereo placement
    int grazes;            // paying near misses this step
    signed char grazeDegree;   // scale degree of the last one (climbs with the streak)
} GameplayEvents;

typedef enum {
    BOSS_DORMANT = 0,
    BOSS_APPROACH,
    BOSS_SHIELDED,
    BOSS_EXPOSED,
    BOSS_ENRAGED,
    BOSS_SINGULARITY
} BossPhase;

typedef struct {
    Vector3 position;
    float maxHealth;
    float phaseTime;
    float fireTimer;
    float reinforcementTimer;
    float rotation;
    double nextSpawnDistance;
    float introFlash;
    float defeatFlash;
    int coreSlot;
    int nodeSlots[4];
    int shieldNodes;
    int encounterIndex;
    int attackPattern;
    BossPhase phase;
    bool leviathan;
    bool active;
    float latticeLane;     // Core desperation wall: arena x of the safe lane
    float latticeGuide;    // seconds the safe-lane guide stays up (0 = none)
    bool latticeArmed;     // the next volley is the wall
    bool clearing;         // boss distance reached: no new formations, the boss arrives on a bar line
    int clearBars;         // bar lines crossed while clearing
    float clearTime;
} BossState;

typedef struct {
    PlayerState player;
    BossState boss;
    uint32_t runSeed;
    Color primaryColor;
    Color secondaryColor;
    Color hotColor;
    Color threatColor;
    Color shadowBodyColor;
    Enemy enemies[MAX_ENEMIES];
    EnemyProjectile projectiles[MAX_ENEMY_PROJECTILES];
    PlayerProjectile playerProjectiles[MAX_PLAYER_PROJECTILES];
    CombatParticle particles[MAX_COMBAT_PARTICLES];
    RailShot rails[MAX_RAILS];
    WeaponCapsule capsules[MAX_CAPSULES];
    ArcBolt arcs[MAX_ARCS];
    DrillRound drills[MAX_DRILLS];
    PickupWeapon weapon;
    int ammo;
    float weaponCooldown;
    bool weaponQueued;     // pressed: fires on the next 16th
    bool mirrorRollCaught; // this roll already spent a mirror charge
    float mirrorFlash;
    float capsuleCooldown;
    int groupSerial;
    int spawnGroup;        // group new enemies join (0 = none)
    int dropSerial;
    EnemyGroup groups[GROUP_RING];

    int score;
    int combo;
    int chainLinks;
    int lockedCount;
    int spawnSerial;
    int killSerial;
    float comboTimer;
    float spawnTimer;
    float difficulty;
    float hitFlash;
    float cameraKick;
    float weaponFlash;
    float grazeCooldown;   // seconds until a graze pays again
    float grazeStreakTimer;
    int grazeStreak;
    float beamTimer;       // seconds since the beam started (visuals)
    float bombTimer;       // seconds since the bomb's wave started (visuals)
    float beamSteps;       // > 0: the beam is firing (16ths left); the ship is held in place
    float beamTickTimer;
    float stretchSteps;    // > 0: time stretch (16ths of stretched music left)
    bool bombActive;       // the wave is expanding
    Vector3 bombOrigin;
    float bombRadius;
    float bombTickTimer;
    int bombSerial;
    float loopTransitionTimer;
    float runTime;
    int weaponTier;
    GameplayArena arena;
    MusicClock music;      // set each frame by the game; invalid in headless tests
    bool bossesEnabled;    // validation and the game enable them; tests may switch them off
    bool suppressLinkRewards;
    bool overdriveActive;
    bool gameOver;
} GameplaySystem;

void InitGameplay(GameplaySystem *game, uint32_t runSeed);
void AdvanceGameplayLoop(GameplaySystem *game, uint32_t runSeed, double virtualPlayerZ);
int GetGameplayRound(const GameplaySystem *game);
// The support protecting this enemy (its slot), or -1. Protected enemies take half damage.
int GetEnemySupport(const GameplaySystem *game, const Enemy *enemy);
int GetGameplayRecursionDepth(const GameplaySystem *game);
bool CanGameplayBoost(const GameplaySystem *game);
// Unboosted, no-boss travel speed; GetGameplayTravelSpeed / this = engine speed factor.
float GetGameplayCruiseSpeed(const GameplaySystem *game, double virtualPlayerZ);
float GetGameplayTravelSpeed(const GameplaySystem *game, double virtualPlayerZ,
                             bool boosting);
void SetGameplayArena(GameplaySystem *game, GameplayArena arena);
void SetGameplayMusicClock(GameplaySystem *game, const MusicClock *clock);
GameplayEvents UpdateGameplay(GameplaySystem *game, float dt, double virtualPlayerZ,
                              bool boosting, const GameplayInput *input);
bool ValidateGameplaySpecials(void);
// 1, or STRETCH_SCALE during a time stretch: enemies, their shots and the music.
float GetGameplayTimeScale(const GameplaySystem *game);
// The beam holds the ship in place while it fires (no steering, rolls or boost).
bool IsGameplayBeamLocked(const GameplaySystem *game);
// Full magazine of a pickup weapon; a top-up adds this, up to twice it.
int GetWeaponAmmoFull(PickupWeapon weapon);
// The sniper's mark before the rail: 1.05 s on level 1, shorter on later levels and depths.
float GetSniperMarkSeconds(const GameplaySystem *game);
const char *GetWeaponName(PickupWeapon weapon);

#endif // GAMEPLAY_H
