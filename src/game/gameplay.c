#include "gameplay.h"
#include "palette.h"
#include "raymath.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define TAP_THRESHOLD 0.12f
#define GAMEPLAY_LATERAL_SCALE 1.5f
// Arena bounds are runtime values (GameplaySystem.arena), set from the engine's
// steering box. The defaults are the original game's box, so formulas below
// reproduce the original numbers there.
#define LEGACY_MAX_X (10.0f * GAMEPLAY_LATERAL_SCALE)
#define LEGACY_MIN_Y 0.8f
#define LEGACY_MAX_Y 7.0f
#define PLAYER_MIN_X (game->arena.minX)
#define PLAYER_MAX_X (game->arena.maxX)
#define PLAYER_MIN_Y (game->arena.minY)
#define PLAYER_MAX_Y (game->arena.maxY)
#define ENEMY_FORMATION_CENTER_X (0.75f * game->arena.maxX)
#define ENEMY_FORMATION_LIMIT_X (0.90f * game->arena.maxX)
#define ENEMY_FORMATION_SPACING (3.2f * GAMEPLAY_LATERAL_SCALE)
// Height fraction t of the arena (0 = floor, 1 = ceiling).
#define ARENA_Y(t) (game->arena.minY + (t) * (game->arena.maxY - game->arena.minY))
// Boss geometry scales with the arena width; the centre line sits 0.2 (scaled)
// above mid-height. With the original box this gives the original 4.1 and radii.
#define BOSS_SCALE (game->arena.maxX / LEGACY_MAX_X)
#define BOSS_CENTER_Y (0.5f * (game->arena.minY + game->arena.maxY) + 0.2f * BOSS_SCALE)
#define PLAYER_BOLT_SPEED 85.0f
#define PLAYER_BALL_SPEED 52.0f
#define PLAYER_HOMING_TURN_RATE 7.5f
#define PLAYER_PROJECTILE_LIFE 3.0f
#define PLAYER_HIT_DAMAGE 18.0f
#define BOOST_HIT_MULTIPLIER 1.15f
#define BOOST_ENERGY_DRAIN 1.0f
#define BOOST_MISSILE_ACCEL 18.0f
#define BOOST_MISSILE_MAX_LATERAL 8.0f
#define BOOST_MISSILE_COMMIT_Z 4.0f

static float ClampFloat(float value, float minimum, float maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float Approach(float current, float target, float response, float dt) {
    float blend = 1.0f - expf(-response * dt);
    return current + (target - current) * blend;
}

// A timer that is due acts on the nearest grid line of the music clock:
// it fires on the first crossing once it is within half a division of zero,
// so the average rate stays as tuned. With no clock it is a plain timer.
static bool TimerOnGrid(const GameplaySystem *game, float timer, int division) {
    const MusicClock *clock = &game->music;
    if (!clock->valid) return timer <= 0.0f;
    float half = 0.5f * (float)division / clock->stepsPerSecond;
    return timer <= half && music_clock_crossed(clock, division);
}

static uint32_t MixBits(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static float Hash01(uint32_t value) {
    return (float)(MixBits(value) & 0x00ffffffu) / 16777215.0f;
}

int GetGameplayRound(const GameplaySystem *game) {
    return game->boss.encounterIndex % BOSSES_PER_RECURSION + 1;
}

int GetGameplayRecursionDepth(const GameplaySystem *game) {
    return game->boss.encounterIndex / BOSSES_PER_RECURSION + 1;
}

static float LoopPressure(const GameplaySystem *game) {
    float depth = (float)(GetGameplayRecursionDepth(game) - 1);
    return fminf((float)game->boss.encounterIndex * 0.055f + log1pf(depth) * 0.12f, 0.78f);
}

static int WeaponTierForChain(int combo) {
    if (combo >= 12) return 3;
    if (combo >= 8) return 2;
    if (combo >= 4) return 1;
    return 0;
}

static bool IsEnemyTargetable(const GameplaySystem *game, const Enemy *enemy) {
    if (!enemy->active) return false;
    if (enemy->phase == ENEMY_ABSORBED) return false;
    if (game->boss.phase == BOSS_APPROACH &&
        (enemy->type == ENEMY_BOSS_CORE || enemy->type == ENEMY_BOSS_NODE)) return false;
    if (enemy->type == ENEMY_BOSS_CORE &&
        (game->boss.phase == BOSS_APPROACH || game->boss.shieldNodes > 0)) return false;
    return true;
}

static void SetGameplayPalette(GameplaySystem *game) {
    const PaletteProfile *pal = GetPaletteForSeed(game->runSeed);
    game->primaryColor = pal->primary;
    game->secondaryColor = pal->secondary;
    game->hotColor = pal->hot;
    game->threatColor = pal->threat;
    game->shadowBodyColor = pal->shadowBody;
}

static float LengthSquared2D(float x, float y) {
    return x * x + y * y;
}

static float SegmentSphereHit(Vector3 start, Vector3 end, Vector3 center, float radius) {
    Vector3 segment = { end.x - start.x, end.y - start.y, end.z - start.z };
    Vector3 offset = { start.x - center.x, start.y - center.y, start.z - center.z };
    float lengthSquared = segment.x * segment.x + segment.y * segment.y + segment.z * segment.z;
    if (lengthSquared < 0.000001f) return -1.0f;

    float projection = -(offset.x * segment.x + offset.y * segment.y +
                         offset.z * segment.z) / lengthSquared;
    float hitTime = ClampFloat(projection, 0.0f, 1.0f);
    float dx = offset.x + segment.x * hitTime;
    float dy = offset.y + segment.y * hitTime;
    float dz = offset.z + segment.z * hitTime;
    return dx * dx + dy * dy + dz * dz <= radius * radius ? hitTime : -1.0f;
}

static int FindFreeEnemy(GameplaySystem *game) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!game->enemies[i].active) return i;
    }
    return -1;
}

static int FindFreeProjectile(GameplaySystem *game) {
    for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
        if (!game->projectiles[i].active) return i;
    }
    return -1;
}

static int FindFreePlayerProjectile(GameplaySystem *game) {
    for (int i = 0; i < MAX_PLAYER_PROJECTILES; i++) {
        if (!game->playerProjectiles[i].active) return i;
    }
    return -1;
}

static int FindFreeParticle(GameplaySystem *game) {
    for (int i = 0; i < MAX_COMBAT_PARTICLES; i++) {
        if (!game->particles[i].active) return i;
    }
    return -1;
}

static void ClearLocks(GameplaySystem *game) {
    for (int i = 0; i < MAX_ENEMIES; i++) game->enemies[i].locked = false;
    game->lockedCount = 0;
}

static Enemy *SpawnEnemyAt(GameplaySystem *game, EnemyType type, Vector3 position,
                           int generation, uint32_t seed) {
    int slot = FindFreeEnemy(game);
    if (slot < 0) return NULL;

    Enemy *enemy = &game->enemies[slot];
    *enemy = (Enemy){ 0 };
    enemy->active = true;
    enemy->type = type;
    enemy->phase = ENEMY_APPROACH;
    enemy->position = position;
    enemy->basePosition = position;
    enemy->identity = game->spawnSerial++;
    enemy->generation = generation;
    enemy->group = game->spawnGroup;
    if (enemy->group > 0) {
        EnemyGroup *g = &game->groups[enemy->group % GROUP_RING];
        if (g->id == enemy->group) g->spawned++;
    }
    enemy->fireTimer = (0.65f + Hash01(seed + 19u) * 0.75f) *
                       (1.0f - game->difficulty * 0.50f) *
                       (1.0f - LoopPressure(game) * 0.35f);

    switch (type) {
        case ENEMY_DRIFTER:
            enemy->size = (Vector3){ 1.8f, 1.8f, 1.8f };
            enemy->health = 1.0f;
            break;
        case ENEMY_CHASER:
            enemy->size = (Vector3){ 1.5f, 1.2f, 2.3f };
            enemy->health = 1.0f;
            break;
        case ENEMY_SPLITTER:
            enemy->size = (Vector3){ 2.5f, 2.5f, 2.5f };
            enemy->health = 2.0f;
            break;
        case ENEMY_SNIPER:
            enemy->size = (Vector3){ 1.0f, 1.0f, 3.2f };
            enemy->health = 2.0f;
            enemy->fireTimer = 1.4f + Hash01(seed + 23u) * 0.8f;
            break;
        case ENEMY_CARRIER:
            enemy->size = (Vector3){ 4.4f, 1.8f, 4.4f };
            enemy->health = 5.0f;
            enemy->fireTimer = 1.6f;
            break;
        case ENEMY_SUPPORT:
            enemy->size = (Vector3){ 2.2f, 2.2f, 2.2f };
            enemy->health = 3.0f;
            break;
        case ENEMY_BOSS_NODE:
            enemy->size = game->boss.leviathan
                ? (Vector3){ 2.35f, 2.35f, 2.35f }
                : (Vector3){ 1.8f, 1.8f, 1.8f };
            enemy->health = 1.7f + (float)GetGameplayRound(game) * 0.65f +
                            sqrtf((float)GetGameplayRecursionDepth(game)) *
                            (game->boss.leviathan ? 1.25f : 0.55f);
            break;
        case ENEMY_BOSS_CORE:
            enemy->size = game->boss.leviathan
                ? (Vector3){ 12.8f, 12.8f, 12.8f }
                : (Vector3){ 6.2f, 6.2f, 6.2f };
            if (game->boss.leviathan) {
                float depth = (float)GetGameplayRecursionDepth(game);
                enemy->health = 150.0f + sqrtf(depth) * 38.0f + log1pf(depth) * 24.0f;
            } else {
                enemy->health = 26.0f + (float)GetGameplayRound(game) * 11.0f +
                                sqrtf((float)(GetGameplayRecursionDepth(game) - 1)) * 14.0f;
            }
            break;
    }
    if (generation > 0) {
        enemy->size.x *= 0.68f;
        enemy->size.y *= 0.68f;
        enemy->size.z *= 0.68f;
    }
    return enemy;
}

// Enemies spawned between BeginGroup and EndGroup form one group.
static void BeginGroup(GameplaySystem *game, bool role) {
    game->spawnGroup = ++game->groupSerial;
    game->groups[game->spawnGroup % GROUP_RING] = (EnemyGroup){ game->spawnGroup, 0, 0, role };
}

static void EndGroup(GameplaySystem *game) { game->spawnGroup = 0; }

static void JoinGroup(GameplaySystem *game, Enemy *enemy, int group) {
    enemy->group = group;
    EnemyGroup *g = &game->groups[group % GROUP_RING];
    if (group > 0 && g->id == group) g->spawned++;
}

static int CountActive(const GameplaySystem *game, EnemyType type) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (game->enemies[i].active && game->enemies[i].type == type &&
            game->enemies[i].phase != ENEMY_ABSORBED) n++;
    }
    return n;
}

// Role groups (886ba1b's sniper, carrier and support), introduced by distance
// (difficulty 0.22 / 0.32 / 0.42: roughly 12, 20 and 30 s into the first
// level) and at most one group of each on the field. Each role has its own
// height band and hold depth, so they read apart before their outlines do.
#define ROLE_SPAWN_Z -110.0f
static bool SpawnRoleGroup(GameplaySystem *game, uint32_t seed) {
    float roll = Hash01(seed + 40u);
    float d = game->difficulty;
    if (d > 0.22f && roll < 0.14f && CountActive(game, ENEMY_SNIPER) == 0) {
        // A mirrored pair high up, far back.
        BeginGroup(game, true);
        float x = (0.40f + Hash01(seed + 41u) * 0.25f) * game->arena.maxX;
        for (int side = -1; side <= 1; side += 2) {
            Vector3 p = { (float)side * x, ARENA_Y(0.84f), ROLE_SPAWN_Z };
            SpawnEnemyAt(game, ENEMY_SNIPER, p, 0, seed + (uint32_t)(side + 2) * 31u);
        }
        EndGroup(game);
        return true;
    }
    if (d > 0.32f && roll < 0.26f && CountActive(game, ENEMY_CARRIER) == 0) {
        // Alone, low, off to one side.
        float x = (Hash01(seed + 42u) * 2.0f - 1.0f) * 0.45f * game->arena.maxX;
        BeginGroup(game, true);
        SpawnEnemyAt(game, ENEMY_CARRIER, (Vector3){ x, ARENA_Y(0.22f), ROLE_SPAWN_Z }, 0, seed + 7u);
        EndGroup(game);
        return true;
    }
    if (d > 0.42f && roll < 0.38f && CountActive(game, ENEMY_SUPPORT) == 0) {
        // A drifter line with its support behind the middle.
        float cx = (Hash01(seed + 43u) * 2.0f - 1.0f) * 0.5f * game->arena.maxX;
        float cy = ARENA_Y(0.40f + Hash01(seed + 44u) * 0.25f);
        BeginGroup(game, true);
        for (int i = 0; i < 3; i++) {
            Vector3 p = { cx + ((float)i - 1.0f) * ENEMY_FORMATION_SPACING * 0.8f, cy, ROLE_SPAWN_Z + 4.0f };
            SpawnEnemyAt(game, ENEMY_DRIFTER, p, 0, seed + 50u + (uint32_t)i * 31u);
        }
        SpawnEnemyAt(game, ENEMY_SUPPORT, (Vector3){ cx, cy + 1.2f, ROLE_SPAWN_Z - 6.0f }, 0, seed + 9u);
        EndGroup(game);
        return true;
    }
    return false;
}

static void SpawnFormation(GameplaySystem *game) {
    uint32_t seed = game->runSeed ^ (uint32_t)(game->spawnSerial + 1) * 0x9e3779b9u;
    if (SpawnRoleGroup(game, seed)) return;
    int count = game->boss.encounterIndex > 0 ? 2 : 1;
    if (game->difficulty > 0.08f && Hash01(seed + 1u) > 0.35f) count++;
    if (game->difficulty > 0.42f && Hash01(seed + 2u) > 0.48f) count++;
    if (GetGameplayRecursionDepth(game) >= 3 && Hash01(seed + 5u) > 0.62f) count++;
    if (count > 5) count = 5;

    float centerX = -ENEMY_FORMATION_CENTER_X +
                    Hash01(seed + 3u) * ENEMY_FORMATION_CENTER_X * 2.0f;
    float centerY = ARENA_Y(0.129f + Hash01(seed + 4u) * 0.742f);
    BeginGroup(game, false);
    for (int i = 0; i < count; i++) {
        float typeRoll = Hash01(seed + 11u + (uint32_t)i * 7u);
        EnemyType type = ENEMY_DRIFTER;
        if (game->difficulty > 0.08f && typeRoll > 0.50f) type = ENEMY_CHASER;
        if (game->difficulty > 0.25f && typeRoll > 0.80f) type = ENEMY_SPLITTER;

        float offset = ((float)i - (float)(count - 1) * 0.5f) * ENEMY_FORMATION_SPACING;
        Vector3 position = {
            ClampFloat(centerX + offset, -ENEMY_FORMATION_LIMIT_X, ENEMY_FORMATION_LIMIT_X),
            ClampFloat(centerY + sinf((float)i * 2.3f) * 1.2f, ARENA_Y(0.032f), ARENA_Y(0.919f)),
            -104.0f - Hash01(seed + 30u + (uint32_t)i) * 28.0f
        };
        SpawnEnemyAt(game, type, position, 0, seed + (uint32_t)i * 31u);
    }
    EndGroup(game);
}

static void SpawnProjectile(GameplaySystem *game, Vector3 origin, float speed, float spread) {
    int slot = FindFreeProjectile(game);
    if (slot < 0) return;

    float dx = game->player.position.x - origin.x + spread;
    float dy = game->player.position.y - origin.y;
    float dz = game->player.position.z - origin.z;
    float length = sqrtf(dx * dx + dy * dy + dz * dz);
    if (length < 0.001f) length = 1.0f;

    EnemyProjectile *projectile = &game->projectiles[slot];
    projectile->active = true;
    projectile->position = origin;
    projectile->velocity = (Vector3){ dx / length * speed, dy / length * speed, dz / length * speed };
    projectile->life = 6.0f;
    projectile->homing = game->overdriveActive;
}

// A shot from origin through an aim point on the player's plane. Never homes
// (patterns keep their shape under boost).
static void SpawnAimedProjectile(GameplaySystem *game, Vector3 origin, Vector3 aim, float speed) {
    int slot = FindFreeProjectile(game);
    if (slot < 0) return;
    float dx = aim.x - origin.x, dy = aim.y - origin.y, dz = aim.z - origin.z;
    float length = sqrtf(dx * dx + dy * dy + dz * dz);
    if (length < 0.001f) length = 1.0f;
    EnemyProjectile *projectile = &game->projectiles[slot];
    projectile->active = true;
    projectile->position = origin;
    projectile->velocity = (Vector3){ dx / length * speed, dy / length * speed, dz / length * speed };
    projectile->life = 6.0f;
    projectile->homing = false;
}

static int EnemySlot(const GameplaySystem *game, const Enemy *enemy) {
    return enemy ? (int)(enemy - game->enemies) : -1;
}

// Player shot with real flight time. Charged shots track targetSlot; tap shots
// use an explicit aim point and targetSlot = -1.
static void SpawnPlayerProjectile(GameplaySystem *game, Vector3 origin, Vector3 aim,
                                  int targetSlot, float speed, float damage, bool charged) {
    int slot = FindFreePlayerProjectile(game);
    if (slot < 0) return;

    float dx = aim.x - origin.x;
    float dy = aim.y - origin.y;
    float dz = aim.z - origin.z;
    float length = sqrtf(dx * dx + dy * dy + dz * dz);
    if (length < 0.001f) length = 1.0f;

    PlayerProjectile *projectile = &game->playerProjectiles[slot];
    projectile->active = true;
    projectile->position = origin;
    projectile->velocity = (Vector3){ dx / length * speed, dy / length * speed, dz / length * speed };
    projectile->speed = speed;
    projectile->damage = damage;
    projectile->life = PLAYER_PROJECTILE_LIFE;
    projectile->targetEnemy = targetSlot;
    projectile->charged = charged;
}

// Boss arrival window. Once the boss distance is reached no new formations
// spawn, and the boss arrives on a bar line: the first one after the field is
// clear, or after BOSS_CLEAR_BARS bars at the latest (survivors are then
// pulled into it). Without a music clock it is a plain BOSS_CLEAR_SECONDS.
#define BOSS_CLEAR_BARS 2
#define BOSS_CLEAR_SECONDS 4.0f
#define ABSORB_SECONDS 0.85f
#define ABSORB_STAGGER 0.35f

static bool FieldClear(const GameplaySystem *game) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (game->enemies[i].active) return false;
    }
    return true;
}

static bool BossArrivalDue(GameplaySystem *game) {
    BossState *boss = &game->boss;
    bool clear = FieldClear(game);
    if (!game->music.valid) return clear || boss->clearTime >= BOSS_CLEAR_SECONDS;
    if (!music_clock_crossed(&game->music, MUSIC_BAR)) return false;
    boss->clearBars++;
    return clear || boss->clearBars > BOSS_CLEAR_BARS;
}

static void SpawnBoss(GameplaySystem *game, double virtualPlayerZ, GameplayEvents *events) {
    BossState *boss = &game->boss;
    uint32_t encounterSeed = game->runSeed ^
        (uint32_t)(boss->encounterIndex + 1) * 0x9e3779b9u;

    // Survivors are pulled into the arriving boss and explode there
    // (UpdateAbsorbed): no reward, no damage to the boss. The core and its
    // four nodes need five free slots; if the pool is that full, the
    // furthest-along absorbed enemies go at once.
    int freeSlots = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *enemy = &game->enemies[i];
        if (!enemy->active) { freeSlots++; continue; }
        enemy->phase = ENEMY_ABSORBED;
        enemy->phaseTime = 0.0f;
        enemy->basePosition = enemy->position;
        enemy->locked = false;
    }
    for (int i = 0; i < MAX_ENEMIES && freeSlots < 5; i++) {
        if (game->enemies[i].active) { game->enemies[i].active = false; freeSlots++; }
    }
    for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) game->projectiles[i].active = false;
    for (int i = 0; i < MAX_PLAYER_PROJECTILES; i++) game->playerProjectiles[i].active = false;
    ClearLocks(game);

    boss->active = true;
    boss->leviathan = GetGameplayRound(game) == BOSSES_PER_RECURSION;
    boss->phase = BOSS_APPROACH;
    boss->phaseTime = 0.0f;
    boss->fireTimer = 1.1f;
    boss->reinforcementTimer = 3.4f;
    boss->rotation = Hash01(encounterSeed) * 6.2831853f;
    boss->attackPattern = 0;
    boss->latticeArmed = false;
    boss->latticeGuide = 0.0f;
    boss->position = (Vector3){ 0.0f, BOSS_CENTER_Y, boss->leviathan ? -148.0f : -125.0f };
    boss->shieldNodes = 4;
    boss->introFlash = 1.0f;
    boss->defeatFlash = 0.0f;
    boss->coreSlot = -1;
    for (int node = 0; node < 4; node++) boss->nodeSlots[node] = -1;
    boss->nextSpawnDistance = virtualPlayerZ + 1180.0f +
        (float)GetGameplayRound(game) * 90.0f + Hash01(encounterSeed ^ 0x51c3u) * 260.0f;
    game->spawnTimer = 1.2f;

    Enemy *core = SpawnEnemyAt(game, ENEMY_BOSS_CORE, boss->position, 0,
                               encounterSeed ^ 0xc0deu);
    boss->coreSlot = EnemySlot(game, core);
    if (core != NULL) boss->maxHealth = core->health;

    for (int node = 0; node < 4; node++) {
        Enemy *shield = SpawnEnemyAt(game, ENEMY_BOSS_NODE, boss->position,
                                     2 + node, encounterSeed + (uint32_t)node * 97u);
        boss->nodeSlots[node] = EnemySlot(game, shield);
    }
    events->bossStarted = true;
}

// Core desperation attack (enraged, from 886ba1b's Core pattern): a wall of
// shots that opens from the core into a lattice on the player's plane, with one
// clear lane. In the enraged phase every other volley is the wall; the volley
// before it picks the lane and raises the guide (combat_draw), so the lane is
// known about a second ahead. The holes between shots are tight but passable;
// a barrel roll's i-frames also carry you through.
#define LATTICE_DX 2.4f
#define LATTICE_DY 2.0f
#define LATTICE_WARN 1.05f      // seconds from the guide to the wall's volley
#define LATTICE_REST 1.75f      // seconds after a wall before the next volley

static bool CoreDesperate(const GameplaySystem *game) {
    const BossState *boss = &game->boss;
    return GetGameplayRound(game) == 1 && !boss->leviathan && boss->phase == BOSS_ENRAGED;
}

static float LatticeSpeed(const GameplaySystem *game) {
    return 24.0f + game->difficulty * 5.0f + LoopPressure(game) * 5.0f;
}

static void ArmLattice(GameplaySystem *game, Vector3 origin) {
    BossState *boss = &game->boss;
    const GameplayArena *a = &game->arena;
    float cx = 0.5f * (a->minX + a->maxX);
    float reach = fmaxf(0.5f * (a->maxX - a->minX) - BOSS_LATTICE_GAP - 0.5f, 0.0f);
    boss->latticeLane = cx + sinf((float)boss->attackPattern * 0.78f + (float)boss->encounterIndex) * reach;
    float flight = fabsf(origin.z - game->player.position.z) / LatticeSpeed(game);
    boss->latticeGuide = LATTICE_WARN + flight + 0.3f;
    boss->latticeArmed = true;
}

static int FireLattice(GameplaySystem *game, Vector3 origin) {
    BossState *boss = &game->boss;
    const GameplayArena *a = &game->arena;
    float cx = 0.5f * (a->minX + a->maxX);
    int cols = (int)((a->maxX - a->minX) / LATTICE_DX) + 2;
    float h = a->maxY - a->minY;
    int rows = (int)ceilf(h / LATTICE_DY) + 1;
    float speed = LatticeSpeed(game);
    int fired = 0;
    for (int c = 0; c < cols; c++) {
        float x = cx + ((float)c - (float)(cols - 1) * 0.5f) * LATTICE_DX;
        if (fabsf(x - boss->latticeLane) < BOSS_LATTICE_GAP) continue;
        for (int r = 0; r < rows; r++) {
            float y = a->minY + h * (float)r / (float)(rows - 1);
            SpawnAimedProjectile(game, origin, (Vector3){ x, y, game->player.position.z }, speed);
            fired++;
        }
    }
    boss->latticeArmed = false;
    return fired;
}

static void UpdateBoss(GameplaySystem *game, float dt, double virtualPlayerZ,
                       GameplayEvents *events) {
    BossState *boss = &game->boss;
    if (!boss->active) {
        if (!boss->clearing) {
            if (virtualPlayerZ < boss->nextSpawnDistance) return;
            boss->clearing = true;
            boss->clearBars = 0;
            boss->clearTime = 0.0f;
        }
        boss->clearTime += dt;
        if (BossArrivalDue(game)) {
            boss->clearing = false;
            SpawnBoss(game, virtualPlayerZ, events);
        }
        return;
    }

    boss->phaseTime += dt;
    boss->latticeGuide = fmaxf(0.0f, boss->latticeGuide - dt);
    float rotationSpeed = boss->phase == BOSS_SINGULARITY ? 2.85f
                        : boss->phase == BOSS_ENRAGED ? 1.95f
                        : boss->leviathan ? 1.38f : 1.05f;
    boss->rotation += dt * rotationSpeed;
    boss->position.z = Approach(boss->position.z, boss->leviathan ? -39.5f : -46.0f,
                                boss->leviathan ? 0.92f : 1.15f, dt);
    float scale = BOSS_SCALE;
    float motionScale = (boss->leviathan ? 3.7f : 2.2f) * scale;
    boss->position.x = sinf(game->runTime * (boss->leviathan ? 0.25f : 0.31f) +
                            (float)boss->encounterIndex) * motionScale;
    boss->position.y = BOSS_CENTER_Y + sinf(game->runTime * (boss->leviathan ? 0.43f : 0.57f)) *
                                  (boss->leviathan ? 1.0f : 0.65f) * scale;

    if (boss->phase == BOSS_APPROACH && boss->phaseTime >= 2.6f) {
        boss->phase = BOSS_SHIELDED;
        boss->phaseTime = 0.0f;
        boss->fireTimer = 0.75f;
    }

    Enemy *core = boss->coreSlot >= 0 ? &game->enemies[boss->coreSlot] : NULL;
    if (core != NULL && core->active) {
        core->position = boss->position;
        core->basePosition = boss->position;
        if (boss->leviathan && boss->shieldNodes == 0 &&
            core->health <= boss->maxHealth * 0.18f && boss->phase != BOSS_SINGULARITY) {
            boss->phase = BOSS_SINGULARITY;
            boss->phaseTime = 0.0f;
            boss->fireTimer = 0.10f;
            boss->reinforcementTimer = 1.4f;
        } else if (boss->shieldNodes == 0 &&
                   core->health <= boss->maxHealth * (boss->leviathan ? 0.52f : 0.46f) &&
                   boss->phase != BOSS_ENRAGED && boss->phase != BOSS_SINGULARITY) {
            boss->phase = BOSS_ENRAGED;
            boss->phaseTime = 0.0f;
            boss->fireTimer = 0.2f;
        }
    }

    for (int node = 0; node < 4; node++) {
        int slot = boss->nodeSlots[node];
        if (slot < 0 || !game->enemies[slot].active) continue;
        float angle = boss->rotation + (float)node * 1.5707963f;
        float radiusX = (boss->leviathan ? 12.4f : 8.35f) * scale;
        float radiusY = (boss->leviathan ? 7.15f : 4.65f) * scale;
        game->enemies[slot].position = (Vector3){
            boss->position.x + cosf(angle) * radiusX,
            boss->position.y + sinf(angle) * radiusY,
            boss->position.z + sinf(angle * 0.5f) * (boss->leviathan ? 3.4f : 2.1f)
        };
        game->enemies[slot].basePosition = game->enemies[slot].position;
    }

    if (boss->phase == BOSS_APPROACH) return;

    boss->reinforcementTimer -= dt;
    if (TimerOnGrid(game, boss->reinforcementTimer, MUSIC_BEAT)) {
        int reinforcements = boss->phase == BOSS_SINGULARITY ? 3
                           : (boss->phase == BOSS_ENRAGED || boss->leviathan ? 2 : 1);
        BeginGroup(game, false);
        for (int add = 0; add < reinforcements; add++) {
            uint32_t addSeed = game->runSeed ^ (uint32_t)game->spawnSerial * 0x85ebca6bu ^
                               (uint32_t)boss->encounterIndex * 97u;
            EnemyType type = (addSeed & 1u) ? ENEMY_DRIFTER : ENEMY_CHASER;
            float side = ((addSeed >> 3) & 1u) ? 1.0f : -1.0f;
            SpawnEnemyAt(game, type,
                         (Vector3){ boss->position.x + side * (5.0f + (float)add * 3.0f) * scale,
                                    boss->position.y + (-1.2f + (float)add) * scale,
                                    boss->position.z - 24.0f - (float)add * 5.0f },
                         0, addSeed);
        }
        EndGroup(game);
        boss->reinforcementTimer = boss->phase == BOSS_SINGULARITY ? 2.25f
                                   : boss->phase == BOSS_ENRAGED ? 2.7f
                                   : boss->leviathan ? 3.1f : 3.8f;
    }

    boss->fireTimer -= dt;
    if (!TimerOnGrid(game, boss->fireTimer, MUSIC_STEP)) return;

    Vector3 origin = boss->position;
    int shotCount = boss->phase == BOSS_SINGULARITY ? 10
                    : boss->phase == BOSS_ENRAGED ? (boss->leviathan ? 8 : 5)
                    : boss->phase == BOSS_EXPOSED ? (boss->leviathan ? 6 : 3)
                    : (boss->leviathan ? 4 : 2);
    if (boss->phase == BOSS_SHIELDED) {
        int preferred = ((int)(boss->rotation * 2.0f) & 3);
        for (int offset = 0; offset < 4; offset++) {
            int slot = boss->nodeSlots[(preferred + offset) & 3];
            if (slot >= 0 && game->enemies[slot].active) {
                origin = game->enemies[slot].position;
                break;
            }
        }
    }

    events->enemyVolleys++;
    events->enemyVolleyX = origin.x;
    if (boss->latticeArmed) {
        if (CoreDesperate(game)) FireLattice(game, origin);
        boss->latticeArmed = false;
        boss->attackPattern++;
        boss->fireTimer = LATTICE_REST;
        return;
    }
    float projectileSpeed = 23.0f + game->difficulty * 9.0f + LoopPressure(game) * 8.0f +
                            (boss->phase == BOSS_ENRAGED ? 4.0f : 0.0f) +
                            (boss->phase == BOSS_SINGULARITY ? 7.0f : 0.0f);
    for (int shot = 0; shot < shotCount; shot++) {
        float centered = (float)shot - (float)(shotCount - 1) * 0.5f;
        Vector3 shotOrigin = origin;
        float spread = centered * (boss->leviathan ? 1.22f : 1.45f);
        if (boss->leviathan && boss->attackPattern % 3 == 1) {
            float angle = boss->rotation + (float)shot * 2.399963f;
            shotOrigin.x += cosf(angle) * 10.5f * scale;
            shotOrigin.y += sinf(angle) * 6.2f * scale;
            spread = sinf(angle * 2.0f) * 2.4f;
        } else if (boss->leviathan && boss->attackPattern % 3 == 2) {
            shotOrigin.x += centered * 2.25f * scale;
            shotOrigin.y += (5.2f + sinf((float)shot * 1.7f) * 1.1f) * scale;
            spread = -centered * 0.62f;
        }
        SpawnProjectile(game, shotOrigin, projectileSpeed + (float)(shot % 3) * 1.8f, spread);
    }
    boss->attackPattern++;

    float cooldown = boss->phase == BOSS_SINGULARITY ? 0.32f
                     : boss->phase == BOSS_ENRAGED ? (boss->leviathan ? 0.43f : 0.48f)
                     : boss->phase == BOSS_EXPOSED ? (boss->leviathan ? 0.62f : 0.84f)
                     : (boss->leviathan ? 0.78f : 1.10f);
    cooldown -= game->difficulty * (boss->leviathan ? 0.12f : 0.22f);
    cooldown -= fminf((float)(GetGameplayRecursionDepth(game) - 1) * 0.012f, 0.08f);
    boss->fireTimer = fmaxf(cooldown, 0.24f);
    if (CoreDesperate(game) && (boss->attackPattern & 1)) {   // every other volley: the wall next
        ArmLattice(game, origin);
        boss->fireTimer = LATTICE_WARN;
    }
}

static void SpawnBurst(GameplaySystem *game, Vector3 position, Color color, int count) {
    uint32_t seed = game->runSeed ^ (uint32_t)(game->killSerial + 1) * 0x85ebca6bu;
    for (int i = 0; i < count; i++) {
        int slot = FindFreeParticle(game);
        if (slot < 0) return;
        float angle = Hash01(seed + (uint32_t)i * 3u) * 6.2831853f;
        float lift = -0.8f + Hash01(seed + (uint32_t)i * 3u + 1u) * 2.0f;
        float speed = 3.0f + Hash01(seed + (uint32_t)i * 3u + 2u) * 7.0f;
        CombatParticle *particle = &game->particles[slot];
        particle->active = true;
        particle->position = position;
        particle->origin = position;
        particle->velocity = (Vector3){ cosf(angle) * speed, lift * speed, sinf(angle) * speed };
        particle->life = 0.35f + Hash01(seed + (uint32_t)i + 80u) * 0.35f;
        particle->maxLife = particle->life;
        particle->color = color;
        particle->shockwave = i == 0;
    }
}


// ---------------------------------------------------------------------------
// Pickup weapons: capsules

#define CAPSULE_SPEED 16.0f         // arena units/s toward the player's plane
#define CAPSULE_PICK_RADIUS 2.2f
#define CAPSULE_DROP_CHANCE 0.25f   // an ordinary wiped group of 2+; role groups always drop
#define CAPSULE_COOLDOWN 12.0f      // seconds after a drop before the next can come

int GetWeaponAmmoFull(PickupWeapon weapon) {
    switch (weapon) {
        case WEAPON_ARC: return 10;
        case WEAPON_DRILL: return 5;
        case WEAPON_MIRROR: return 4;
        default: return 0;
    }
}

const char *GetWeaponName(PickupWeapon weapon) {
    switch (weapon) {
        case WEAPON_ARC: return "ARC";
        case WEAPON_DRILL: return "DRILL";
        case WEAPON_MIRROR: return "MIRROR";
        default: return "";
    }
}

static void DropCapsule(GameplaySystem *game, Vector3 position, GameplayEvents *events) {
    int slot = -1;
    for (int i = 0; i < MAX_CAPSULES; i++) if (!game->capsules[i].active) { slot = i; break; }
    if (slot < 0) return;
    uint32_t h = MixBits(game->runSeed ^ (uint32_t)(game->dropSerial + 1) * 0x27d4eb2du);
    game->dropSerial++;
    WeaponCapsule *c = &game->capsules[slot];
    c->active = true;
    c->age = 0.0f;
    c->type = (PickupWeapon)(WEAPON_ARC + (int)(h % 3u));
    c->position = position;
    c->position.x = ClampFloat(position.x, PLAYER_MIN_X + 1.0f, PLAYER_MAX_X - 1.0f);
    c->position.y = ClampFloat(position.y, PLAYER_MIN_Y + 0.5f, PLAYER_MAX_Y - 0.5f);
    game->capsuleCooldown = CAPSULE_COOLDOWN;
    events->capsulesDropped++;
}

// A member of `group` was killed. Kills by specials (beam, bomb) never drop.
static void GroupKilled(GameplaySystem *game, int group, Vector3 position, GameplayEvents *events) {
    EnemyGroup *g = &game->groups[group % GROUP_RING];
    if (g->id != group) return;
    g->killed++;
    if (g->killed < g->spawned) return;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (game->enemies[i].active && game->enemies[i].group == group) return;
    g->id = 0;   // wiped: count it once
    if (game->suppressLinkRewards) return;
    if (game->capsuleCooldown > 0.0f) return;
    bool drop = g->role || (g->spawned >= 2 &&
                Hash01(game->runSeed ^ (uint32_t)group * 0x9e3779b9u ^ 0xca95u) < CAPSULE_DROP_CHANCE);
    if (drop) DropCapsule(game, position, events);
}

static void UpdateCapsules(GameplaySystem *game, float dt, GameplayEvents *events) {
    game->capsuleCooldown = fmaxf(0.0f, game->capsuleCooldown - dt);
    for (int i = 0; i < MAX_CAPSULES; i++) {
        WeaponCapsule *c = &game->capsules[i];
        if (!c->active) continue;
        c->age += dt;
        c->position.z += CAPSULE_SPEED * dt;
        float dx = c->position.x - game->player.position.x;
        float dy = c->position.y - game->player.position.y;
        float dz = c->position.z - game->player.position.z;
        if (dz > -1.6f && dz < 1.6f && LengthSquared2D(dx, dy) < CAPSULE_PICK_RADIUS * CAPSULE_PICK_RADIUS) {
            int full = GetWeaponAmmoFull(c->type);
            if (game->weapon == c->type) {
                game->ammo = game->ammo + full > 2 * full ? 2 * full : game->ammo + full;
            } else {
                game->weapon = c->type;   // a different weapon replaces the slot and its ammo
                game->ammo = full;
                game->weaponQueued = false;
            }
            game->weaponFlash = 1.0f;
            events->weaponCollected = true;
            c->active = false;
        } else if (c->position.z > 4.0f) {
            c->active = false;
        }
    }
}

static void RegisterKill(GameplaySystem *game, Enemy *enemy, bool charged,
                         GameplayEvents *events) {
    Vector3 deathPosition = enemy->position;
    EnemyType deadType = enemy->type;
    int generation = enemy->generation;
    int group = enemy->group;
    enemy->active = false;
    enemy->locked = false;

    if (deadType == ENEMY_BOSS_NODE) {
        if (game->boss.shieldNodes > 0) game->boss.shieldNodes--;
        if (game->boss.shieldNodes == 0 && game->boss.active) {
            game->boss.phase = BOSS_EXPOSED;
            game->boss.phaseTime = 0.0f;
            game->boss.fireTimer = 0.45f;
        }
    } else if (deadType == ENEMY_BOSS_CORE) {
        bool completedRecursion = game->boss.leviathan;
        int clearedRound = GetGameplayRound(game);
        int clearedDepth = GetGameplayRecursionDepth(game);
        game->boss.active = false;
        game->boss.phase = BOSS_DORMANT;
        game->boss.defeatFlash = 1.0f;
        game->boss.encounterIndex++;
        game->player.energy = 100.0f;
        game->score += completedRecursion ? 50000 * clearedDepth
                                         : 8000 * clearedRound + 2500 * (clearedDepth - 1);
        events->bossDefeated = true;
        events->recursionCompleted = completedRecursion;
        if (completedRecursion) game->loopTransitionTimer = CORE_TRANSITION_DURATION;
        for (int node = 0; node < 4; node++) {
            int slot = game->boss.nodeSlots[node];
            if (slot >= 0 && slot < MAX_ENEMIES) game->enemies[slot].active = false;
        }
        for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) game->projectiles[i].active = false;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (game->enemies[i].type != ENEMY_BOSS_CORE) game->enemies[i].active = false;
        }
    }

    int previousTier = game->weaponTier;
    game->combo = game->comboTimer > 0.0f ? game->combo + 1 : 1;
    game->comboTimer = 2.6f;
    game->weaponTier = WeaponTierForChain(game->combo);
    if (game->weaponTier > previousTier) {
        game->weaponFlash = 1.0f;
        events->weaponTierAdvanced = true;
    }
    int overdriveMultiplier = game->overdriveActive ? 2 : 1;
    int worth = deadType == ENEMY_CARRIER ? 4
              : (deadType == ENEMY_SNIPER || deadType == ENEMY_SUPPORT) ? 3
              : deadType == ENEMY_SPLITTER ? 2 : 1;
    game->score += 100 * game->combo * worth * overdriveMultiplier;
    if (!game->suppressLinkRewards) {
        int linkReward = deadType == ENEMY_BOSS_CORE ? 10
                       : deadType == ENEMY_BOSS_NODE ? 5
                       : deadType == ENEMY_CARRIER ? 3
                       : (deadType == ENEMY_SPLITTER || deadType == ENEMY_SNIPER ||
                          deadType == ENEMY_SUPPORT) ? 2 : 1;
        if (game->overdriveActive && deadType != ENEMY_BOSS_CORE) linkReward++;
        game->chainLinks = game->chainLinks + linkReward > MAX_CHAIN_LINKS
            ? MAX_CHAIN_LINKS : game->chainLinks + linkReward;
    }
    game->player.energy = ClampFloat(game->player.energy + (charged ? 3.5f : 1.0f), 0.0f, 100.0f);
    game->cameraKick = fmaxf(game->cameraKick, deadType == ENEMY_SPLITTER ? 0.55f : 0.28f);
    game->killSerial++;
    events->enemiesDestroyed++;

    Color burstColor = deadType == ENEMY_BOSS_CORE ? game->hotColor
        : deadType == ENEMY_BOSS_NODE ? game->secondaryColor
        : deadType == ENEMY_CHASER
        ? (Color){ 255, 55, 130, 255 }
        : (Color){ 0, 235, 255, 255 };
    int burstCount = deadType == ENEMY_BOSS_CORE ? 32
                     : deadType == ENEMY_CARRIER ? 20
                     : (deadType == ENEMY_SPLITTER || deadType == ENEMY_SUPPORT ? 14 : 8);
    SpawnBurst(game, deathPosition, burstColor, burstCount);

    if (deadType == ENEMY_SPLITTER && generation == 0) {
        for (int side = -1; side <= 1; side += 2) {
            Vector3 childPosition = deathPosition;
            childPosition.x = ClampFloat(childPosition.x + (float)side * 1.4f,
                                         -0.6f * game->arena.maxX, 0.6f * game->arena.maxX);
            childPosition.z -= 2.0f;
            Enemy *child = SpawnEnemyAt(game, ENEMY_CHASER, childPosition, 1,
                                        game->runSeed ^ (uint32_t)game->killSerial * 17u ^
                                        (uint32_t)(side + 2));
            if (child != NULL) {
                child->phase = ENEMY_HOVER;
                child->phaseTime = 0.0f;
                child->basePosition = childPosition;
                JoinGroup(game, child, group);
            }
        }
    }
    // After the splitter's children joined, so splitting is not a wipe.
    if (group > 0) GroupKilled(game, group, deathPosition, events);
}

// Impact sparks for a hit that does not kill: a few short white streaks, no shockwave.
static void SpawnSparks(GameplaySystem *game, const Enemy *enemy) {
    uint32_t seed = game->runSeed ^ MixBits((uint32_t)enemy->identity * 0x9e3779b9u ^
                                            (uint32_t)(enemy->age * 977.0f));
    for (int i = 0; i < 4; i++) {
        int slot = FindFreeParticle(game);
        if (slot < 0) return;
        float angle = Hash01(seed + (uint32_t)i * 3u) * 6.2831853f;
        float lift = -1.0f + Hash01(seed + (uint32_t)i * 3u + 1u) * 2.0f;
        float speed = 6.0f + Hash01(seed + (uint32_t)i * 3u + 2u) * 6.0f;
        CombatParticle *particle = &game->particles[slot];
        *particle = (CombatParticle){ 0 };
        particle->active = true;
        particle->position = enemy->position;
        particle->position.z += enemy->size.x * 0.5f;   // the face toward the player
        particle->origin = particle->position;
        particle->velocity = (Vector3){ cosf(angle) * speed, lift * speed, 2.0f + sinf(angle) * speed * 0.5f };
        particle->life = 0.12f + Hash01(seed + (uint32_t)i + 80u) * 0.10f;
        particle->maxLife = particle->life;
        particle->color = (Color){ 235, 248, 255, 255 };
    }
}

// The support protecting this enemy (slot), or -1.
int GetEnemySupport(const GameplaySystem *game, const Enemy *enemy) {
    if (enemy->type == ENEMY_SUPPORT || enemy->type == ENEMY_BOSS_CORE ||
        enemy->type == ENEMY_BOSS_NODE || enemy->phase == ENEMY_ABSORBED) return -1;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *s = &game->enemies[i];
        if (!s->active || s->type != ENEMY_SUPPORT || s->phase == ENEMY_ABSORBED) continue;
        float dx = s->position.x - enemy->position.x, dy = s->position.y - enemy->position.y,
              dz = s->position.z - enemy->position.z;
        if (dx * dx + dy * dy + dz * dz < SUPPORT_RADIUS * SUPPORT_RADIUS) return i;
    }
    return -1;
}

static void ApplyDamage(GameplaySystem *game, Enemy *enemy, float damage, bool charged,
                        bool shielded, GameplayEvents *events) {
    if (!enemy->active) return;
    if (shielded && GetEnemySupport(game, enemy) >= 0) damage *= 0.5f;
    enemy->health -= damage;
    game->cameraKick = fmaxf(game->cameraKick, 0.12f);
    if (enemy->health <= 0.0f) RegisterKill(game, enemy, charged, events);
    else { enemy->hurt = 1.0f; SpawnSparks(game, enemy); }
}

static void DamageEnemy(GameplaySystem *game, Enemy *enemy, float damage, bool charged,
                        GameplayEvents *events) {
    ApplyDamage(game, enemy, damage, charged, true, events);
}


// ---------------------------------------------------------------------------
// Pickup weapons: ARC, DRILL, MIRROR. Everything lands on the 16th grid; with
// no music clock (tests) a 16th is a plain 0.125 s.

#define ARC_DAMAGE 2.0f
#define ARC_COOLDOWN 0.3f
#define ARC_FADE 0.35f
#define DRILL_SPEED 42.0f
#define DRILL_TICK_DAMAGE 0.3f
#define DRILL_BLAST_DAMAGE 4.0f
#define DRILL_SPLASH_DAMAGE 1.5f
#define DRILL_SPLASH_RADIUS 5.0f
#define DRILL_MIN_STUCK 0.4f        // at least this long ticking before the bar line counts
#define DRILL_MAX_STUCK 2.4f        // detonates by then whatever the clock does
#define DRILL_COOLDOWN 0.45f
#define MIRROR_SHOT_SPEED 75.0f
#define MIRROR_SHOT_DAMAGE 2.5f

static float StepSeconds(const GameplaySystem *game) {
    return game->music.valid && game->music.stepsPerSecond > 0.0f ? 1.0f / game->music.stepsPerSecond : 0.125f;
}

static void AddWeaponNote(GameplayEvents *events, PickupWeapon kind, int degree) {
    if (events->weaponNotes >= 8) return;
    events->weaponNoteDegree[events->weaponNotes] = (signed char)degree;
    events->weaponNoteKind[events->weaponNotes] = (unsigned char)kind;
    events->weaponNotes++;
}

// The first target ahead of the ship: the lock-on's choice (closest to the
// ship's line, then nearest), in a wider cone.
static int TargetAhead(const GameplaySystem *game) {
    int best = -1;
    float bestScore = 1e9f;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &game->enemies[i];
        if (!IsEnemyTargetable(game, e) || e->position.z >= game->player.position.z - 1.0f) continue;
        float depth = game->player.position.z - e->position.z;
        if (depth > 100.0f) continue;
        float cone = 5.0f + depth * 0.06f + e->size.x * 0.3f;
        float dx = e->position.x - game->player.position.x, dy = e->position.y - game->player.position.y;
        float lateral = LengthSquared2D(dx, dy);
        if (lateral > cone * cone) continue;
        float score = lateral * 5.0f + depth * 0.08f;
        if (score < bestScore) { best = i; bestScore = score; }
    }
    return best;
}

static int ArcNextTarget(const GameplaySystem *game, const ArcBolt *arc) {
    if (arc->hits == 0) return TargetAhead(game);
    Vector3 from = arc->points[arc->hits];
    int best = -1;
    float bestD = ARC_JUMP_RADIUS * ARC_JUMP_RADIUS;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &game->enemies[i];
        if (!IsEnemyTargetable(game, e) || e->position.z >= game->player.position.z) continue;
        bool visited = false;
        for (int k = 0; k < arc->hits; k++) visited |= arc->slots[k] == i;
        if (visited) continue;
        float dx = e->position.x - from.x, dy = e->position.y - from.y, dz = e->position.z - from.z;
        float d = dx * dx + dy * dy + dz * dz;
        if (d < bestD) { best = i; bestD = d; }
    }
    return best;
}

// Chord tones up the seed's scale: each jump is the next arpeggio note.
static const signed char ARC_DEGREES[ARC_MAX_HITS] = { 0, 2, 4, 7, 9, 11 };

static void ArcLand(GameplaySystem *game, ArcBolt *arc, int slot, GameplayEvents *events) {
    Enemy *e = &game->enemies[slot];
    arc->slots[arc->hits] = slot;
    arc->points[arc->hits + 1] = e->position;
    AddWeaponNote(events, WEAPON_ARC, ARC_DEGREES[arc->hits]);
    arc->hits++;
    ApplyDamage(game, e, ARC_DAMAGE, false, false, events);   // through the support's shield
    SpawnBurst(game, arc->points[arc->hits], (Color){ 235, 248, 255, 255 }, 3);
}

static bool FireArc(GameplaySystem *game, GameplayEvents *events) {
    int first = TargetAhead(game);
    if (first < 0) return false;
    int slot = -1;
    for (int i = 0; i < MAX_ARCS; i++) if (!game->arcs[i].active) { slot = i; break; }
    if (slot < 0) return false;
    ArcBolt *arc = &game->arcs[slot];
    *arc = (ArcBolt){ 0 };
    arc->active = true;
    arc->points[0] = game->player.position;
    arc->points[0].z -= 1.2f;
    ArcLand(game, arc, first, events);
    arc->timer = StepSeconds(game);
    return true;
}

static void UpdateArcs(GameplaySystem *game, float dt, GameplayEvents *events) {
    for (int i = 0; i < MAX_ARCS; i++) {
        ArcBolt *arc = &game->arcs[i];
        if (!arc->active) continue;
        if (arc->done) {
            arc->fade -= dt;
            if (arc->fade <= 0.0f) arc->active = false;
            continue;
        }
        arc->timer -= dt;
        if (!TimerOnGrid(game, arc->timer, MUSIC_STEP)) continue;
        int next = arc->hits < ARC_MAX_HITS ? ArcNextTarget(game, arc) : -1;
        if (next < 0) { arc->done = true; arc->fade = ARC_FADE; continue; }
        ArcLand(game, arc, next, events);
        arc->timer = StepSeconds(game);
    }
}

static bool FireDrill(GameplaySystem *game) {
    int slot = -1;
    for (int i = 0; i < MAX_DRILLS; i++) if (!game->drills[i].active) { slot = i; break; }
    if (slot < 0) return false;
    DrillRound *d = &game->drills[slot];
    *d = (DrillRound){ 0 };
    d->active = true;
    d->position = game->player.position;
    d->position.z -= 1.2f;
    d->target = TargetAhead(game);
    Vector3 aim = d->target >= 0 ? game->enemies[d->target].position
                                 : (Vector3){ d->position.x, d->position.y, -110.0f };
    Vector3 dir = Vector3Normalize((Vector3){ aim.x - d->position.x, aim.y - d->position.y, aim.z - d->position.z });
    d->velocity = (Vector3){ dir.x * DRILL_SPEED, dir.y * DRILL_SPEED, dir.z * DRILL_SPEED };
    return true;
}

static void DetonateDrill(GameplaySystem *game, DrillRound *d, GameplayEvents *events) {
    d->active = false;
    Vector3 at = d->position;
    if (d->target >= 0 && game->enemies[d->target].active)
        ApplyDamage(game, &game->enemies[d->target], DRILL_BLAST_DAMAGE, true, true, events);
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &game->enemies[i];
        if (i == d->target || !IsEnemyTargetable(game, e)) continue;
        float dx = e->position.x - at.x, dy = e->position.y - at.y, dz = e->position.z - at.z;
        if (dx * dx + dy * dy + dz * dz < DRILL_SPLASH_RADIUS * DRILL_SPLASH_RADIUS)
            ApplyDamage(game, e, DRILL_SPLASH_DAMAGE, true, true, events);
    }
    SpawnBurst(game, at, game->hotColor, 12);
    game->cameraKick = fmaxf(game->cameraKick, 0.5f);
    events->drillDetonations++;
}

static void UpdateDrills(GameplaySystem *game, float dt, GameplayEvents *events) {
    bool ticked = false;   // one tick note per frame, however many drills tick
    for (int i = 0; i < MAX_DRILLS; i++) {
        DrillRound *d = &game->drills[i];
        if (!d->active) continue;
        d->age += dt;
        Enemy *target = d->target >= 0 ? &game->enemies[d->target] : NULL;
        if (d->stuck > 0.0f) {
            if (target == NULL || !target->active) { DetonateDrill(game, d, events); continue; }
            d->stuck += dt;
            d->position = Vector3Add(target->position, d->offset);
            d->tickTimer -= dt;
            if (TimerOnGrid(game, d->tickTimer, MUSIC_STEP)) {
                d->tickTimer = StepSeconds(game);
                if (!ticked) AddWeaponNote(events, WEAPON_DRILL, 0);
                ticked = true;
                ApplyDamage(game, target, DRILL_TICK_DAMAGE, false, true, events);
                if (!target->active) { DetonateDrill(game, d, events); continue; }
            }
            bool bar = game->music.valid ? music_clock_crossed(&game->music, MUSIC_BAR) : d->stuck >= 1.6f;
            if ((d->stuck >= DRILL_MIN_STUCK && bar) || d->stuck >= DRILL_MAX_STUCK) DetonateDrill(game, d, events);
            continue;
        }
        // In flight: home like a charged shot, stick to the first enemy it meets.
        if (target != NULL && !target->active) { d->target = -1; target = NULL; }
        if (target != NULL) {
            Vector3 to = Vector3Subtract(target->position, d->position);
            float len = Vector3Length(to);
            if (len > 0.001f) {
                Vector3 desired = Vector3Scale(to, DRILL_SPEED / len);
                float turn = 1.0f - expf(-PLAYER_HOMING_TURN_RATE * dt);
                d->velocity = Vector3Add(d->velocity, Vector3Scale(Vector3Subtract(desired, d->velocity), turn));
            }
        }
        Vector3 prev = d->position;
        d->position = Vector3Add(d->position, Vector3Scale(d->velocity, dt));
        int hit = -1;
        float first = 2.0f;
        for (int k = 0; k < MAX_ENEMIES; k++) {
            Enemy *e = &game->enemies[k];
            if (!IsEnemyTargetable(game, e)) continue;
            float t = SegmentSphereHit(prev, d->position, e->position, 0.55f + e->size.x * 0.28f);
            if (t >= 0.0f && t < first) { first = t; hit = k; }
        }
        if (hit >= 0) {
            Enemy *e = &game->enemies[hit];
            d->target = hit;
            d->position = Vector3Lerp(prev, d->position, first);
            d->offset = Vector3Subtract(d->position, e->position);
            d->stuck = 0.0001f;
            d->tickTimer = StepSeconds(game);
            SpawnBurst(game, d->position, game->hotColor, 4);
        } else if (d->age > 3.0f || d->position.z < -200.0f) {
            d->active = false;
        }
    }
}

// Mirror: the enemy that fired a shot, found by running the shot's path
// backwards (aimed shots point straight back at the shooter; fans at the boss).
static int MirrorTarget(const GameplaySystem *game, const EnemyProjectile *p) {
    Vector3 back = Vector3Normalize(Vector3Negate(p->velocity));
    int best = -1;
    float bestScore = 1e9f;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &game->enemies[i];
        if (!IsEnemyTargetable(game, e) || e->position.z >= game->player.position.z - 1.0f) continue;
        Vector3 v = Vector3Subtract(e->position, p->position);
        float along = Vector3DotProduct(v, back);
        if (along <= 0.0f) continue;
        float off = Vector3LengthSqr(v) - along * along;
        float score = off - e->size.x * e->size.x * 0.25f;
        if (score < bestScore) { best = i; bestScore = score; }
    }
    return best >= 0 ? best : TargetAhead(game);
}

static void MirrorCatch(GameplaySystem *game, EnemyProjectile *p, GameplayEvents *events, bool *noted) {
    p->active = false;
    if (!game->mirrorRollCaught) {
        game->mirrorRollCaught = true;
        game->ammo--;
        events->weaponFired = true;
    }
    game->mirrorFlash = 1.0f;
    if (!*noted) AddWeaponNote(events, WEAPON_MIRROR, 14);
    *noted = true;
    int target = MirrorTarget(game, p);
    Vector3 aim = target >= 0 ? game->enemies[target].position
                              : (Vector3){ p->position.x, p->position.y, -110.0f };
    SpawnPlayerProjectile(game, p->position, aim, target, MIRROR_SHOT_SPEED, MIRROR_SHOT_DAMAGE, true);
}

static bool MirrorArmed(const GameplaySystem *game) {
    return game->player.dodging && game->weapon == WEAPON_MIRROR &&
           (game->ammo > 0 || game->mirrorRollCaught);
}

static void UpdatePickupWeapon(GameplaySystem *game, float dt, const GameplayInput *input,
                               GameplayEvents *events) {
    game->weaponCooldown = fmaxf(0.0f, game->weaponCooldown - dt);
    game->mirrorFlash = fmaxf(0.0f, game->mirrorFlash - dt * 3.0f);
    if (input->weaponPressed) {
        if ((game->weapon == WEAPON_ARC || game->weapon == WEAPON_DRILL) && game->ammo > 0)
            game->weaponQueued = true;
        else if (game->weapon == WEAPON_NONE)
            events->weaponEmpty = true;
    }
    if (game->weaponQueued && game->weaponCooldown <= 0.0f && TimerOnGrid(game, 0.0f, MUSIC_STEP)) {
        game->weaponQueued = false;
        bool fired = game->weapon == WEAPON_ARC ? FireArc(game, events)
                   : game->weapon == WEAPON_DRILL ? FireDrill(game) : false;
        if (fired) {
            game->ammo--;
            game->weaponCooldown = game->weapon == WEAPON_ARC ? ARC_COOLDOWN : DRILL_COOLDOWN;
            events->weaponFired = true;
        } else {
            events->weaponEmpty = true;   // nothing in range: no ammo spent
        }
    }
    if (game->ammo <= 0 && game->weapon != WEAPON_NONE && !MirrorArmed(game)) {
        game->ammo = 0;
        game->weapon = WEAPON_NONE;
        game->weaponQueued = false;
    }
}

static void FireTap(GameplaySystem *game, GameplayEvents *events) {
    Vector3 muzzle = game->player.position;
    muzzle.z -= 1.2f;
    Vector3 aim = { game->player.position.x, game->player.position.y, -110.0f };
    float speed = PLAYER_BOLT_SPEED + (float)game->weaponTier * 5.0f;
    float damage = 1.0f + (float)game->weaponTier * 0.5f;
    SpawnPlayerProjectile(game, muzzle, aim, -1, speed, damage, false);
    events->tapShots++;
}

static void FireCharge(GameplaySystem *game, GameplayEvents *events) {
    Vector3 muzzle = game->player.position;
    muzzle.z -= 1.2f;
    int fired = 0;
    for (int i = 0; i < MAX_ENEMIES && fired < MAX_LOCK_TARGETS; i++) {
        Enemy *enemy = &game->enemies[i];
        if (!enemy->active || !enemy->locked) continue;
        SpawnPlayerProjectile(game, muzzle, enemy->position, i, PLAYER_BALL_SPEED,
                              game->weaponTier >= 3 ? 3.0f : 2.0f, true);
        fired++;
    }
    events->chargeShots++;
    ClearLocks(game);
}

static void FireLinkBeam(GameplaySystem *game, GameplayEvents *events) {
    if (game->chainLinks < BEAM_LINK_COST) return;
    game->chainLinks -= BEAM_LINK_COST;
    game->beamTimer = 0.34f;
    game->cameraKick = fmaxf(game->cameraKick, 0.85f);
    game->suppressLinkRewards = true;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *enemy = &game->enemies[i];
        if (!IsEnemyTargetable(game, enemy)) continue;
        float dx = enemy->position.x - game->player.position.x;
        float dy = enemy->position.y - game->player.position.y;
        float radius = 1.1f + enemy->size.x * 0.35f;
        if (LengthSquared2D(dx, dy) > radius * radius) continue;
        float damage = enemy->type == ENEMY_BOSS_CORE ? 8.0f
                     : enemy->type == ENEMY_BOSS_NODE ? 4.0f : 1000.0f;
        DamageEnemy(game, enemy, damage, true, events);
    }
    game->suppressLinkRewards = false;
    events->beamFired = true;
}

static void FireLinkBomb(GameplaySystem *game, GameplayEvents *events) {
    if (game->chainLinks < BOMB_LINK_COST) return;
    game->chainLinks -= BOMB_LINK_COST;
    game->bombTimer = 0.72f;
    game->cameraKick = fmaxf(game->cameraKick, 1.4f);
    for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
        game->projectiles[i].active = false;
    }
    game->suppressLinkRewards = true;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *enemy = &game->enemies[i];
        if (!IsEnemyTargetable(game, enemy)) continue;
        float damage = enemy->type == ENEMY_BOSS_CORE ? 12.0f
                     : enemy->type == ENEMY_BOSS_NODE ? 7.0f : 1000.0f;
        DamageEnemy(game, enemy, damage, true, events);
    }
    game->suppressLinkRewards = false;
    events->bombFired = true;
}

static void UpdateLocks(GameplaySystem *game) {
    ClearLocks(game);
    if (game->player.chargeTime <= TAP_THRESHOLD) return;

    bool chosen[MAX_ENEMIES] = { false };
    int maximumLocks = game->weaponTier >= 2 ? 6 : 4;
    int targetLimit = game->player.chargeTime > 0.72f
        ? maximumLocks
        : (game->weaponTier >= 2 ? 4 : 3);
    float coneBase = 1.0f + ClampFloat(game->player.chargeTime, 0.0f, 1.2f) * 2.1f;

    for (int lock = 0; lock < targetLimit; lock++) {
        int best = -1;
        float bestScore = 1000000.0f;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *enemy = &game->enemies[i];
            if (!IsEnemyTargetable(game, enemy) || chosen[i] || enemy->position.z >= 0.0f) continue;
            float depth = -enemy->position.z;
            float cone = coneBase + depth * 0.035f;
            float dx = enemy->position.x - game->player.position.x;
            float dy = enemy->position.y - game->player.position.y;
            float lateral = LengthSquared2D(dx, dy);
            if (lateral > cone * cone) continue;
            float score = lateral * 5.0f + depth * 0.08f;
            if (score < bestScore) {
                best = i;
                bestScore = score;
            }
        }
        if (best < 0) break;
        chosen[best] = true;
        game->enemies[best].locked = true;
        game->lockedCount++;
    }
}

static void HitPlayer(GameplaySystem *game, Vector3 hitPosition, GameplayEvents *events) {
    if (game->player.invulnerability > 0.0f || game->player.dodging || game->gameOver) return;
    float hitDamage = PLAYER_HIT_DAMAGE *
                      (game->overdriveActive ? BOOST_HIT_MULTIPLIER : 1.0f);
    game->player.energy = fmaxf(0.0f, game->player.energy - hitDamage);
    game->player.invulnerability = 1.05f;
    game->combo = 0;
    game->comboTimer = 0.0f;
    game->weaponTier = 0;
    game->hitFlash = 1.0f;
    game->cameraKick = 1.0f;
    events->playerHit = true;
    SpawnBurst(game, hitPosition, (Color){ 255, 35, 115, 255 }, 16);
    if (game->player.energy <= 0.0f) game->gameOver = true;
}

// The ship's position comes from the engine's flight model (GameplayInput, in
// arena units). This keeps the original energy, special-attack and fire logic.
#define LEVIATHAN_PULL_CORE 2.0f   // arena units

static void UpdatePlayer(GameplaySystem *game, float dt, bool boosting,
                         const GameplayInput *input, GameplayEvents *events) {
    game->player.position.x = ClampFloat(input->playerX, PLAYER_MIN_X, PLAYER_MAX_X);
    game->player.position.y = ClampFloat(input->playerY, PLAYER_MIN_Y, PLAYER_MAX_Y);
    game->player.velocity.x = input->velocityX;
    game->player.velocity.y = input->velocityY;
    if (input->dodging && !game->player.dodging) game->mirrorRollCaught = false;   // a new roll
    game->player.dodging = input->dodging;

    // Fractal Leviathan gravity: reported for the flight model to apply.
    game->player.pull = (Vector2){ 0.0f, 0.0f };
    if (game->boss.active && game->boss.leviathan && game->boss.phase >= BOSS_SHIELDED) {
        float dx = game->boss.position.x - game->player.position.x;
        float dy = game->boss.position.y - game->player.position.y;
        float distance = sqrtf(dx * dx + dy * dy) + 0.001f;
        float gravity = game->boss.phase == BOSS_SINGULARITY ? 5.4f
                        : game->boss.phase == BOSS_ENRAGED ? 3.4f : 2.1f;
        gravity += fminf((float)(GetGameplayRecursionDepth(game) - 1) * 0.16f, 1.8f);
        // Soft core: the pull fades out over the last couple of units, so a ship
        // parked under the boss settles instead of flipping direction every frame.
        gravity *= fminf(distance / LEVIATHAN_PULL_CORE, 1.0f);
        game->player.pull = (Vector2){ dx / distance * gravity, dy / distance * gravity * 0.72f };
    }

    if (input->beamPressed) FireLinkBeam(game, events);
    if (input->bombPressed) FireLinkBomb(game, events);
    UpdatePickupWeapon(game, dt, input, events);

    if (boosting) game->player.energy = fmaxf(0.0f, game->player.energy - BOOST_ENERGY_DRAIN * dt);
    else game->player.energy = fminf(100.0f, game->player.energy + 1.6f * dt);

    bool fireHeld = input->fireHeld;
    if (fireHeld) {
        game->player.chargeTime = fminf(game->player.chargeTime + dt, 1.25f);
        UpdateLocks(game);
    } else if (game->player.fireHeldLast) {
        if (game->player.chargeTime <= TAP_THRESHOLD) FireTap(game, events);
        else FireCharge(game, events);
        game->player.chargeTime = 0.0f;
    }
    game->player.fireHeldLast = fireHeld;
}

static void SetEnemyPhase(Enemy *enemy, EnemyPhase phase) {
    enemy->phase = phase;
    enemy->phaseTime = 0.0f;
}

// Pulled into the arriving boss: bulges outward first, then falls in with
// increasing speed, whitening, and bursts at the boss's position.
static void UpdateAbsorbed(GameplaySystem *game, Enemy *enemy, float dt, GameplayEvents *events) {
    enemy->phaseTime += dt;
    enemy->age += dt;
    float span = ABSORB_SECONDS + Hash01(game->runSeed ^ (uint32_t)enemy->identity * 0x2c1b3c6du) * ABSORB_STAGGER;
    float t = fminf(enemy->phaseTime / span, 1.0f);
    float side = t * t * (3.0f - 2.0f * t);
    float bulge = sinf(t * 3.14159265f) * 0.45f;
    Vector3 a = enemy->basePosition;
    Vector3 b = game->boss.position;
    enemy->position.x = a.x + (b.x - a.x) * side + (a.x - b.x) * bulge;
    enemy->position.y = a.y + (b.y - a.y) * side + (a.y - b.y) * bulge * 0.5f;
    enemy->position.z = a.z + (b.z - a.z) * t * t;
    enemy->hurt = t;
    if (t >= 1.0f) {
        SpawnBurst(game, enemy->position, game->threatColor, 10);
        enemy->active = false;
        events->enemiesAbsorbed++;
    }
}

// Sniper, carrier, support. Each holds its own depth; the sniper commits its
// aim point at the start of the telegraph and never tracks after it.
#define SNIPER_HOLD_Z -48.0f
#define CARRIER_HOLD_Z -37.0f
#define SUPPORT_HOLD_Z -33.0f
#define SNIPER_SHOT_SPEED 46.0f

static void UpdateRoleEnemy(GameplaySystem *game, Enemy *enemy, float dt, bool overdrive,
                            GameplayEvents *events) {
    enemy->age += dt;
    enemy->phaseTime += dt;
    float identity = (float)enemy->identity;
    float holdZ = enemy->type == ENEMY_SNIPER ? SNIPER_HOLD_Z
                : enemy->type == ENEMY_CARRIER ? CARRIER_HOLD_Z : SUPPORT_HOLD_Z;
    if (enemy->phase == ENEMY_APPROACH) {
        enemy->position.z += (enemy->type == ENEMY_CARRIER ? 15.0f : 20.0f) * dt;
        if (enemy->position.z >= holdZ) {
            enemy->position.z = holdZ;
            enemy->basePosition = enemy->position;
            SetEnemyPhase(enemy, ENEMY_HOVER);
        }
    } else {
        float sway = enemy->type == ENEMY_CARRIER ? 1.6f : enemy->type == ENEMY_SNIPER ? 0.6f : 1.0f;
        enemy->position.x = enemy->basePosition.x + sinf(enemy->age * 0.55f + identity) * sway;
        enemy->position.y = enemy->basePosition.y + sinf(enemy->age * 0.8f + identity) * 0.3f;
    }
    if (enemy->type == ENEMY_SUPPORT) { if (enemy->age > 30.0f) enemy->active = false; return; }

    if (enemy->phase == ENEMY_HOVER) {
        enemy->fireTimer -= dt * (overdrive ? 1.6f : 1.0f);
        if (enemy->fireTimer <= 0.0f) {
            enemy->aim = game->player.position;
            SetEnemyPhase(enemy, ENEMY_TELEGRAPH);
        }
    } else if (enemy->phase == ENEMY_TELEGRAPH) {
        float delay = enemy->type == ENEMY_SNIPER ? SNIPER_MARK_SECONDS : CARRIER_BAY_SECONDS;
        if (TimerOnGrid(game, delay - enemy->phaseTime, MUSIC_STEP)) {
            if (enemy->type == ENEMY_SNIPER) {
                events->enemyVolleys++;
                events->enemyVolleyX = enemy->position.x;
                SpawnAimedProjectile(game, enemy->position, enemy->aim, SNIPER_SHOT_SPEED);
                enemy->fireTimer = 2.6f * (1.0f - game->difficulty * 0.25f);
            } else {
                int room = 0;
                for (int i = 0; i < MAX_ENEMIES; i++) room += !game->enemies[i].active;
                for (int side = -1; side <= 1 && room > 8; side += 2, room--) {
                    Vector3 p = enemy->position;
                    p.x += (float)side * enemy->size.x * 0.6f;
                    p.z += 1.5f;
                    Enemy *child = SpawnEnemyAt(game, ENEMY_CHASER, p, 1,
                                                game->runSeed ^ (uint32_t)game->spawnSerial * 31u);
                    if (child) {
                        child->phase = ENEMY_HOVER;
                        child->basePosition = p;
                        child->fireTimer = 1.4f + 0.3f * (float)(side + 1);
                    }
                }
                enemy->fireTimer = 4.4f * (1.0f - game->difficulty * 0.2f);
            }
            SetEnemyPhase(enemy, ENEMY_HOVER);
        }
    }
    if (enemy->age > 30.0f) enemy->active = false;
}

static void UpdateEnemy(GameplaySystem *game, Enemy *enemy, float dt, bool overdrive,
                        GameplayEvents *events) {
    if (enemy->phase == ENEMY_ABSORBED) { UpdateAbsorbed(game, enemy, dt, events); return; }
    if (enemy->type == ENEMY_BOSS_NODE || enemy->type == ENEMY_BOSS_CORE) return;
    if (enemy->type >= ENEMY_SNIPER) { UpdateRoleEnemy(game, enemy, dt, overdrive, events); return; }
    enemy->age += dt;
    enemy->phaseTime += dt;
    float identity = (float)enemy->identity;
    float holdZ = (enemy->type == ENEMY_SPLITTER ? -28.0f :
                   (enemy->type == ENEMY_CHASER ? -22.0f : -25.0f)) +
                  game->difficulty * 2.0f;
    if (enemy->generation > 0) holdZ = -22.0f;

    switch (enemy->phase) {
        case ENEMY_APPROACH:
            enemy->position.z += (13.0f + game->difficulty * 3.0f) * dt;
            enemy->position.x += sinf(enemy->age * 1.8f + identity) * dt * 1.1f;
            if (enemy->position.z >= holdZ) {
                enemy->position.z = holdZ;
                enemy->basePosition = enemy->position;
                SetEnemyPhase(enemy, ENEMY_HOVER);
            }
            break;

        case ENEMY_HOVER:
            if (enemy->type == ENEMY_DRIFTER) {
                enemy->position.x = enemy->basePosition.x +
                                    sinf(enemy->age * 1.45f + identity) *
                                    (3.1f * GAMEPLAY_LATERAL_SCALE);
                enemy->position.y = enemy->basePosition.y + cosf(enemy->age * 1.1f + identity) * 0.9f;
            } else if (enemy->type == ENEMY_SPLITTER) {
                enemy->position.x = enemy->basePosition.x +
                                    sinf(enemy->age * 1.7f + identity) *
                                    (2.0f * GAMEPLAY_LATERAL_SCALE);
                enemy->position.y = enemy->basePosition.y + cosf(enemy->age * 2.2f + identity) * 1.4f;
            } else {
                enemy->position.x = Approach(enemy->position.x, game->player.position.x, 0.35f, dt);
                enemy->position.y += sinf(enemy->age * 3.0f + identity) * dt * 0.8f;
            }
            // Boost accelerates each surviving enemy's attack cycle, not just
            // formation spawning. The projectile pool remains fixed-size.
            enemy->fireTimer -= dt * (overdrive ? 2.0f : 1.0f);
            if (enemy->fireTimer <= 0.0f) SetEnemyPhase(enemy, ENEMY_TELEGRAPH);
            break;

        case ENEMY_TELEGRAPH: {
            float track = enemy->type == ENEMY_CHASER ? 2.8f : 1.4f;
            enemy->position.x = Approach(enemy->position.x, game->player.position.x, track, dt);
            enemy->position.y = Approach(enemy->position.y, game->player.position.y, track, dt);
            float delay = enemy->type == ENEMY_CHASER
                ? 0.62f - game->difficulty * 0.12f
                : 0.90f - game->difficulty * 0.24f;
            if (overdrive) delay -= 0.10f;
            if (TimerOnGrid(game, delay - enemy->phaseTime, MUSIC_STEP)) {
                if (enemy->type == ENEMY_CHASER) {
                    float attackSpeed = 32.0f + game->difficulty * 10.0f +
                                        (overdrive ? 6.0f : 0.0f);
                    float travel = fmaxf(0.2f, -enemy->position.z / attackSpeed);
                    enemy->velocity.x = (game->player.position.x - enemy->position.x) / travel;
                    enemy->velocity.y = (game->player.position.y - enemy->position.y) / travel;
                    enemy->velocity.z = attackSpeed;
                } else if (enemy->type == ENEMY_SPLITTER) {
                    events->enemyVolleys++;
                    events->enemyVolleyX = enemy->position.x;
                    float pressure = overdrive ? 4.0f : 0.0f;
                    SpawnProjectile(game, enemy->position, 21.0f + game->difficulty * 6.0f + pressure, -1.6f);
                    SpawnProjectile(game, enemy->position, 23.0f + game->difficulty * 6.0f + pressure, 0.0f);
                    SpawnProjectile(game, enemy->position, 21.0f + game->difficulty * 6.0f + pressure, 1.6f);
                } else {
                    events->enemyVolleys++;
                    events->enemyVolleyX = enemy->position.x;
                    SpawnProjectile(game, enemy->position,
                                    22.0f + game->difficulty * 7.0f +
                                    (overdrive ? 4.0f : 0.0f), 0.0f);
                }
                SetEnemyPhase(enemy, ENEMY_ATTACK);
            }
            break;
        }

        case ENEMY_ATTACK:
            if (enemy->type == ENEMY_CHASER) {
                enemy->position.x += enemy->velocity.x * dt;
                enemy->position.y += enemy->velocity.y * dt;
                enemy->position.z += enemy->velocity.z * dt;
                float dx = enemy->position.x - game->player.position.x;
                float dy = enemy->position.y - game->player.position.y;
                if (enemy->position.z > -1.5f && enemy->position.z < 2.5f &&
                    LengthSquared2D(dx, dy) < 1.5f) {
                    HitPlayer(game, game->player.position, events);
                    enemy->active = false;
                } else if (enemy->position.z > 8.0f) {
                    enemy->active = false;
                }
            } else if (enemy->phaseTime > 0.35f) {
                enemy->fireTimer = (0.95f + Hash01(game->runSeed ^
                    (uint32_t)enemy->identity * 23u) * 1.0f) *
                    (1.0f - game->difficulty * 0.52f) *
                    (overdrive ? 0.62f : 1.0f);
                SetEnemyPhase(enemy, ENEMY_HOVER);
            }
            break;

        case ENEMY_ABSORBED:
            break;   // handled by UpdateAbsorbed
    }

    if (enemy->age > 28.0f) enemy->active = false;
}

static void UpdateProjectiles(GameplaySystem *game, float dt, GameplayEvents *events) {
    bool mirrorNoted = false;
    for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
        EnemyProjectile *projectile = &game->projectiles[i];
        if (!projectile->active) continue;
        if (MirrorArmed(game)) {
            float mz = projectile->position.z - game->player.position.z;
            float mx = projectile->position.x - game->player.position.x;
            float my = projectile->position.y - game->player.position.y;
            if (mz > -3.0f && mz < 1.5f &&
                LengthSquared2D(mx, my) < MIRROR_CATCH_RADIUS * MIRROR_CATCH_RADIUS) {
                MirrorCatch(game, projectile, events, &mirrorNoted);
                continue;
            }
        }
        if (game->overdriveActive) projectile->homing = true;
        // Lead the ship while there is room to turn, but commit to the final
        // trajectory near the player. A sharp late lateral reversal can evade
        // the shot; straight-line boosting cannot.
        float range = game->player.position.z - projectile->position.z;
        if (projectile->homing && projectile->velocity.z > 1.0f &&
            range > BOOST_MISSILE_COMMIT_Z) {
            float remaining = fmaxf(range / projectile->velocity.z, 0.16f);
            float leadX = ClampFloat(game->player.velocity.x * 0.06f, -0.9f, 0.9f);
            float leadY = ClampFloat(game->player.velocity.y * 0.06f, -0.7f, 0.7f);
            float aimX = ClampFloat(game->player.position.x + leadX,PLAYER_MIN_X,PLAYER_MAX_X);
            float aimY = ClampFloat(game->player.position.y + leadY,PLAYER_MIN_Y,PLAYER_MAX_Y);
            float desiredX = ClampFloat((aimX - projectile->position.x) /
                                        remaining,-BOOST_MISSILE_MAX_LATERAL,
                                        BOOST_MISSILE_MAX_LATERAL);
            float desiredY = ClampFloat((aimY - projectile->position.y) /
                                        remaining,-BOOST_MISSILE_MAX_LATERAL,
                                        BOOST_MISSILE_MAX_LATERAL);
            float turn = BOOST_MISSILE_ACCEL * dt;
            projectile->velocity.x += ClampFloat(desiredX-projectile->velocity.x,-turn,turn);
            projectile->velocity.y += ClampFloat(desiredY-projectile->velocity.y,-turn,turn);
        }
        Vector3 previous = projectile->position;
        projectile->position.x += projectile->velocity.x * dt;
        projectile->position.y += projectile->velocity.y * dt;
        projectile->position.z += projectile->velocity.z * dt;
        projectile->life -= dt;

        // Swept crossing keeps faster guided shots from skipping through the
        // player between frames at a low frame rate.
        float travelZ=projectile->position.z-previous.z;
        float fraction=travelZ>0.0001f
            ? ClampFloat((game->player.position.z-previous.z)/travelZ,0.0f,1.0f) : 1.0f;
        float hitX=previous.x+(projectile->position.x-previous.x)*fraction;
        float hitY=previous.y+(projectile->position.y-previous.y)*fraction;
        float dx=hitX-game->player.position.x;
        float dy=hitY-game->player.position.y;
        bool crossing = previous.z <= game->player.position.z + 1.0f &&
                        projectile->position.z >= game->player.position.z - 1.0f;
        float hitRadiusSquared=0.72f;
        if (crossing && LengthSquared2D(dx, dy) < hitRadiusSquared) {
            HitPlayer(game, projectile->position, events);
            projectile->active = false;
        } else if (projectile->life <= 0.0f || projectile->position.z > 9.0f) {
            projectile->active = false;
        }
    }
}

static void UpdatePlayerProjectiles(GameplaySystem *game, float dt, GameplayEvents *events) {
    for (int i = 0; i < MAX_PLAYER_PROJECTILES; i++) {
        PlayerProjectile *projectile = &game->playerProjectiles[i];
        if (!projectile->active) continue;
        projectile->life -= dt;

        Enemy *target = projectile->targetEnemy >= 0 ? &game->enemies[projectile->targetEnemy] : NULL;
        bool trackingLive = target != NULL && target->active;
        if (trackingLive) {
            float dx = target->position.x - projectile->position.x;
            float dy = target->position.y - projectile->position.y;
            float dz = target->position.z - projectile->position.z;
            float len = sqrtf(dx * dx + dy * dy + dz * dz);
            if (len > 0.001f) {
                Vector3 desired = { dx / len * projectile->speed, dy / len * projectile->speed,
                                    dz / len * projectile->speed };
                float turn = 1.0f - expf(-PLAYER_HOMING_TURN_RATE * dt);
                projectile->velocity.x += (desired.x - projectile->velocity.x) * turn;
                projectile->velocity.y += (desired.y - projectile->velocity.y) * turn;
                projectile->velocity.z += (desired.z - projectile->velocity.z) * turn;
            }
        }

        Vector3 previousPosition = projectile->position;
        projectile->position.x += projectile->velocity.x * dt;
        projectile->position.y += projectile->velocity.y * dt;
        projectile->position.z += projectile->velocity.z * dt;

        if (trackingLive) {
            float hitRadius = 0.55f + target->size.x * 0.28f + (projectile->charged ? 0.25f : 0.0f);
            if (SegmentSphereHit(previousPosition, projectile->position,
                                 target->position, hitRadius) >= 0.0f) {
                DamageEnemy(game, target, projectile->damage, projectile->charged, events);
                SpawnBurst(game, projectile->position,
                          projectile->charged ? game->hotColor : game->secondaryColor,
                          projectile->charged ? 6 : 3);
                projectile->active = false;
                continue;
            }
        } else if (projectile->targetEnemy < 0) {
            int hitEnemy = -1;
            float firstHit = 2.0f;
            for (int enemyIndex = 0; enemyIndex < MAX_ENEMIES; enemyIndex++) {
                Enemy *enemy = &game->enemies[enemyIndex];
                if (!IsEnemyTargetable(game, enemy)) continue;
                float hitRadius = 0.55f + enemy->size.x * 0.28f;
                float hitTime = SegmentSphereHit(previousPosition, projectile->position,
                                                 enemy->position, hitRadius);
                if (hitTime >= 0.0f && hitTime < firstHit) {
                    firstHit = hitTime;
                    hitEnemy = enemyIndex;
                }
            }
            if (hitEnemy >= 0) {
                projectile->position.x = previousPosition.x +
                    (projectile->position.x - previousPosition.x) * firstHit;
                projectile->position.y = previousPosition.y +
                    (projectile->position.y - previousPosition.y) * firstHit;
                projectile->position.z = previousPosition.z +
                    (projectile->position.z - previousPosition.z) * firstHit;
                DamageEnemy(game, &game->enemies[hitEnemy], projectile->damage, false, events);
                SpawnBurst(game, projectile->position, game->secondaryColor, 3);
                projectile->active = false;
                continue;
            }
        }

        if (projectile->life <= 0.0f || projectile->position.z < -220.0f) {
            projectile->active = false;
        }
    }
}

static void UpdateEffects(GameplaySystem *game, float dt) {
    for (int i = 0; i < MAX_COMBAT_PARTICLES; i++) {
        CombatParticle *particle = &game->particles[i];
        if (!particle->active) continue;
        particle->position.x += particle->velocity.x * dt;
        particle->position.y += particle->velocity.y * dt;
        particle->position.z += particle->velocity.z * dt;
        particle->velocity.x *= 1.0f - fminf(dt * 2.5f, 0.9f);
        particle->velocity.y *= 1.0f - fminf(dt * 2.5f, 0.9f);
        particle->velocity.z *= 1.0f - fminf(dt * 2.5f, 0.9f);
        particle->life -= dt;
        if (particle->life <= 0.0f) particle->active = false;
    }
    game->hitFlash = fmaxf(0.0f, game->hitFlash - dt * 2.7f);
    game->cameraKick = fmaxf(0.0f, game->cameraKick - dt * 3.5f);
    game->weaponFlash = fmaxf(0.0f, game->weaponFlash - dt * 1.25f);
    game->beamTimer = fmaxf(0.0f, game->beamTimer - dt);
    game->bombTimer = fmaxf(0.0f, game->bombTimer - dt);
    game->loopTransitionTimer = fmaxf(0.0f, game->loopTransitionTimer - dt);
    game->boss.introFlash = fmaxf(0.0f, game->boss.introFlash - dt * 0.48f);
    game->boss.defeatFlash = fmaxf(0.0f, game->boss.defeatFlash - dt * 0.42f);
}

void InitGameplay(GameplaySystem *game, uint32_t runSeed) {
    *game = (GameplaySystem){ 0 };
    game->runSeed = runSeed;
    SetGameplayPalette(game);
    game->arena = (GameplayArena){ -LEGACY_MAX_X, LEGACY_MAX_X, LEGACY_MIN_Y, LEGACY_MAX_Y };
    game->bossesEnabled = true;
    game->player.position = (Vector3){ 0.0f, 2.4f, 0.0f };
    game->player.energy = 100.0f;
    game->spawnTimer = 0.65f;
    game->boss.coreSlot = -1;
    for (int node = 0; node < 4; node++) game->boss.nodeSlots[node] = -1;
    game->boss.nextSpawnDistance = 1400.0f + Hash01(runSeed ^ 0xb05594u) * 320.0f;
}

void AdvanceGameplayLoop(GameplaySystem *game, uint32_t runSeed, double virtualPlayerZ) {
    game->runSeed = runSeed;
    SetGameplayPalette(game);

    for (int i = 0; i < MAX_ENEMIES; i++) game->enemies[i].active = false;
    for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) game->projectiles[i].active = false;
    for (int i = 0; i < MAX_PLAYER_PROJECTILES; i++) game->playerProjectiles[i].active = false;
    for (int i = 0; i < MAX_CAPSULES; i++) game->capsules[i].active = false;
    for (int i = 0; i < MAX_ARCS; i++) game->arcs[i].active = false;
    for (int i = 0; i < MAX_DRILLS; i++) game->drills[i].active = false;
    ClearLocks(game);   // the pickup weapon and its ammo carry over

    game->boss.coreSlot = -1;
    for (int node = 0; node < 4; node++) game->boss.nodeSlots[node] = -1;
    game->boss.leviathan = false;
    game->boss.clearing = false;
    game->boss.nextSpawnDistance = virtualPlayerZ + 1450.0f +
        Hash01(runSeed ^ (uint32_t)game->boss.encounterIndex * 0x51c3u) * 320.0f;
    game->spawnTimer = 0.45f;
}

bool ValidateGameplaySpecials(void) {
    GameplaySystem game;
    GameplayEvents events = { 0 };
    InitGameplay(&game, 94u);

    Enemy *target = &game.enemies[0];
    target->active = true;
    target->type = ENEMY_DRIFTER;
    target->health = 1.0f;
    target->size = (Vector3){ 1.0f, 1.0f, 1.0f };
    target->position = (Vector3){ game.player.position.x, game.player.position.y, -24.0f };

    game.chainLinks = BEAM_LINK_COST - 1;
    FireLinkBeam(&game, &events);
    bool insufficientFunds = game.chainLinks == BEAM_LINK_COST - 1 &&
                             target->active && !events.beamFired;

    game.chainLinks = BEAM_LINK_COST;
    FireLinkBeam(&game, &events);
    bool beamValid = game.chainLinks == 0 && !target->active && events.beamFired &&
                     events.enemiesDestroyed == 1;

    game.chainLinks = 73;
    AdvanceGameplayLoop(&game, 1337u, 2000.0);
    bool loopPersistence = game.chainLinks == 73;

    game.combo = 8;
    game.weaponTier = 2;
    // A barrel roll passes through hits: nothing changes while dodging.
    game.player.invulnerability = 0.0f;
    game.player.dodging = true;
    float energyBefore = game.player.energy;
    events = (GameplayEvents){ 0 };
    HitPlayer(&game, game.player.position, &events);
    bool dodgeValid = !events.playerHit && game.player.energy == energyBefore && game.combo == 8;
    game.player.dodging = false;

    HitPlayer(&game, game.player.position, &events);
    bool hitPersistence = game.chainLinks == 73 && game.combo == 0 && game.weaponTier == 0 &&
                          events.playerHit;

    for (int i = 0; i < 2; i++) {
        Enemy *enemy = &game.enemies[i];
        enemy->active = true;
        enemy->type = ENEMY_DRIFTER;
        enemy->health = 1.0f;
        enemy->size = (Vector3){ 1.0f, 1.0f, 1.0f };
        enemy->position = (Vector3){ (float)i * 4.0f, 2.0f, -30.0f };
    }
    game.projectiles[0].active = true;
    game.chainLinks = BOMB_LINK_COST;
    events = (GameplayEvents){ 0 };
    FireLinkBomb(&game, &events);
    bool bombValid = game.chainLinks == 0 && !game.enemies[0].active &&
                     !game.enemies[1].active && !game.projectiles[0].active &&
                     events.bombFired && events.enemiesDestroyed == 2;

    Enemy *core = &game.enemies[0];
    core->active = true;
    core->type = ENEMY_BOSS_CORE;
    core->health = 30.0f;
    core->size = (Vector3){ 7.0f, 7.0f, 7.0f };
    game.boss.active = true;
    game.boss.phase = BOSS_EXPOSED;
    game.boss.shieldNodes = 0;
    game.boss.coreSlot = 0;
    game.boss.maxHealth = 34.0f;
    game.chainLinks = BOMB_LINK_COST;
    events = (GameplayEvents){ 0 };
    FireLinkBomb(&game, &events);
    bool bossCapValid = fabsf(core->health - 18.0f) < 0.001f && core->active &&
                        game.chainLinks == 0;

    core->active = false;
    Enemy *rewardTarget = &game.enemies[1];
    rewardTarget->active = true;
    rewardTarget->type = ENEMY_DRIFTER;
    rewardTarget->health = 1.0f;
    rewardTarget->position = (Vector3){ 0.0f, 2.0f, -20.0f };
    game.chainLinks = MAX_CHAIN_LINKS - 1;
    events = (GameplayEvents){ 0 };
    DamageEnemy(&game, rewardTarget, 1.0f, false, &events);
    bool capValid = game.chainLinks == MAX_CHAIN_LINKS;

    GameplaySystem speedGame;
    InitGameplay(&speedGame, 94u);
    float startSpeed = GetGameplayTravelSpeed(&speedGame, 0.0, false);
    float lateSpeed = GetGameplayTravelSpeed(&speedGame, 1800.0, false);
    float boostSpeed = GetGameplayTravelSpeed(&speedGame, 1800.0, true);
    speedGame.boss.active = true;
    speedGame.boss.phase = BOSS_APPROACH;
    speedGame.boss.phaseTime = 0.75f;
    float approachSpeed = GetGameplayTravelSpeed(&speedGame, 1800.0, false);
    speedGame.boss.phase = BOSS_SHIELDED;
    float combatSpeed = GetGameplayTravelSpeed(&speedGame, 1800.0, false);
    bool speedValid = fabsf(startSpeed - 18.0f) < 0.001f &&
                      fabsf(lateSpeed - 30.0f) < 0.001f &&
                      boostSpeed > lateSpeed * 1.8f &&
                      fabsf(approachSpeed - 6.0f) < 0.001f &&
                      fabsf(combatSpeed - 6.0f) < 0.001f &&
                      !CanGameplayBoost(&speedGame);

    GameplaySystem roundGame;
    InitGameplay(&roundGame, 94u);
    roundGame.boss.active = true;
    roundGame.boss.leviathan = false;
    roundGame.boss.phase = BOSS_EXPOSED;
    roundGame.boss.shieldNodes = 0;
    roundGame.boss.coreSlot = 0;
    roundGame.enemies[0].active = true;
    roundGame.enemies[0].type = ENEMY_BOSS_CORE;
    roundGame.enemies[0].health = 1.0f;
    events = (GameplayEvents){ 0 };
    DamageEnemy(&roundGame, &roundGame.enemies[0], 1.0f, false, &events);
    bool roundAdvanceValid = events.bossDefeated && !events.recursionCompleted &&
                             roundGame.loopTransitionTimer <= 0.0f &&
                             GetGameplayRound(&roundGame) == 2 &&
                             GetGameplayRecursionDepth(&roundGame) == 1;

    GameplaySystem leviathanGame;
    InitGameplay(&leviathanGame, 94u);
    leviathanGame.boss.encounterIndex = 4;
    leviathanGame.boss.nextSpawnDistance = 0.0;
    events = (GameplayEvents){ 0 };
    UpdateBoss(&leviathanGame, 0.01f, 1.0, &events);
    bool leviathanSpawnValid = events.bossStarted && leviathanGame.boss.leviathan &&
                               leviathanGame.boss.maxHealth > 180.0f;
    leviathanGame.boss.phase = BOSS_EXPOSED;
    leviathanGame.boss.shieldNodes = 0;
    Enemy *leviathanCore = &leviathanGame.enemies[leviathanGame.boss.coreSlot];
    leviathanCore->health = leviathanGame.boss.maxHealth * 0.17f;
    UpdateBoss(&leviathanGame, 0.01f, 1.0, &events);
    bool singularityValid = leviathanGame.boss.phase == BOSS_SINGULARITY;
    leviathanCore->health = 1.0f;
    events = (GameplayEvents){ 0 };
    DamageEnemy(&leviathanGame, leviathanCore, 1.0f, false, &events);
    bool recursionAdvanceValid = events.bossDefeated && events.recursionCompleted &&
                                 leviathanGame.loopTransitionTimer == CORE_TRANSITION_DURATION &&
                                 GetGameplayRound(&leviathanGame) == 1 &&
                                 GetGameplayRecursionDepth(&leviathanGame) == 2;

    GameplaySystem riskGame;
    InitGameplay(&riskGame, 94u);
    events = (GameplayEvents){0};
    HitPlayer(&riskGame,riskGame.player.position,&events);
    bool ordinaryDamageValid=events.playerHit &&
        fabsf(riskGame.player.energy-82.0f)<0.001f;
    InitGameplay(&riskGame,94u);
    riskGame.overdriveActive=true;
    events=(GameplayEvents){0};
    HitPlayer(&riskGame,riskGame.player.position,&events);
    bool boostDamageValid=events.playerHit &&
        fabsf(riskGame.player.energy-79.3f)<0.001f && !riskGame.gameOver;
    InitGameplay(&riskGame,94u);
    riskGame.player.energy=20.0f;
    riskGame.overdriveActive=true;
    events=(GameplayEvents){0};
    HitPlayer(&riskGame,riskGame.player.position,&events);
    boostDamageValid=boostDamageValid && riskGame.gameOver &&
        riskGame.player.energy==0.0f;
    InitGameplay(&riskGame,94u);
    riskGame.overdriveActive=true;
    for(int hit=0;hit<5;hit++) {
        riskGame.player.invulnerability=0.0f;
        events=(GameplayEvents){0};
        HitPlayer(&riskGame,riskGame.player.position,&events);
        boostDamageValid=boostDamageValid && events.playerHit &&
            riskGame.gameOver==(hit==4);
    }

    GameplaySystem guidanceGame;
    InitGameplay(&guidanceGame,94u);
    guidanceGame.player.position.x=6.0f;
    EnemyProjectile testShot={.position={0.0f,2.4f,-20.0f},
        .velocity={0.0f,0.0f,20.0f},.life=3.0f,.active=true};
    guidanceGame.projectiles[0]=testShot;
    events=(GameplayEvents){0};
    for(int step=0;step<6;step++) UpdateProjectiles(&guidanceGame,0.05f,&events);
    float straightX=guidanceGame.projectiles[0].position.x;
    guidanceGame.projectiles[0]=testShot;
    guidanceGame.overdriveActive=true;
    for(int step=0;step<6;step++) UpdateProjectiles(&guidanceGame,0.05f,&events);
    bool homingValid=guidanceGame.projectiles[0].homing &&
        guidanceGame.projectiles[0].position.x>straightX+0.8f;
    guidanceGame.overdriveActive=false;
    float priorX=guidanceGame.projectiles[0].position.x;
    UpdateProjectiles(&guidanceGame,0.05f,&events);
    homingValid=homingValid && guidanceGame.projectiles[0].homing &&
        guidanceGame.projectiles[0].position.x>priorX;
    guidanceGame.projectiles[1]=(EnemyProjectile){.position={0.0f,2.4f,-2.0f},
        .velocity={0.0f,0.0f,20.0f},.life=1.0f,.homing=true,.active=true};
    UpdateProjectiles(&guidanceGame,0.02f,&events);
    bool evadeWindowValid=guidanceGame.projectiles[1].active &&
        guidanceGame.projectiles[1].velocity.x==0.0f;

    GameplaySystem fireGame;
    InitGameplay(&fireGame,94u);
    Enemy normalFire={.active=true,.type=ENEMY_DRIFTER,.phase=ENEMY_HOVER,
        .position={0.0f,2.4f,-25.0f},.basePosition={0.0f,2.4f,-25.0f},
        .fireTimer=0.4f,.identity=7};
    Enemy boostedFire=normalFire;
    for(int step=0;step<5;step++) {
        UpdateEnemy(&fireGame,&normalFire,0.05f,false,&events);
        UpdateEnemy(&fireGame,&boostedFire,0.05f,true,&events);
    }
    bool fireRateValid=normalFire.phase==ENEMY_HOVER &&
        boostedFire.phase==ENEMY_TELEGRAPH;
    normalFire.phase=ENEMY_ATTACK;
    boostedFire.phase=ENEMY_ATTACK;
    normalFire.phaseTime=boostedFire.phaseTime=0.36f;
    UpdateEnemy(&fireGame,&normalFire,0.01f,false,&events);
    UpdateEnemy(&fireGame,&boostedFire,0.01f,true,&events);
    fireRateValid=fireRateValid && normalFire.phase==ENEMY_HOVER &&
        boostedFire.phase==ENEMY_HOVER &&
        boostedFire.fireTimer<normalFire.fireTimer*0.65f;

    GameplaySystem sweepGame;
    InitGameplay(&sweepGame,94u);
    sweepGame.projectiles[0]=(EnemyProjectile){.position={0.0f,2.4f,-1.4f},
        .velocity={0.0f,0.0f,60.0f},.life=1.0f,.active=true};
    events=(GameplayEvents){0};
    UpdateProjectiles(&sweepGame,0.05f,&events);
    bool sweepValid=events.playerHit && !sweepGame.projectiles[0].active &&
        fabsf(sweepGame.player.energy-82.0f)<0.001f;

    bool valid = insufficientFunds && beamValid && loopPersistence && hitPersistence &&
                 bombValid && bossCapValid && capValid && speedValid && roundAdvanceValid &&
                 leviathanSpawnValid && singularityValid && recursionAdvanceValid &&
                 ordinaryDamageValid && boostDamageValid && homingValid &&
                 evadeWindowValid && fireRateValid && sweepValid && dodgeValid;
    TraceLog(valid ? LOG_INFO : LOG_ERROR,
             "GAMEPLAY: funds=%s beam=%s loop=%s hit=%s bomb=%s boss-cap=%s link-cap=%s speed=%s round=%s leviathan=%s singularity=%s recurse=%s normal-hit=%s boost-hit=%s homing=%s evade=%s fire-rate=%s sweep=%s dodge=%s",
             insufficientFunds ? "yes" : "no", beamValid ? "yes" : "no",
             loopPersistence ? "yes" : "no", hitPersistence ? "yes" : "no",
             bombValid ? "yes" : "no", bossCapValid ? "yes" : "no",
             capValid ? "yes" : "no", speedValid ? "yes" : "no",
             roundAdvanceValid ? "yes" : "no", leviathanSpawnValid ? "yes" : "no",
             singularityValid ? "yes" : "no", recursionAdvanceValid ? "yes" : "no",
             ordinaryDamageValid ? "yes" : "no",boostDamageValid ? "yes" : "no",
             homingValid ? "yes" : "no",evadeWindowValid ? "yes" : "no",
             fireRateValid ? "yes" : "no",sweepValid ? "yes" : "no",dodgeValid ? "yes" : "no");
    return valid;
}

bool CanGameplayBoost(const GameplaySystem *game) {
    return !game->gameOver && game->loopTransitionTimer <= 0.0f &&
           !game->boss.active && game->player.energy > 1.0f;
}

float GetGameplayCruiseSpeed(const GameplaySystem *game, double virtualPlayerZ) {
    float progress = ClampFloat((float)(fmax(virtualPlayerZ, 0.0) / 1800.0), 0.0f, 1.0f);
    return 18.0f + progress * 12.0f + fminf((float)game->boss.encounterIndex * 2.0f, 6.0f);
}

float GetGameplayTravelSpeed(const GameplaySystem *game, double virtualPlayerZ,
                             bool boosting) {
    float cruise = GetGameplayCruiseSpeed(game, virtualPlayerZ);
    if (game->boss.active) {
        if (game->boss.phase == BOSS_APPROACH) {
            float slowdown = ClampFloat(game->boss.phaseTime / 0.75f, 0.0f, 1.0f);
            slowdown = slowdown * slowdown * (3.0f - 2.0f * slowdown);
            return cruise + (6.0f - cruise) * slowdown;
        }
        return 6.0f;
    }
    return boosting ? cruise * 1.85f : cruise;
}

GameplayEvents UpdateGameplay(GameplaySystem *game, float dt, double virtualPlayerZ,
                              bool boosting, const GameplayInput *input) {
    GameplayEvents events = { 0 };
    dt = fminf(dt, 0.05f);
    game->runTime += dt;
    UpdateEffects(game, dt);
    if (game->player.invulnerability > 0.0f) {
        game->player.invulnerability = fmaxf(0.0f, game->player.invulnerability - dt);
    }
    if (game->comboTimer > 0.0f) {
        game->comboTimer -= dt;
        if (game->comboTimer <= 0.0f) {
            game->combo = 0;
            game->weaponTier = 0;
        }
    }
    if (game->gameOver) return events;
    if (game->loopTransitionTimer > 0.0f) return events;

    game->overdriveActive = boosting;
    UpdatePlayer(game, dt, boosting, input, &events);
    game->difficulty = ClampFloat(log1pf((float)(fmax(virtualPlayerZ, 0.0) / 350.0)) *
                                  0.43429448f, 0.0f, 1.0f);
    if (game->bossesEnabled) UpdateBoss(game, dt, virtualPlayerZ, &events);
    if (!game->boss.active && !game->boss.clearing) {
        game->spawnTimer -= dt * (boosting ? 1.8f : 1.0f);
        if (TimerOnGrid(game, game->spawnTimer, MUSIC_BEAT)) {
            SpawnFormation(game);
            game->spawnTimer = (1.18f - game->difficulty * 0.68f) *
                               (1.0f - LoopPressure(game) * 0.45f) *
                               (boosting ? 0.72f : 1.0f);
        }
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (game->enemies[i].active) {
            game->enemies[i].hurt = fmaxf(0.0f, game->enemies[i].hurt - dt * 9.0f);
            UpdateEnemy(game, &game->enemies[i], dt, boosting, &events);
        }
    }
    UpdateProjectiles(game, dt, &events);
    UpdatePlayerProjectiles(game, dt, &events);
    UpdateArcs(game, dt, &events);
    UpdateDrills(game, dt, &events);
    UpdateCapsules(game, dt, &events);
    return events;
}

void SetGameplayArena(GameplaySystem *game, GameplayArena arena) {
    game->arena = arena;
}

void SetGameplayMusicClock(GameplaySystem *game, const MusicClock *clock) {
    game->music = *clock;
}
