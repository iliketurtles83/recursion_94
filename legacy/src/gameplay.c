#include "gameplay.h"
#include "palette.h"
#include "rlgl.h"
#include "raymath.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define TAP_THRESHOLD 0.12f
#define GAMEPLAY_LATERAL_SCALE 1.5f
#define PLAYER_MAX_X (10.0f * GAMEPLAY_LATERAL_SCALE)
#define PLAYER_MIN_X (-PLAYER_MAX_X)
#define ENEMY_FORMATION_CENTER_X (7.5f * GAMEPLAY_LATERAL_SCALE)
#define ENEMY_FORMATION_LIMIT_X (9.0f * GAMEPLAY_LATERAL_SCALE)
#define ENEMY_FORMATION_SPACING (3.2f * GAMEPLAY_LATERAL_SCALE)
#define PLAYER_MIN_Y 0.8f
#define PLAYER_MAX_Y 7.0f
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

static void SpawnFormation(GameplaySystem *game) {
    uint32_t seed = game->runSeed ^ (uint32_t)(game->spawnSerial + 1) * 0x9e3779b9u;
    int count = game->boss.encounterIndex > 0 ? 2 : 1;
    if (game->difficulty > 0.08f && Hash01(seed + 1u) > 0.35f) count++;
    if (game->difficulty > 0.42f && Hash01(seed + 2u) > 0.48f) count++;
    if (GetGameplayRecursionDepth(game) >= 3 && Hash01(seed + 5u) > 0.62f) count++;
    if (count > 5) count = 5;

    float centerX = -ENEMY_FORMATION_CENTER_X +
                    Hash01(seed + 3u) * ENEMY_FORMATION_CENTER_X * 2.0f;
    float centerY = 1.6f + Hash01(seed + 4u) * 4.6f;
    for (int i = 0; i < count; i++) {
        float typeRoll = Hash01(seed + 11u + (uint32_t)i * 7u);
        EnemyType type = ENEMY_DRIFTER;
        if (game->difficulty > 0.08f && typeRoll > 0.50f) type = ENEMY_CHASER;
        if (game->difficulty > 0.25f && typeRoll > 0.80f) type = ENEMY_SPLITTER;

        float offset = ((float)i - (float)(count - 1) * 0.5f) * ENEMY_FORMATION_SPACING;
        Vector3 position = {
            ClampFloat(centerX + offset, -ENEMY_FORMATION_LIMIT_X, ENEMY_FORMATION_LIMIT_X),
            ClampFloat(centerY + sinf((float)i * 2.3f) * 1.2f, 1.0f, 6.5f),
            -104.0f - Hash01(seed + 30u + (uint32_t)i) * 28.0f
        };
        SpawnEnemyAt(game, type, position, 0, seed + (uint32_t)i * 31u);
    }
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

static void SpawnBoss(GameplaySystem *game, double virtualPlayerZ, GameplayEvents *events) {
    BossState *boss = &game->boss;
    uint32_t encounterSeed = game->runSeed ^
        (uint32_t)(boss->encounterIndex + 1) * 0x9e3779b9u;

    // Clear the lane so the encounter reads as a deliberate arena rather than
    // another formation stacked on top of regular enemies.
    for (int i = 0; i < MAX_ENEMIES; i++) game->enemies[i].active = false;
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
    boss->position = (Vector3){ 0.0f, 4.1f, boss->leviathan ? -148.0f : -125.0f };
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

static void UpdateBoss(GameplaySystem *game, float dt, double virtualPlayerZ,
                       GameplayEvents *events) {
    BossState *boss = &game->boss;
    if (!boss->active) {
        if (virtualPlayerZ >= boss->nextSpawnDistance) SpawnBoss(game, virtualPlayerZ, events);
        return;
    }

    boss->phaseTime += dt;
    float rotationSpeed = boss->phase == BOSS_SINGULARITY ? 2.85f
                        : boss->phase == BOSS_ENRAGED ? 1.95f
                        : boss->leviathan ? 1.38f : 1.05f;
    boss->rotation += dt * rotationSpeed;
    boss->position.z = Approach(boss->position.z, boss->leviathan ? -39.5f : -46.0f,
                                boss->leviathan ? 0.92f : 1.15f, dt);
    float motionScale = boss->leviathan ? 3.7f : 2.2f;
    boss->position.x = sinf(game->runTime * (boss->leviathan ? 0.25f : 0.31f) +
                            (float)boss->encounterIndex) * motionScale;
    boss->position.y = 4.1f + sinf(game->runTime * (boss->leviathan ? 0.43f : 0.57f)) *
                                  (boss->leviathan ? 1.0f : 0.65f);

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
        float radiusX = boss->leviathan ? 12.4f : 8.35f;
        float radiusY = boss->leviathan ? 7.15f : 4.65f;
        game->enemies[slot].position = (Vector3){
            boss->position.x + cosf(angle) * radiusX,
            boss->position.y + sinf(angle) * radiusY,
            boss->position.z + sinf(angle * 0.5f) * (boss->leviathan ? 3.4f : 2.1f)
        };
        game->enemies[slot].basePosition = game->enemies[slot].position;
    }

    if (boss->phase == BOSS_APPROACH) return;

    boss->reinforcementTimer -= dt;
    if (boss->reinforcementTimer <= 0.0f) {
        int reinforcements = boss->phase == BOSS_SINGULARITY ? 3
                           : (boss->phase == BOSS_ENRAGED || boss->leviathan ? 2 : 1);
        for (int add = 0; add < reinforcements; add++) {
            uint32_t addSeed = game->runSeed ^ (uint32_t)game->spawnSerial * 0x85ebca6bu ^
                               (uint32_t)boss->encounterIndex * 97u;
            EnemyType type = (addSeed & 1u) ? ENEMY_DRIFTER : ENEMY_CHASER;
            float side = ((addSeed >> 3) & 1u) ? 1.0f : -1.0f;
            SpawnEnemyAt(game, type,
                         (Vector3){ boss->position.x + side * (5.0f + (float)add * 3.0f),
                                    boss->position.y - 1.2f + (float)add,
                                    boss->position.z - 24.0f - (float)add * 5.0f },
                         0, addSeed);
        }
        boss->reinforcementTimer = boss->phase == BOSS_SINGULARITY ? 2.25f
                                   : boss->phase == BOSS_ENRAGED ? 2.7f
                                   : boss->leviathan ? 3.1f : 3.8f;
    }

    boss->fireTimer -= dt;
    if (boss->fireTimer > 0.0f) return;

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

    float projectileSpeed = 23.0f + game->difficulty * 9.0f + LoopPressure(game) * 8.0f +
                            (boss->phase == BOSS_ENRAGED ? 4.0f : 0.0f) +
                            (boss->phase == BOSS_SINGULARITY ? 7.0f : 0.0f);
    for (int shot = 0; shot < shotCount; shot++) {
        float centered = (float)shot - (float)(shotCount - 1) * 0.5f;
        Vector3 shotOrigin = origin;
        float spread = centered * (boss->leviathan ? 1.22f : 1.45f);
        if (boss->leviathan && boss->attackPattern % 3 == 1) {
            float angle = boss->rotation + (float)shot * 2.399963f;
            shotOrigin.x += cosf(angle) * 10.5f;
            shotOrigin.y += sinf(angle) * 6.2f;
            spread = sinf(angle * 2.0f) * 2.4f;
        } else if (boss->leviathan && boss->attackPattern % 3 == 2) {
            shotOrigin.x += centered * 2.25f;
            shotOrigin.y += 5.2f + sinf((float)shot * 1.7f) * 1.1f;
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

static void RegisterKill(GameplaySystem *game, Enemy *enemy, bool charged,
                         GameplayEvents *events) {
    Vector3 deathPosition = enemy->position;
    EnemyType deadType = enemy->type;
    int generation = enemy->generation;
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
    game->score += 100 * game->combo * (deadType == ENEMY_SPLITTER ? 2 : 1) *
                   overdriveMultiplier;
    if (!game->suppressLinkRewards) {
        int linkReward = deadType == ENEMY_BOSS_CORE ? 10
                       : deadType == ENEMY_BOSS_NODE ? 5
                       : deadType == ENEMY_SPLITTER ? 2 : 1;
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
                     : (deadType == ENEMY_SPLITTER ? 14 : 8);
    SpawnBurst(game, deathPosition, burstColor, burstCount);

    if (deadType == ENEMY_SPLITTER && generation == 0) {
        for (int side = -1; side <= 1; side += 2) {
            Vector3 childPosition = deathPosition;
            childPosition.x = ClampFloat(childPosition.x + (float)side * 1.4f, -9.0f, 9.0f);
            childPosition.z -= 2.0f;
            Enemy *child = SpawnEnemyAt(game, ENEMY_CHASER, childPosition, 1,
                                        game->runSeed ^ (uint32_t)game->killSerial * 17u ^
                                        (uint32_t)(side + 2));
            if (child != NULL) {
                child->phase = ENEMY_HOVER;
                child->phaseTime = 0.0f;
                child->basePosition = childPosition;
            }
        }
    }
}

static void DamageEnemy(GameplaySystem *game, Enemy *enemy, float damage, bool charged,
                        GameplayEvents *events) {
    if (!enemy->active) return;
    enemy->health -= damage;
    game->cameraKick = fmaxf(game->cameraKick, 0.12f);
    if (enemy->health <= 0.0f) RegisterKill(game, enemy, charged, events);
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
    if (game->player.invulnerability > 0.0f || game->gameOver) return;
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

static void UpdatePlayer(GameplaySystem *game, float dt, bool boosting,
                         GameplayEvents *events) {
    float inputX = 0.0f;
    float inputY = 0.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) inputX -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) inputX += 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) inputY -= 1.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) inputY += 1.0f;
    if (inputX != 0.0f && inputY != 0.0f) {
        inputX *= 0.7071068f;
        inputY *= 0.7071068f;
    }

    float pullX = 0.0f;
    float pullY = 0.0f;
    if (game->boss.active && game->boss.leviathan && game->boss.phase >= BOSS_SHIELDED) {
        float dx = game->boss.position.x - game->player.position.x;
        float dy = game->boss.position.y - game->player.position.y;
        float distance = sqrtf(dx * dx + dy * dy) + 0.001f;
        float gravity = game->boss.phase == BOSS_SINGULARITY ? 5.4f
                        : game->boss.phase == BOSS_ENRAGED ? 3.4f : 2.1f;
        gravity += fminf((float)(GetGameplayRecursionDepth(game) - 1) * 0.16f, 1.8f);
        pullX = dx / distance * gravity;
        pullY = dy / distance * gravity * 0.72f;
    }
    game->player.velocity.x = Approach(game->player.velocity.x, inputX * 14.0f + pullX, 10.0f, dt);
    game->player.velocity.y = Approach(game->player.velocity.y, inputY * 10.0f + pullY, 10.0f, dt);
    game->player.position.x += game->player.velocity.x * dt;
    game->player.position.y += game->player.velocity.y * dt;
    game->player.position.x = ClampFloat(game->player.position.x, PLAYER_MIN_X, PLAYER_MAX_X);
    game->player.position.y = ClampFloat(game->player.position.y, PLAYER_MIN_Y, PLAYER_MAX_Y);
    game->player.bank = Approach(game->player.bank, -inputX * 0.7f, 8.0f, dt);

    if (IsKeyPressed(KEY_K)) FireLinkBeam(game, events);
    if (IsKeyPressed(KEY_L)) FireLinkBomb(game, events);

    if (boosting) game->player.energy = fmaxf(0.0f, game->player.energy - BOOST_ENERGY_DRAIN * dt);
    else game->player.energy = fminf(100.0f, game->player.energy + 1.6f * dt);

    bool fireHeld = IsKeyDown(KEY_J) || IsMouseButtonDown(MOUSE_BUTTON_LEFT);
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

static void UpdateEnemy(GameplaySystem *game, Enemy *enemy, float dt, bool overdrive,
                        GameplayEvents *events) {
    if (enemy->type == ENEMY_BOSS_NODE || enemy->type == ENEMY_BOSS_CORE) return;
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
            if (enemy->phaseTime >= delay) {
                if (enemy->type == ENEMY_CHASER) {
                    float attackSpeed = 32.0f + game->difficulty * 10.0f +
                                        (overdrive ? 6.0f : 0.0f);
                    float travel = fmaxf(0.2f, -enemy->position.z / attackSpeed);
                    enemy->velocity.x = (game->player.position.x - enemy->position.x) / travel;
                    enemy->velocity.y = (game->player.position.y - enemy->position.y) / travel;
                    enemy->velocity.z = attackSpeed;
                } else if (enemy->type == ENEMY_SPLITTER) {
                    float pressure = overdrive ? 4.0f : 0.0f;
                    SpawnProjectile(game, enemy->position, 21.0f + game->difficulty * 6.0f + pressure, -1.6f);
                    SpawnProjectile(game, enemy->position, 23.0f + game->difficulty * 6.0f + pressure, 0.0f);
                    SpawnProjectile(game, enemy->position, 21.0f + game->difficulty * 6.0f + pressure, 1.6f);
                } else {
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
    }

    if (enemy->age > 28.0f) enemy->active = false;
}

static void UpdateProjectiles(GameplaySystem *game, float dt, GameplayEvents *events) {
    for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
        EnemyProjectile *projectile = &game->projectiles[i];
        if (!projectile->active) continue;
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
    ClearLocks(game);

    game->boss.coreSlot = -1;
    for (int node = 0; node < 4; node++) game->boss.nodeSlots[node] = -1;
    game->boss.leviathan = false;
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
    HitPlayer(&game, game.player.position, &events);
    bool hitPersistence = game.chainLinks == 73 && game.combo == 0 && game.weaponTier == 0;

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
                 evadeWindowValid && fireRateValid && sweepValid;
    TraceLog(valid ? LOG_INFO : LOG_ERROR,
             "GAMEPLAY: funds=%s beam=%s loop=%s hit=%s bomb=%s boss-cap=%s link-cap=%s speed=%s round=%s leviathan=%s singularity=%s recurse=%s normal-hit=%s boost-hit=%s homing=%s evade=%s fire-rate=%s sweep=%s",
             insufficientFunds ? "yes" : "no", beamValid ? "yes" : "no",
             loopPersistence ? "yes" : "no", hitPersistence ? "yes" : "no",
             bombValid ? "yes" : "no", bossCapValid ? "yes" : "no",
             capValid ? "yes" : "no", speedValid ? "yes" : "no",
             roundAdvanceValid ? "yes" : "no", leviathanSpawnValid ? "yes" : "no",
             singularityValid ? "yes" : "no", recursionAdvanceValid ? "yes" : "no",
             ordinaryDamageValid ? "yes" : "no",boostDamageValid ? "yes" : "no",
             homingValid ? "yes" : "no",evadeWindowValid ? "yes" : "no",
             fireRateValid ? "yes" : "no",sweepValid ? "yes" : "no");
    return valid;
}

bool CanGameplayBoost(const GameplaySystem *game) {
    return !game->gameOver && game->loopTransitionTimer <= 0.0f &&
           !game->boss.active && game->player.energy > 1.0f;
}

float GetGameplayTravelSpeed(const GameplaySystem *game, double virtualPlayerZ,
                             bool boosting) {
    float progress = ClampFloat((float)(fmax(virtualPlayerZ, 0.0) / 1800.0), 0.0f, 1.0f);
    float cruise = 18.0f + progress * 12.0f +
                   fminf((float)game->boss.encounterIndex * 2.0f, 6.0f);
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
                              bool boosting) {
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
    UpdatePlayer(game, dt, boosting, &events);
    game->difficulty = ClampFloat(log1pf((float)(fmax(virtualPlayerZ, 0.0) / 350.0)) *
                                  0.43429448f, 0.0f, 1.0f);
    UpdateBoss(game, dt, virtualPlayerZ, &events);
    if (!game->boss.active) {
        game->spawnTimer -= dt * (boosting ? 1.8f : 1.0f);
        if (game->spawnTimer <= 0.0f) {
            SpawnFormation(game);
            game->spawnTimer = (1.18f - game->difficulty * 0.68f) *
                               (1.0f - LoopPressure(game) * 0.45f) *
                               (boosting ? 0.72f : 1.0f);
        }
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (game->enemies[i].active) {
            UpdateEnemy(game, &game->enemies[i], dt, boosting, &events);
        }
    }
    UpdateProjectiles(game, dt, &events);
    UpdatePlayerProjectiles(game, dt, &events);
    return events;
}

void UpdateGameplayCamera(const GameplaySystem *game, Camera3D *camera, float dt, bool boosting) {
    float shake = game->cameraKick;
    float shakeX = sinf(game->runTime * 91.0f) * shake * 0.18f;
    float shakeY = cosf(game->runTime * 77.0f) * shake * 0.13f;
    float desiredPositionX = game->player.position.x * 0.32f + shakeX;
    float desiredPositionY = 5.7f + (game->player.position.y - 2.4f) * 0.22f + shakeY;
    float desiredTargetX = game->player.position.x * 0.72f + game->player.velocity.x * 0.035f;
    float desiredTargetY = game->player.position.y * 0.74f + game->player.velocity.y * 0.025f;
    float arrivalPull = 0.0f;
    if (game->boss.active && game->boss.phase == BOSS_APPROACH) {
        float progress = ClampFloat(game->boss.phaseTime / 2.6f, 0.0f, 1.0f);
        arrivalPull = sinf(progress * 3.14159265f);
        desiredPositionY += arrivalPull * 1.15f;
        desiredTargetX = desiredTargetX * (1.0f - arrivalPull * 0.42f) +
                         game->boss.position.x * arrivalPull * 0.42f;
        desiredTargetY = desiredTargetY * (1.0f - arrivalPull * 0.34f) +
                         game->boss.position.y * arrivalPull * 0.34f;
    }
    if (game->loopTransitionTimer > 0.0f) {
        float progress = 1.0f - game->loopTransitionTimer / CORE_TRANSITION_DURATION;
        float collapsePull = sinf(ClampFloat(progress, 0.0f, 1.0f) * 3.14159265f);
        arrivalPull = fmaxf(arrivalPull, collapsePull * 0.78f);
        desiredTargetX = desiredTargetX * (1.0f - collapsePull * 0.72f) +
                         game->boss.position.x * collapsePull * 0.72f;
        desiredTargetY = desiredTargetY * (1.0f - collapsePull * 0.64f) +
                         game->boss.position.y * collapsePull * 0.64f;
    }
    camera->position.x = Approach(camera->position.x, desiredPositionX, 6.0f, dt);
    camera->position.y = Approach(camera->position.y, desiredPositionY, 6.0f, dt);
    camera->position.z = Approach(camera->position.z, 8.0f + arrivalPull * 1.7f, 6.0f, dt);
    camera->target.x = Approach(camera->target.x, desiredTargetX, 7.0f, dt);
    camera->target.y = Approach(camera->target.y, desiredTargetY, 7.0f, dt);
    camera->target.z = -22.0f - arrivalPull * 10.0f;
    float targetFov = (boosting ? 77.0f : 72.0f) + arrivalPull * 7.0f;
    camera->fovy = Approach(camera->fovy, targetFov, 4.0f, dt);
}

static Color MixColor(Color a, Color b, float amount, unsigned char alpha) {
    amount = ClampFloat(amount, 0.0f, 1.0f);
    return (Color){
        (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * amount),
        (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * amount),
        (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * amount), alpha
    };
}

// Each type gets its own hue so silhouettes read apart at a glance instead
// of Chaser/boss and Drifter/Splitter/node collapsing onto the same colors.
static Color EnemyColor(const GameplaySystem *game, const Enemy *enemy) {
    switch (enemy->type) {
        case ENEMY_DRIFTER: return game->primaryColor;
        case ENEMY_CHASER: return game->threatColor;
        case ENEMY_SPLITTER: return game->secondaryColor;
        case ENEMY_BOSS_NODE: return MixColor(game->threatColor, game->secondaryColor, 0.5f, 255);
        case ENEMY_BOSS_CORE: return MixColor(game->threatColor, game->hotColor, 0.45f, 255);
        default: return game->threatColor;
    }
}

static Color ScaleColor(Color color, float scale, int lift) {
    int r = (int)((float)color.r * scale) + lift;
    int g = (int)((float)color.g * scale) + lift;
    int b = (int)((float)color.b * scale) + lift;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, color.a };
}

static void DrawLitTriangle3D(Vector3 a, Vector3 b, Vector3 c, Color color) {
    Vector3 ab = {b.x-a.x,b.y-a.y,b.z-a.z};
    Vector3 ac = {c.x-a.x,c.y-a.y,c.z-a.z};
    Vector3 n = {ab.y*ac.z-ab.z*ac.y,ab.z*ac.x-ab.x*ac.z,ab.x*ac.y-ab.y*ac.x};
    float length = sqrtf(n.x*n.x+n.y*n.y+n.z*n.z);
    if (length < 0.000001f) return;
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r,color.g,color.b,color.a);
    rlNormal3f(n.x/length,n.y/length,n.z/length);
    rlVertex3f(a.x,a.y,a.z); rlVertex3f(b.x,b.y,b.z); rlVertex3f(c.x,c.y,c.z);
    rlEnd();
}

static void DrawQuad3D(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color) {
    DrawLitTriangle3D(a, b, c, color);
    DrawLitTriangle3D(a, c, d, color);
}

// Lit procedural cylinder; the stock immediate cylinder does not supply normals.
static void DrawLitCylinderEx(Vector3 a, Vector3 b, float ra, float rb,
                              int sides, Color color) {
    Vector3 axis={b.x-a.x,b.y-a.y,b.z-a.z};
    float length=sqrtf(axis.x*axis.x+axis.y*axis.y+axis.z*axis.z);
    if(length<0.00001f) return;
    axis=(Vector3){axis.x/length,axis.y/length,axis.z/length};
    Vector3 u=fabsf(axis.y)<0.9f?(Vector3){-axis.z,0,axis.x}:(Vector3){0,axis.z,-axis.y};
    float ul=sqrtf(u.x*u.x+u.y*u.y+u.z*u.z);
    u=(Vector3){u.x/ul,u.y/ul,u.z/ul};
    Vector3 v={axis.y*u.z-axis.z*u.y,axis.z*u.x-axis.x*u.z,axis.x*u.y-axis.y*u.x};
    float slope=(ra-rb)/length;
    for(int i=0;i<sides;i++) {
        float t0=(float)i*6.2831853f/(float)sides;
        float t1=(float)(i+1)*6.2831853f/(float)sides;
        Vector3 r0={u.x*cosf(t0)+v.x*sinf(t0),u.y*cosf(t0)+v.y*sinf(t0),u.z*cosf(t0)+v.z*sinf(t0)};
        Vector3 r1={u.x*cosf(t1)+v.x*sinf(t1),u.y*cosf(t1)+v.y*sinf(t1),u.z*cosf(t1)+v.z*sinf(t1)};
        Vector3 p[4]={{a.x+r0.x*ra,a.y+r0.y*ra,a.z+r0.z*ra},
                      {a.x+r1.x*ra,a.y+r1.y*ra,a.z+r1.z*ra},
                      {b.x+r0.x*rb,b.y+r0.y*rb,b.z+r0.z*rb},
                      {b.x+r1.x*rb,b.y+r1.y*rb,b.z+r1.z*rb}};
        const int order[6]={0,1,2,1,3,2};
        rlBegin(RL_TRIANGLES);
        rlColor4ub(color.r,color.g,color.b,color.a);
        for(int k=0;k<6;k++) {
            int j=order[k];
            Vector3 r=(j&1)?r1:r0;
            float nl=sqrtf(1.0f+slope*slope);
            rlNormal3f((r.x+axis.x*slope)/nl,(r.y+axis.y*slope)/nl,(r.z+axis.z*slope)/nl);
            rlVertex3f(p[j].x,p[j].y,p[j].z);
        }
        rlEnd();
        if(ra>0.0f) DrawLitTriangle3D(a,p[1],p[0],color);
        if(rb>0.0f) DrawLitTriangle3D(b,p[2],p[3],color);
    }
}
#define DrawCylinderEx DrawLitCylinderEx

static void DrawEnergyHalo(Vector3 p,float radius,Color color) {
    // Fixed triangle fan with a radial alpha gradient; no sprite texture.
    rlBegin(RL_TRIANGLES);
    rlNormal3f(0,0,1);
    for(int i=0;i<24;i++) {
        float a=(float)i*6.2831853f/24.0f,b=(float)(i+1)*6.2831853f/24.0f;
        rlColor4ub(color.r,color.g,color.b,color.a);
        rlVertex3f(p.x,p.y,p.z);
        rlColor4ub(color.r,color.g,color.b,0);
        rlVertex3f(p.x+cosf(a)*radius,p.y+sinf(a)*radius,p.z);
        rlVertex3f(p.x+cosf(b)*radius,p.y+sinf(b)*radius,p.z);
    }
    rlEnd();
}

static void DrawThickTriangle3D(Vector3 a, Vector3 b, Vector3 c, float thickness,
                                Color top, Color edge) {
    float half = thickness * 0.5f;
    Vector3 at = { a.x, a.y + half, a.z };
    Vector3 bt = { b.x, b.y + half, b.z };
    Vector3 ct = { c.x, c.y + half, c.z };
    Vector3 ab = { a.x, a.y - half, a.z };
    Vector3 bb = { b.x, b.y - half, b.z };
    Vector3 cb = { c.x, c.y - half, c.z };
    DrawLitTriangle3D(at, bt, ct, top);
    DrawLitTriangle3D(ab, cb, bb, edge);
    DrawQuad3D(at, ab, bb, bt, edge);
    DrawQuad3D(bt, bb, cb, ct, edge);
    DrawQuad3D(ct, cb, ab, at, edge);
}

// Extrudes a four-sided plate along Z. Boss armor uses these broad surfaces so
// its detail reads as manufactured mass and shadow instead of additional glow.
static void DrawThickQuadZ(Vector3 a, Vector3 b, Vector3 c, Vector3 d,
                           float thickness, Color front, Color edge) {
    float half = thickness * 0.5f;
    Vector3 af = { a.x, a.y, a.z + half };
    Vector3 bf = { b.x, b.y, b.z + half };
    Vector3 cf = { c.x, c.y, c.z + half };
    Vector3 df = { d.x, d.y, d.z + half };
    Vector3 ab = { a.x, a.y, a.z - half };
    Vector3 bb = { b.x, b.y, b.z - half };
    Vector3 cb = { c.x, c.y, c.z - half };
    Vector3 db = { d.x, d.y, d.z - half };
    DrawQuad3D(af, bf, cf, df, front);
    DrawQuad3D(ab, db, cb, bb, edge);
    DrawQuad3D(af, ab, bb, bf, edge);
    DrawQuad3D(bf, bb, cb, cf, edge);
    DrawQuad3D(cf, cb, db, df, edge);
    DrawQuad3D(df, db, ab, af, edge);
}

static void DrawSegmentedRingXY(Vector3 center, float radius, int segments,
                                float rotation, float gap, float thickness, Color color) {
    float step = 6.2831853f / (float)segments;
    for (int segment = 0; segment < segments; segment++) {
        float a0 = rotation + (float)segment * step + gap;
        float a1 = rotation + (float)(segment + 1) * step - gap;
        Vector3 a = { center.x + cosf(a0) * radius,
                      center.y + sinf(a0) * radius, center.z };
        Vector3 b = { center.x + cosf(a1) * radius,
                      center.y + sinf(a1) * radius, center.z };
        DrawCylinderEx(a, b, thickness, thickness, 6, color);
    }
}

// True open annulus: unlike a capped cylinder this leaves the machinery behind
// the aperture visible, including inner walls and a stepped front bevel.
static void DrawRingHousing(Vector3 p,float outer,float inner,float depth,Color face,Color edge) {
    for(int i=0;i<24;i++) {
        float a=(float)i*6.2831853f/24.0f,b=(float)(i+1)*6.2831853f/24.0f;
        Vector3 o0={p.x+cosf(a)*outer,p.y+sinf(a)*outer,p.z};
        Vector3 o1={p.x+cosf(b)*outer,p.y+sinf(b)*outer,p.z};
        Vector3 i0={p.x+cosf(a)*inner,p.y+sinf(a)*inner,p.z};
        Vector3 i1={p.x+cosf(b)*inner,p.y+sinf(b)*inner,p.z};
        Vector3 ob0=o0,ob1=o1,ib0=i0,ib1=i1;
        ob0.z-=depth; ob1.z-=depth; ib0.z-=depth; ib1.z-=depth;
        DrawQuad3D(o0,o1,i1,i0,face);
        DrawQuad3D(ob1,ob0,ib0,ib1,edge);
        DrawQuad3D(o1,o0,ob0,ob1,edge);
        DrawQuad3D(i0,i1,ib1,ib0,edge);
    }
}

static void DrawMachineryHousing(Vector3 p,Vector3 size,Color face,Color edge) {
    const Vector2 outline[8]={{-1,-0.7f},{-0.7f,-1},{0.7f,-1},{1,-0.7f},
                              {1,0.7f},{0.7f,1},{-0.7f,1},{-1,0.7f}};
    Vector3 crown={p.x,p.y+size.y*0.5f,p.z};
    for(int i=0;i<8;i++) {
        int j=(i+1)&7;
        Vector3 a={p.x+outline[i].x*size.x*0.5f,p.y+size.y*0.25f,p.z+outline[i].y*size.z*0.5f};
        Vector3 b={p.x+outline[j].x*size.x*0.5f,p.y+size.y*0.25f,p.z+outline[j].y*size.z*0.5f};
        Vector3 at={p.x+(a.x-p.x)*0.90f,crown.y,p.z+(a.z-p.z)*0.90f};
        Vector3 bt={p.x+(b.x-p.x)*0.90f,crown.y,p.z+(b.z-p.z)*0.90f};
        Vector3 ab=a,bb=b; ab.y=p.y-size.y*0.5f; bb.y=ab.y;
        DrawLitTriangle3D(crown,bt,at,face);
        DrawQuad3D(at,bt,b,a,ScaleColor(face,0.72f,2));
        DrawQuad3D(a,b,bb,ab,edge);
    }
}

static void DrawPlayerShip(const GameplaySystem *game) {
    Vector3 p = game->player.position;
    Color normalHull = MixColor((Color){ 118, 137, 156, 255 }, game->primaryColor, 0.12f, 255);
    Color body = game->player.invulnerability > 0.0f && ((int)(game->runTime * 18.0f) & 1)
        ? (Color){ 180, 225, 240, 255 }
        : normalHull;
    Color hullDark = MixColor(game->primaryColor, (Color){ 6, 11, 25, 255 }, 0.86f, 255);
    Color hullEdge = MixColor((Color){ 40, 53, 69, 255 }, game->primaryColor, 0.14f, 255);
    Color canopy = MixColor((Color){ 4, 13, 26, 255 }, game->primaryColor, 0.06f, 255);
    Color panelMetal = MixColor(game->primaryColor, (Color){ 31, 38, 47, 255 }, 0.80f, 255);
    float bankLift = game->player.bank * 0.38f;

    // Continuous, faceted airframe: tapered stern, wide shoulders and a fine
    // nose. Rings replace overlapping capped cones and their exposed end disks.
    const float hullZ[4] = {-1.82f, -0.58f, 0.30f, 0.83f};
    const float hullW[4] = {0.025f, 0.30f, 0.40f, 0.22f};
    const float hullH[4] = {0.02f, 0.25f, 0.23f, 0.14f};
    Vector3 hull[4][8];
    for (int ring=0;ring<4;ring++) {
        for (int side=0;side<8;side++) {
            float angle=(float)side*0.78539816f;
            hull[ring][side]=(Vector3){p.x+cosf(angle)*hullW[ring],
                p.y+sinf(angle)*hullH[ring],p.z+hullZ[ring]};
        }
    }
    for (int ring=0;ring<3;ring++) {
        for (int side=0;side<8;side++) {
            int next=(side+1)&7;
            Color plate=side<4?body:hullDark;
            DrawQuad3D(hull[ring][side],hull[ring][next],
                       hull[ring+1][next],hull[ring+1][side],plate);
        }
    }
    for (int side=0;side<8;side++) {
        DrawLitTriangle3D((Vector3){p.x,p.y,p.z+0.83f},
                           hull[3][side],hull[3][(side+1)&7],hullDark);
    }

    Vector3 nose = { p.x, p.y - 0.01f, p.z - 1.52f };
    Vector3 leftTip = { p.x - 1.86f, p.y - bankLift, p.z + 0.48f };
    Vector3 leftRoot = { p.x - 0.24f, p.y, p.z + 0.38f };
    Vector3 rightRoot = { p.x + 0.24f, p.y, p.z + 0.38f };
    Vector3 rightTip = { p.x + 1.86f, p.y + bankLift, p.z + 0.48f };
    DrawThickTriangle3D(nose, leftTip, leftRoot, 0.13f, body, hullDark);
    DrawThickTriangle3D(nose, rightRoot, rightTip, 0.13f, body, hullDark);

    // Inset wing panels and continuous leading-edge light strips improve the
    // material read without turning the entire vessel into a white glow.
    DrawLitTriangle3D((Vector3){ p.x - 0.18f, p.y + 0.075f, p.z - 0.70f },
                   (Vector3){ p.x - 1.43f, p.y - bankLift * 0.78f + 0.075f, p.z + 0.34f },
                   (Vector3){ p.x - 0.37f, p.y + 0.075f, p.z + 0.23f }, hullEdge);
    DrawLitTriangle3D((Vector3){ p.x + 0.18f, p.y + 0.075f, p.z - 0.70f },
                   (Vector3){ p.x + 0.37f, p.y + 0.075f, p.z + 0.23f },
                   (Vector3){ p.x + 1.43f, p.y + bankLift * 0.78f + 0.075f, p.z + 0.34f }, hullEdge);
    DrawThickTriangle3D((Vector3){ p.x, p.y + 0.08f, p.z - 1.48f },
                        (Vector3){ p.x - 0.34f, p.y + 0.10f, p.z + 0.37f },
                        (Vector3){ p.x + 0.34f, p.y + 0.10f, p.z + 0.37f },
                        0.075f, ScaleColor(body, 0.78f, 2), hullDark);
    DrawLine3D(nose, leftTip, game->primaryColor);
    DrawLine3D(nose, rightTip, game->primaryColor);
    DrawLine3D(leftTip, leftRoot, game->secondaryColor);
    DrawLine3D(rightTip, rightRoot, game->secondaryColor);
    DrawSphereEx(leftTip, 0.075f, 5, 8, game->hotColor);
    DrawSphereEx(rightTip, 0.075f, 5, 8, game->hotColor);

    // Elongated smoked-glass canopy seated in an armored frame.
    rlPushMatrix();
    rlTranslatef(p.x,p.y+0.24f,p.z-0.45f);
    rlScalef(0.72f,0.55f,1.6f);
    DrawSphereEx((Vector3){0,0,0},0.31f,10,16,canopy);
    rlPopMatrix();
    DrawLine3D((Vector3){p.x,p.y+0.415f,p.z-0.42f},
               (Vector3){p.x,p.y+0.25f,p.z-0.94f},ScaleColor(game->primaryColor,0.55f,10));

    // Fine armor panels, canards and a nose sensor add readable scale when the
    // vessel fills more of the frame during camera movement.
    DrawQuad3D((Vector3){ p.x - 0.30f, p.y + 0.135f, p.z - 0.94f },
               (Vector3){ p.x - 0.40f, p.y + 0.14f, p.z + 0.18f },
               (Vector3){ p.x - 0.18f, p.y + 0.15f, p.z + 0.28f },
               (Vector3){ p.x - 0.10f, p.y + 0.145f, p.z - 1.06f }, hullEdge);
    DrawQuad3D((Vector3){ p.x + 0.10f, p.y + 0.145f, p.z - 1.06f },
               (Vector3){ p.x + 0.18f, p.y + 0.15f, p.z + 0.28f },
               (Vector3){ p.x + 0.40f, p.y + 0.14f, p.z + 0.18f },
               (Vector3){ p.x + 0.30f, p.y + 0.135f, p.z - 0.94f }, hullEdge);
    for (int side = -1; side <= 1; side += 2) {
        DrawThickTriangle3D((Vector3){ p.x + (float)side * 0.24f, p.y, p.z - 1.16f },
                            (Vector3){ p.x + (float)side * 0.88f, p.y, p.z - 0.72f },
                            (Vector3){ p.x + (float)side * 0.28f, p.y, p.z - 0.57f },
                            0.075f, hullEdge, hullDark);
    }
    DrawSphereEx((Vector3){ p.x, p.y, p.z - 1.73f }, 0.075f, 5, 8,
                 game->primaryColor);

    // Avionics rails, intake shoulders and trailing control surfaces add
    // small-scale construction detail to the player's foreground silhouette.
    for (int side = -1; side <= 1; side += 2) {
        float sideF = (float)side;
        DrawCylinderEx((Vector3){ p.x + sideF * 0.28f, p.y + 0.135f, p.z - 0.92f },
                       (Vector3){ p.x + sideF * 0.34f, p.y + 0.135f, p.z + 0.36f },
                       0.045f, 0.065f, 7, panelMetal);
        DrawCylinderEx((Vector3){ p.x + sideF * 0.67f, p.y - 0.025f, p.z + 0.44f },
                       (Vector3){ p.x + sideF * 0.61f, p.y - 0.015f, p.z - 0.38f },
                       0.17f, 0.10f, 10, panelMetal);
        DrawThickTriangle3D((Vector3){ p.x + sideF * 0.62f, p.y - 0.01f, p.z + 0.42f },
                            (Vector3){ p.x + sideF * 1.48f,
                                       p.y + sideF * bankLift * 0.82f, p.z + 0.50f },
                            (Vector3){ p.x + sideF * 1.08f,
                                       p.y + sideF * bankLift * 0.62f, p.z + 0.82f },
                            0.055f, panelMetal, hullDark);
        DrawSphereEx((Vector3){ p.x + sideF * 0.43f, p.y + 0.13f, p.z - 0.32f },
                     0.048f, 4, 6, hullDark);
        DrawSphereEx((Vector3){ p.x + sideF * 0.45f, p.y + 0.13f, p.z - 0.06f },
                     0.048f, 4, 6, hullDark);
    }
    DrawCylinderEx((Vector3){ p.x, p.y - 0.20f, p.z + 0.34f },
                   (Vector3){ p.x, p.y - 0.18f, p.z - 1.24f },
                   0.09f, 0.045f, 8, panelMetal);

    // Non-emissive hardware: recessed cooling cassettes, raised louvers and
    // countersunk wing fasteners. These catch the same key light as the hull.
    for (int side=-1;side<=1;side+=2) {
        float bank=(float)side*bankLift;
        Vector3 intake={p.x+(float)side*0.84f,p.y+bank*0.45f+0.18f,p.z+0.01f};
        DrawMachineryHousing(intake,(Vector3){0.42f,0.24f,0.80f},hullEdge,hullDark);
        float mouthZ=intake.z-0.403f;
        DrawQuad3D((Vector3){intake.x-0.14f,intake.y-0.065f,mouthZ},
                   (Vector3){intake.x-0.14f,intake.y+0.045f,mouthZ},
                   (Vector3){intake.x+0.14f,intake.y+0.045f,mouthZ},
                   (Vector3){intake.x+0.14f,intake.y-0.065f,mouthZ},(Color){5,8,12,255});
        for(int fin=0;fin<5;fin++) {
            float fz=intake.z-0.22f+(float)fin*0.11f;
            DrawCylinderEx((Vector3){intake.x-0.16f,intake.y+0.13f,fz},
                           (Vector3){intake.x+0.16f,intake.y+0.13f,fz},0.012f,0.012f,5,panelMetal);
        }
        Vector3 plate={p.x+(float)side*1.27f,p.y+bank*0.68f+0.12f,p.z+0.29f};
        DrawMachineryHousing(plate,(Vector3){0.29f,0.075f,0.25f},body,hullDark);
        Vector3 cableA={p.x+(float)side*0.38f,p.y+0.14f,p.z+0.40f};
        Vector3 cableB={p.x+(float)side*0.58f,p.y+0.13f,p.z+0.63f};
        Vector3 cableC={intake.x,intake.y-0.03f,intake.z+0.34f};
        DrawCylinderEx(cableA,cableB,0.026f,0.026f,6,(Color){76,69,58,255});
        DrawCylinderEx(cableB,cableC,0.026f,0.026f,6,(Color){76,69,58,255});
        float x=p.x+(float)side*0.265f;
        float y=p.y+0.255f;
        float z=p.z+0.06f;
        DrawQuad3D((Vector3){x-0.095f,y,z-0.22f},(Vector3){x-0.095f,y,z+0.22f},
                   (Vector3){x+0.095f,y,z+0.22f},(Vector3){x+0.095f,y,z-0.22f},hullDark);
        for (int louver=0;louver<7;louver++) {
            float v=z-0.18f+(float)louver*0.057f;
            DrawQuad3D((Vector3){x-0.080f,y+0.009f,v},(Vector3){x-0.080f,y+0.018f,v+0.025f},
                       (Vector3){x+0.080f,y+0.018f,v+0.025f},(Vector3){x+0.080f,y+0.009f,v},panelMetal);
        }
        for (int fastener=0;fastener<4;fastener++) {
            float offset=0.54f+(float)fastener*0.28f;
            Vector3 screw={p.x+(float)side*offset,
                           p.y+(float)side*bankLift*offset/1.86f+0.085f,
                           p.z+0.23f+(float)fastener*0.023f};
            DrawCylinderEx(screw,(Vector3){screw.x,screw.y+0.009f,screw.z},
                           0.027f,0.027f,8,panelMetal);
            DrawLine3D((Vector3){screw.x-0.017f,screw.y+0.010f,screw.z},
                       (Vector3){screw.x+0.017f,screw.y+0.010f,screw.z},hullDark);
        }
    }

    DrawThickTriangle3D((Vector3){ p.x, p.y + 0.05f, p.z + 0.66f },
                        (Vector3){ p.x, p.y + 0.78f, p.z + 0.52f },
                        (Vector3){ p.x, p.y + 0.07f, p.z - 0.18f },
                        0.09f, body, hullDark);
    DrawThickTriangle3D((Vector3){ p.x - 0.70f, p.y - 0.02f, p.z + 0.62f },
                        (Vector3){ p.x - 1.20f, p.y - bankLift * 0.64f, p.z + 0.92f },
                        (Vector3){ p.x - 0.48f, p.y, p.z + 0.18f },
                        0.10f, hullEdge, hullDark);
    DrawThickTriangle3D((Vector3){ p.x + 0.70f, p.y - 0.02f, p.z + 0.62f },
                        (Vector3){ p.x + 0.48f, p.y, p.z + 0.18f },
                        (Vector3){ p.x + 1.20f, p.y + bankLift * 0.64f, p.z + 0.92f },
                        0.10f, hullEdge, hullDark);

    for (int engine = -1; engine <= 1; engine += 2) {
        float engineX = p.x + (float)engine * 0.52f;
        DrawCylinderEx((Vector3){ engineX, p.y - 0.035f, p.z + 0.82f },
                       (Vector3){ engineX - (float)engine * 0.06f, p.y, p.z - 0.30f },
                       0.22f, 0.12f, 12, body);
        DrawCylinderEx((Vector3){engineX,p.y-0.035f,p.z+0.83f},
                       (Vector3){engineX,p.y-0.035f,p.z+0.92f},
                       0.19f,0.16f,12,hullDark);
        for (int collar=0;collar<3;collar++) {
            float z=p.z+0.50f+(float)collar*0.11f;
            DrawCylinderEx((Vector3){engineX,p.y-0.035f,z},
                           (Vector3){engineX,p.y-0.035f,z+0.023f},
                           0.207f,0.210f,12,panelMetal);
        }
        DrawCircle3D((Vector3){ engineX, p.y - 0.035f, p.z + 0.82f },
                     0.235f, (Vector3){ 1.0f, 0.0f, 0.0f }, 90.0f,
                     game->primaryColor);
        DrawSphereEx((Vector3){ engineX + (float)engine * 0.16f, p.y - 0.05f,
                                p.z + 0.48f },
                     0.055f, 4, 6, game->secondaryColor);
    }
    if (game->weaponTier >= 1) {
        for (int side = -1; side <= 1; side += 2) {
            float x = p.x + (float)side * 1.16f;
            float y = p.y + (float)side * bankLift * 0.68f;
            DrawCylinderEx((Vector3){ x, y, p.z + 0.20f },
                           (Vector3){ x, y, p.z - 0.60f },
                           0.12f, 0.055f, 8, panelMetal);
            DrawCylinderEx((Vector3){x,y,p.z-0.50f},(Vector3){x,y,p.z-0.57f},
                           0.069f,0.065f,8,hullDark);
        }
    }
    if (game->weaponTier >= 2) {
        DrawCylinderEx((Vector3){ p.x - 0.62f, p.y, p.z - 0.15f },
                       (Vector3){ p.x - 0.62f, p.y, p.z - 0.92f },
                       0.10f, 0.06f, 6, panelMetal);
        DrawCylinderEx((Vector3){ p.x + 0.62f, p.y, p.z - 0.15f },
                       (Vector3){ p.x + 0.62f, p.y, p.z - 0.92f },
                       0.10f, 0.06f, 6, panelMetal);
    }
}

static void DrawPlayerExhaust(const GameplaySystem *game) {
    Vector3 p=game->player.position;
    float length=(game->overdriveActive?2.65f:1.15f)*
                 (0.92f+0.08f*sinf(game->runTime*31.0f));
    Color plume=MixColor(game->primaryColor,game->secondaryColor,0.22f,95);
    for(int engine=-1;engine<=1;engine+=2) {
        Vector3 nozzle={p.x+(float)engine*0.52f,p.y-0.035f,p.z+0.94f};
        DrawEnergyHalo(nozzle,0.58f,plume);
        DrawCylinderEx(nozzle,(Vector3){nozzle.x,nozzle.y,nozzle.z+length},
                       0.145f,0.008f,10,plume);
        DrawCylinderEx(nozzle,(Vector3){nozzle.x,nozzle.y,nozzle.z+length*0.66f},
                       0.06f,0.006f,8,(Color){185,230,255,200});
        DrawEnergyHalo(nozzle,0.17f,(Color){220,245,255,255});
        for(int diamond=0;diamond<4;diamond++) {
            float t=(float)(diamond+1)/5.0f;
            DrawSphereEx((Vector3){nozzle.x,nozzle.y,nozzle.z+t*length},
                         0.045f*(1.0f-t)+0.018f,4,6,
                         (Color){200,245,255,(unsigned char)(180.0f*(1.0f-t))});
        }
    }
}

// Each round has its own primary silhouette; the four shield nodes and core
// remain the existing gameplay targets. These are render-only assemblies.
static void DrawBastionBoss(const GameplaySystem *game, Vector3 p, float time) {
    Color hull={27,34,43,255}, edge={9,14,21,255}, plate={65,76,84,255};
    Color trim={99,105,99,255};
    DrawCubeV((Vector3){p.x,p.y,p.z-0.65f},(Vector3){9.8f,5.5f,3.1f},hull);
    DrawCubeV((Vector3){p.x,p.y+3.08f,p.z+0.45f},(Vector3){14.8f,0.90f,2.15f},plate);
    DrawCubeV((Vector3){p.x,p.y-3.08f,p.z+0.45f},(Vector3){13.7f,0.75f,1.9f},edge);
    for(int side=-1;side<=1;side+=2) {
        float x=p.x+(float)side*5.75f;
        DrawCubeV((Vector3){x,p.y,p.z+0.25f},(Vector3){2.65f,8.0f,3.4f},hull);
        DrawCubeV((Vector3){x,p.y+3.25f,p.z+1.45f},(Vector3){2.90f,0.33f,0.62f},trim);
        DrawCubeV((Vector3){x,p.y-2.72f,p.z+1.80f},(Vector3){1.75f,1.30f,0.34f},edge);
        for(int vent=0;vent<6;vent++)
            DrawCubeV((Vector3){x,p.y-3.12f+vent*0.18f,p.z+2.02f},
                      (Vector3){1.50f,0.055f,0.10f},plate);
        DrawCylinderEx((Vector3){x,p.y+1.75f,p.z+1.55f},
                       (Vector3){x,p.y+1.75f,p.z+3.10f},0.38f,0.22f,10,trim);
        DrawCubeV((Vector3){p.x+(float)side*3.35f,p.y,p.z+1.68f},
                  (Vector3){0.38f,5.12f,0.52f},plate);
    }
    DrawCubeV((Vector3){p.x,p.y,p.z+1.32f},(Vector3){4.65f,3.20f,0.68f},edge);
    DrawCubeV((Vector3){p.x,p.y,p.z+1.74f},(Vector3){3.36f,2.16f,0.22f},trim);
    DrawCubeV((Vector3){p.x,p.y,p.z+1.91f},(Vector3){2.34f,1.18f,0.18f},edge);
    for(int latch=-1;latch<=1;latch+=2)
        DrawCubeV((Vector3){p.x+(float)latch*1.55f,p.y,p.z+1.94f},
                  (Vector3){0.16f,1.70f,0.20f},plate);
    DrawSphereEx((Vector3){p.x,p.y,p.z+2.12f},0.34f+0.035f*sinf(time*4.0f),8,12,
                 game->hotColor);
}

static void DrawPrismBoss(const GameplaySystem *game, Vector3 p, float time) {
    Color hull={20,29,39,255}, edge={7,13,21,255}, facet={71,84,97,255};
    Color ridge={112,119,119,255};
    DrawCylinderEx((Vector3){p.x,p.y,p.z-2.0f},
                   (Vector3){p.x,p.y,p.z+1.45f},2.35f,1.75f,3,hull);
    for(int wing=0;wing<3;wing++) {
        float a=-1.5707963f+(float)wing*2.0943951f+0.10f*sinf(time*0.34f);
        float ca=cosf(a), sa=sinf(a), ta=-sa, tb=ca;
        Vector3 a0={p.x+ca*1.15f+ta*1.10f,p.y+sa*1.15f+tb*1.10f,p.z+1.56f};
        Vector3 a1={p.x+ca*7.55f+ta*0.54f,p.y+sa*7.55f+tb*0.54f,p.z-0.45f};
        Vector3 a2={p.x+ca*7.55f-ta*0.54f,p.y+sa*7.55f-tb*0.54f,p.z-0.45f};
        Vector3 a3={p.x+ca*1.15f-ta*1.10f,p.y+sa*1.15f-tb*1.10f,p.z+1.56f};
        DrawThickQuadZ(a0,a1,a2,a3,0.45f,(wing&1)?facet:hull,edge);
        DrawCylinderEx((Vector3){p.x+ca*1.8f,p.y+sa*1.8f,p.z+1.82f},
                       (Vector3){p.x+ca*6.65f,p.y+sa*6.65f,p.z-0.16f},
                       0.22f,0.09f,6,ridge);
        for(int rib=0;rib<4;rib++) {
            float r=2.9f+(float)rib*0.95f;
            Vector3 center={p.x+ca*r,p.y+sa*r,p.z+1.38f-r*0.25f};
            DrawCylinderEx((Vector3){center.x+ta*0.55f,center.y+tb*0.55f,center.z},
                           (Vector3){center.x-ta*0.55f,center.y-tb*0.55f,center.z},
                           0.045f,0.045f,5,ridge);
        }
    }
    // A front-facing triangular iris, never a recolored copy of the turbine.
    Vector3 top={p.x,p.y-1.60f,p.z+2.05f};
    Vector3 left={p.x-1.55f,p.y+1.25f,p.z+2.05f};
    Vector3 right={p.x+1.55f,p.y+1.25f,p.z+2.05f};
    DrawLitTriangle3D(top,left,right,facet);
    DrawRingHousing((Vector3){p.x,p.y,p.z+2.21f},0.78f,0.45f,0.30f,ridge,edge);
    DrawSphereEx((Vector3){p.x,p.y,p.z+2.36f},0.30f,8,10,game->primaryColor);
}

static void DrawMantisBoss(const GameplaySystem *game, Vector3 p, float time) {
    Color hull={26,35,39,255}, edge={8,15,19,255}, armor={77,88,85,255};
    Color steel={120,122,107,255};
    DrawMachineryHousing((Vector3){p.x,p.y,p.z-0.55f},
                         (Vector3){2.8f,8.2f,3.0f},hull,edge);
    DrawCubeV((Vector3){p.x,p.y,p.z+1.32f},(Vector3){1.78f,5.8f,0.62f},armor);
    DrawCubeV((Vector3){p.x,p.y,p.z+1.72f},(Vector3){0.90f,3.2f,0.28f},edge);
    for(int segment=0;segment<7;segment++)
        DrawCubeV((Vector3){p.x,p.y-1.33f+segment*0.45f,p.z+1.91f},
                  (Vector3){0.78f,0.08f,0.15f},steel);
    for(int side=-1;side<=1;side+=2) {
        float s=(float)side;
        Vector3 shoulder={p.x+s*1.20f,p.y+2.02f,p.z+0.42f};
        Vector3 elbow={p.x+s*5.42f,p.y+3.10f+0.16f*sinf(time*0.7f+s),p.z+0.15f};
        Vector3 claw={p.x+s*8.55f,p.y+0.85f,p.z+1.18f};
        DrawCylinderEx(shoulder,elbow,0.54f,0.31f,8,armor);
        DrawCylinderEx(elbow,claw,0.38f,0.20f,8,hull);
        DrawSphereEx(elbow,0.68f,8,12,steel);
        DrawThickQuadZ((Vector3){claw.x,claw.y+0.47f,claw.z},
                       (Vector3){claw.x+s*1.78f,claw.y+0.16f,claw.z+0.20f},
                       (Vector3){claw.x+s*1.15f,claw.y-1.70f,claw.z+0.52f},
                       (Vector3){claw.x,claw.y-0.38f,claw.z+0.20f},
                       0.31f,armor,edge);
        DrawCylinderEx((Vector3){p.x+s*1.0f,p.y-2.28f,p.z-0.7f},
                       (Vector3){p.x+s*4.40f,p.y-3.82f,p.z+0.20f},
                       0.30f,0.14f,7,hull);
        DrawCylinderEx(shoulder,claw,0.075f,0.055f,6,steel);
    }
    DrawSphereEx((Vector3){p.x,p.y,p.z+2.02f},0.36f,8,10,game->secondaryColor);
}

static void DrawLeviathanBoss(const GameplaySystem *game, Vector3 p, float time) {
    Color abyss={3,6,11,255}, hull={17,22,29,255}, plate={51,59,66,255};
    Color strut={93,98,98,255};
    float spin=time*(game->boss.phase==BOSS_SINGULARITY?0.78f:0.26f);
    for(int layer=0;layer<3;layer++) {
        float radius=7.4f+(float)layer*2.15f;
        DrawSegmentedRingXY((Vector3){p.x,p.y,p.z-1.25f-(float)layer*0.34f},
                            radius,12+layer*4,spin*(layer&1?-0.68f:0.42f),
                            0.044f,0.28f-(float)layer*0.04f,
                            layer==1?plate:hull);
    }
    for(int arm=0;arm<12;arm++) {
        float a=(float)arm*0.52359878f-spin*0.35f;
        float ca=cosf(a),sa=sinf(a);
        Vector3 root={p.x+ca*4.9f,p.y+sa*4.9f,p.z-0.65f};
        Vector3 elbow={p.x+ca*9.25f,p.y+sa*9.25f,p.z-1.45f};
        Vector3 tip={p.x+ca*13.0f,p.y+sa*13.0f,p.z-2.2f};
        DrawCylinderEx(root,elbow,0.30f,0.20f,7,plate);
        DrawCylinderEx(elbow,tip,0.21f,0.045f,7,hull);
        DrawSphereEx(elbow,0.34f,6,8,strut);
        Vector3 brace={elbow.x-sa*0.56f,elbow.y+ca*0.56f,elbow.z+0.62f};
        DrawCylinderEx(root,brace,0.075f,0.075f,6,strut);
        DrawCylinderEx(brace,tip,0.075f,0.035f,6,plate);
        if((arm&1)==0)
            DrawThickQuadZ((Vector3){p.x+ca*7.4f-sa*0.65f,p.y+sa*7.4f+ca*0.65f,p.z-0.76f},
                           (Vector3){p.x+ca*10.6f-sa*0.28f,p.y+sa*10.6f+ca*0.28f,p.z-1.50f},
                           (Vector3){p.x+ca*10.6f+sa*0.28f,p.y+sa*10.6f-ca*0.28f,p.z-1.50f},
                           (Vector3){p.x+ca*7.4f+sa*0.65f,p.y+sa*7.4f-ca*0.65f,p.z-0.76f},
                           0.24f,hull,abyss);
    }
    // Open, black central aperture: no shared ordinary-boss pressure vessel.
    DrawRingHousing((Vector3){p.x,p.y,p.z+0.30f},5.18f,3.45f,1.10f,plate,hull);
    DrawRingHousing((Vector3){p.x,p.y,p.z+0.88f},3.72f,3.18f,0.28f,strut,abyss);
    rlPushMatrix();
    rlTranslatef(p.x,p.y,p.z-0.10f);
    rlScalef(1.0f,1.0f,0.12f);
    DrawSphereEx((Vector3){0,0,0},3.30f,12,24,abyss);
    rlPopMatrix();
    DrawSphereEx((Vector3){p.x,p.y,p.z+0.42f},0.18f,6,8,game->hotColor);
}

static void DrawEnemy(const GameplaySystem *game, const Enemy *enemy, float runTime) {
    Color color = EnemyColor(game, enemy);
    Vector3 p = enemy->position;
    Color armor = MixColor(color, (Color){ 6, 10, 24, 255 }, 0.76f, 255);
    Color armorEdge = ScaleColor(color, 0.36f, 14);
    if (enemy->type == ENEMY_BOSS_CORE) {
        int form=game->boss.encounterIndex%BOSSES_PER_RECURSION;
        if(form==1) DrawBastionBoss(game,p,runTime);
        else if(form==2) DrawPrismBoss(game,p,runTime);
        else if(form==3) DrawMantisBoss(game,p,runTime);
        else if(form==4) DrawLeviathanBoss(game,p,runTime);
        else {
        float pulse = sinf(runTime * 5.0f) * 0.5f + 0.5f;
        Color bossArmor = MixColor(game->secondaryColor, (Color){ 8, 11, 19, 255 }, 0.94f, 255);
        Color bossPanel = MixColor(game->primaryColor, (Color){ 34, 42, 50, 255 }, 0.80f, 255);
        Color bossMetal = MixColor(game->secondaryColor, (Color){ 19, 23, 30, 255 }, 0.94f, 255);
        Color bossTrim = MixColor(game->hotColor, (Color){ 8, 11, 22, 255 }, 0.78f, 255);
        float plateSpin = runTime * (game->boss.phase == BOSS_SINGULARITY ? 0.62f
                                      : game->boss.phase == BOSS_ENRAGED ? 0.36f : 0.14f);
        float plateSpread = game->boss.phase == BOSS_SINGULARITY ? 1.18f
                            : game->boss.phase == BOSS_ENRAGED ? 0.72f
                            : (game->boss.phase == BOSS_EXPOSED ? 0.38f : 0.0f);
        float outerRadius = 5.42f + plateSpread;

        // Main pressure vessel and rear machinery establish a heavy axial
        // silhouette before the segmented front armor is layered over it.
        rlPushMatrix();
        rlTranslatef(p.x,p.y,p.z-0.35f);
        rlScalef(1.0f,1.0f,0.60f);
        DrawSphereEx((Vector3){0,0,0},3.55f,12,24,bossArmor);
        rlPopMatrix();
        DrawCylinderEx((Vector3){ p.x, p.y, p.z - 2.45f },
                       (Vector3){ p.x, p.y, p.z + 2.35f },
                       2.42f, 2.12f, 18, bossMetal);
        DrawCylinderEx((Vector3){ p.x, p.y, p.z - 2.65f },
                       (Vector3){ p.x, p.y, p.z - 3.10f },
                       2.02f, 1.46f, 16, bossArmor);

        // Eight wedge-shaped armor petals, each with an inset panel, a radial
        // support spar and occasional hardpoint. Phase changes physically open
        // the assembly instead of merely making it brighter.
        for (int plate = 0; plate < 8; plate++) {
            float angle = plateSpin + (float)plate * 0.78539816f;
            float halfAngle = 0.235f;
            float ca0 = cosf(angle - halfAngle);
            float sa0 = sinf(angle - halfAngle);
            float ca1 = cosf(angle + halfAngle);
            float sa1 = sinf(angle + halfAngle);
            Vector3 innerA = { p.x + ca0 * 2.62f, p.y + sa0 * 2.62f, p.z + 2.72f };
            Vector3 outerA = { p.x + ca0 * outerRadius, p.y + sa0 * outerRadius, p.z + 0.62f };
            Vector3 outerB = { p.x + ca1 * outerRadius, p.y + sa1 * outerRadius, p.z + 0.62f };
            Vector3 innerB = { p.x + ca1 * 2.62f, p.y + sa1 * 2.62f, p.z + 2.72f };
            Color plateColor = (plate & 1) ? bossPanel : bossArmor;
            DrawThickQuadZ(innerA, outerA, outerB, innerB, 0.34f,
                           plateColor, bossMetal);

            // Radial seams are backed by exposed telescopic actuators, not light.
            float gapAngle=angle+0.39f;
            Vector3 ramRoot={p.x+cosf(gapAngle)*2.20f,p.y+sinf(gapAngle)*2.20f,p.z+1.55f};
            Vector3 ramJoint={p.x+cosf(gapAngle)*3.55f,p.y+sinf(gapAngle)*3.55f,p.z+1.20f};
            Vector3 ramEnd={p.x+cosf(gapAngle)*(outerRadius-0.05f),p.y+sinf(gapAngle)*(outerRadius-0.05f),p.z+0.50f};
            DrawCylinderEx(ramRoot,ramJoint,0.17f,0.17f,8,bossMetal);
            DrawCylinderEx(ramJoint,ramEnd,0.075f,0.075f,8,(Color){92,99,106,255});
            DrawSphereEx(ramJoint,0.22f,6,8,bossTrim);

            float insetInner = 3.28f;
            float insetOuter = outerRadius - 0.33f;
            float insetHalf = 0.125f;
            Vector3 insetA = { p.x + cosf(angle - insetHalf) * insetInner,
                               p.y + sinf(angle - insetHalf) * insetInner, p.z + 2.48f };
            Vector3 insetB = { p.x + cosf(angle - insetHalf) * insetOuter,
                               p.y + sinf(angle - insetHalf) * insetOuter, p.z + 0.83f };
            Vector3 insetC = { p.x + cosf(angle + insetHalf) * insetOuter,
                               p.y + sinf(angle + insetHalf) * insetOuter, p.z + 0.83f };
            Vector3 insetD = { p.x + cosf(angle + insetHalf) * insetInner,
                               p.y + sinf(angle + insetHalf) * insetInner, p.z + 2.48f };
            DrawThickQuadZ(insetA, insetB, insetC, insetD, 0.075f,
                           bossMetal, bossArmor);

            Vector3 sparRoot = { p.x + cosf(angle) * 2.18f,
                                 p.y + sinf(angle) * 2.18f, p.z - 0.42f };
            Vector3 sparTip = { p.x + cosf(angle) * (outerRadius - 0.18f),
                                p.y + sinf(angle) * (outerRadius - 0.18f), p.z - 0.42f };
            DrawCylinderEx(sparRoot, sparTip, 0.16f, 0.11f, 7, bossMetal);
            DrawSphereEx((Vector3){ p.x + cosf(angle) * 2.72f,
                                    p.y + sinf(angle) * 2.72f, p.z + 2.55f },
                         0.20f, 6, 8, bossMetal);
            if ((plate & 1) == 0) {
                Vector3 hardpointBase = { p.x + cosf(angle) * (outerRadius - 0.55f),
                                          p.y + sinf(angle) * (outerRadius - 0.55f),
                                          p.z + 0.82f };
                Vector3 hardpointTip = { hardpointBase.x, hardpointBase.y, p.z + 0.92f };
                DrawCylinderEx(hardpointBase, hardpointTip, 0.16f, 0.085f, 7, bossTrim);
                DrawSphereEx(hardpointTip, 0.11f, 5, 7,
                             (plate & 2) ? game->secondaryColor : game->primaryColor);
            }
        }

        // Segmented maintenance halo: a real mechanical ring assembled from
        // dark metal links, with one faint alignment trace for motion.
        for (int segment = 0; segment < 12; segment++) {
            float a0 = -plateSpin * 0.72f + (float)segment * 0.52359878f + 0.035f;
            float a1 = -plateSpin * 0.72f + (float)(segment + 1) * 0.52359878f - 0.035f;
            Vector3 ringA = { p.x + cosf(a0) * 6.12f,
                              p.y + sinf(a0) * 6.12f, p.z - 0.82f };
            Vector3 ringB = { p.x + cosf(a1) * 6.12f,
                              p.y + sinf(a1) * 6.12f, p.z - 0.82f };
            DrawCylinderEx(ringA, ringB, 0.14f, 0.14f, 6, bossMetal);
            float middle = (a0 + a1) * 0.5f;
            DrawSphereEx((Vector3){ p.x + cosf(middle) * 6.12f,
                                    p.y + sinf(middle) * 6.12f, p.z - 0.82f },
                         0.17f, 5, 7, bossPanel);
        }
        DrawCircle3D((Vector3){ p.x, p.y, p.z - 0.76f }, 6.14f,
                     (Vector3){ 1.0f, 0.0f, 0.0f }, runTime * -9.0f,
                     (Color){ game->secondaryColor.r, game->secondaryColor.g,
                              game->secondaryColor.b, 58 });

        // Concentric front reactor housing. Only the small innermost aperture
        // carries a strong accent; the surrounding machinery remains dark.
        DrawRingHousing((Vector3){p.x,p.y,p.z+2.78f},2.24f,1.55f,0.40f,bossPanel,bossMetal);
        DrawRingHousing((Vector3){p.x,p.y,p.z+3.16f},1.64f,1.01f,0.36f,bossMetal,bossArmor);
        DrawRingHousing((Vector3){p.x,p.y,p.z+3.23f},1.66f,1.52f,0.10f,(Color){75,83,91,255},bossMetal);
        DrawCylinderEx((Vector3){p.x,p.y,p.z+2.43f},(Vector3){p.x,p.y,p.z+2.51f},
                       1.04f,1.04f,24,(Color){4,7,11,255});
        for(int vane=0;vane<16;vane++) {
            float a=(float)vane*0.39269908f+runTime*0.16f;
            float b=a+0.20f;
            Vector3 a0={p.x+cosf(a)*0.42f,p.y+sinf(a)*0.42f,p.z+2.76f};
            Vector3 a1={p.x+cosf(a+0.11f)*0.98f,p.y+sinf(a+0.11f)*0.98f,p.z+2.88f};
            Vector3 a2={p.x+cosf(b+0.11f)*0.98f,p.y+sinf(b+0.11f)*0.98f,p.z+2.93f};
            Vector3 a3={p.x+cosf(b)*0.42f,p.y+sinf(b)*0.42f,p.z+2.83f};
            DrawThickQuadZ(a0,a1,a2,a3,0.05f,(Color){76,85,95,255},bossArmor);
            Vector3 bolt={p.x+cosf(a)*1.90f,p.y+sinf(a)*1.90f,p.z+2.84f};
            DrawCylinderEx(bolt,(Vector3){bolt.x,bolt.y,bolt.z+0.07f},0.078f,0.067f,6,bossTrim);
        }
        DrawSphereEx((Vector3){ p.x, p.y, p.z + 3.18f },
                     0.58f + pulse * 0.06f, 10, 16, bossTrim);
        DrawSphereEx((Vector3){ p.x, p.y, p.z + 3.58f },
                     0.20f + pulse * 0.035f, 7, 10, color);

        // Four rear power modules sit behind the armored petals. Their dark
        // exhaust housings deepen the silhouette without adding light sources.
        for (int module = 0; module < 4; module++) {
            float angle = plateSpin * 0.38f + (float)module * 1.5707963f + 0.78539816f;
            float mx = p.x + cosf(angle) * 3.35f;
            float my = p.y + sinf(angle) * 3.35f;
            DrawCylinderEx((Vector3){ mx, my, p.z - 1.72f },
                           (Vector3){ mx, my, p.z - 3.48f },
                           0.44f, 0.27f, 10, bossMetal);
            DrawCylinderEx((Vector3){ mx, my, p.z - 3.40f },
                           (Vector3){ mx, my, p.z - 3.76f },
                           0.28f, 0.20f, 9, bossArmor);
        }

        if (game->boss.shieldNodes > 0) {
            DrawCircle3D(p, 7.60f, (Vector3){ 0.4f, 1.0f, 0.2f },
                         runTime * 11.0f,
                         (Color){ game->primaryColor.r, game->primaryColor.g,
                                  game->primaryColor.b, 120 });
            DrawCircle3D(p, 8.10f, (Vector3){ 1.0f, 0.25f, 0.4f },
                         runTime * -8.0f,
                         (Color){ game->secondaryColor.r, game->secondaryColor.g,
                                  game->secondaryColor.b, 92 });
        }
        }
    } else if (enemy->type == ENEMY_BOSS_NODE) {
        Color nodeMetal = MixColor(game->secondaryColor, (Color){ 10, 14, 24, 255 }, 0.82f, 255);
        Color nodeArmor = MixColor(color, (Color){ 7, 11, 21, 255 }, 0.90f, 255);
        float nodeSpin = runTime * 0.58f + (float)enemy->generation * 0.67f;
        if (game->boss.leviathan) {
            DrawSegmentedRingXY((Vector3){ p.x, p.y, p.z - 0.46f }, 2.15f, 8,
                                -nodeSpin * 0.52f, 0.075f, 0.12f, nodeMetal);
            for (int brace = 0; brace < 4; brace++) {
                float angle = nodeSpin * 0.24f + (float)brace * 1.5707963f;
                Vector3 root = { p.x + cosf(angle) * 1.18f,
                                 p.y + sinf(angle) * 1.18f, p.z - 0.32f };
                Vector3 tip = { p.x + cosf(angle) * 2.08f,
                                p.y + sinf(angle) * 2.08f, p.z - 0.55f };
                DrawCylinderEx(root, tip, 0.13f, 0.07f, 6, nodeArmor);
            }
        }
        rlPushMatrix();
        rlTranslatef(p.x,p.y,p.z-0.10f);
        rlScalef(1.0f,1.0f,0.62f);
        DrawSphereEx((Vector3){0,0,0},1.16f,10,16,nodeArmor);
        rlPopMatrix();
        DrawCylinderEx((Vector3){ p.x, p.y, p.z - 0.82f },
                       (Vector3){ p.x, p.y, p.z + 0.86f },
                       0.92f, 0.78f, 12, nodeMetal);
        for (int fin = 0; fin < 4; fin++) {
            float angle = nodeSpin + (float)fin * 1.5707963f;
            float halfAngle = 0.21f;
            Vector3 a = { p.x + cosf(angle - halfAngle) * 0.72f,
                          p.y + sinf(angle - halfAngle) * 0.72f, p.z + 0.03f };
            Vector3 b = { p.x + cosf(angle - halfAngle) * 1.58f,
                          p.y + sinf(angle - halfAngle) * 1.58f, p.z - 0.04f };
            Vector3 c = { p.x + cosf(angle + halfAngle) * 1.58f,
                          p.y + sinf(angle + halfAngle) * 1.58f, p.z - 0.04f };
            Vector3 d = { p.x + cosf(angle + halfAngle) * 0.72f,
                          p.y + sinf(angle + halfAngle) * 0.72f, p.z + 0.03f };
            DrawThickQuadZ(a, b, c, d, 0.16f,
                           (fin & 1) ? nodeArmor : nodeMetal, armorEdge);
        }
        DrawCylinderEx((Vector3){ p.x, p.y, p.z + 0.72f },
                       (Vector3){ p.x, p.y, p.z + 1.15f },
                       0.48f, 0.29f, 10, armorEdge);
        DrawSphereEx((Vector3){ p.x, p.y, p.z + 1.19f }, 0.18f, 6, 9, color);
        DrawRingHousing((Vector3){p.x,p.y,p.z+1.16f},0.68f,0.33f,0.16f,nodeMetal,nodeArmor);
        DrawCircle3D((Vector3){ p.x, p.y, p.z - 0.12f }, 1.42f,
                     (Vector3){ 1.0f, 0.0f, 0.0f }, runTime * 34.0f, armorEdge);
    } else if (enemy->type == ENEMY_DRIFTER) {
        float radius = enemy->size.x * 0.56f;
        Color drifterMetal = MixColor(color, (Color){ 26, 31, 39, 255 }, 0.88f, 255);
        float spin = runTime * 0.34f + (float)enemy->identity * 0.43f;
        DrawSphereEx(p, radius * 0.64f, 7, 10, armor);
        DrawCylinderEx((Vector3){ p.x, p.y - radius * 0.22f, p.z },
                       (Vector3){ p.x, p.y + radius * 0.22f, p.z },
                       radius * 1.30f, radius * 1.08f, 14, drifterMetal);
        DrawSegmentedRingXY((Vector3){ p.x, p.y, p.z - 0.08f }, radius * 1.42f,
                            8, spin, 0.055f, 0.075f, drifterMetal);
        for (int plate = 0; plate < 4; plate++) {
            float angle = spin * 0.45f + (float)plate * 1.5707963f;
            float h = 0.22f;
            Vector3 a = { p.x + cosf(angle - h) * radius * 0.62f,
                          p.y + sinf(angle - h) * radius * 0.62f, p.z + radius * 0.66f };
            Vector3 b = { p.x + cosf(angle - h) * radius * 1.30f,
                          p.y + sinf(angle - h) * radius * 1.30f, p.z + radius * 0.18f };
            Vector3 c = { p.x + cosf(angle + h) * radius * 1.30f,
                          p.y + sinf(angle + h) * radius * 1.30f, p.z + radius * 0.18f };
            Vector3 d = { p.x + cosf(angle + h) * radius * 0.62f,
                          p.y + sinf(angle + h) * radius * 0.62f, p.z + radius * 0.66f };
            DrawThickQuadZ(a, b, c, d, 0.085f,
                           (plate & 1) ? armor : drifterMetal, armorEdge);
        }
        DrawCylinderEx((Vector3){ p.x, p.y, p.z + radius * 0.58f },
                       (Vector3){ p.x, p.y, p.z + radius * 1.02f },
                       radius * 0.38f, radius * 0.23f, 10, drifterMetal);
        DrawSphereEx((Vector3){ p.x, p.y, p.z + radius * 1.05f },
                     radius * 0.17f, 6, 9, color);
        for (int engine = -1; engine <= 1; engine += 2) {
            float ex = p.x + (float)engine * radius * 0.62f;
            DrawCylinderEx((Vector3){ ex, p.y - radius * 0.16f, p.z - radius * 0.50f },
                           (Vector3){ ex, p.y - radius * 0.16f, p.z - radius * 0.94f },
                           radius * 0.18f, radius * 0.11f, 8, drifterMetal);
        }
    } else if (enemy->type == ENEMY_CHASER) {
        float sx = enemy->size.x;
        float sy = enemy->size.y;
        Color chaserMetal = MixColor(color, (Color){ 30, 34, 41, 255 }, 0.87f, 255);
        Vector3 tip = { p.x, p.y, p.z + enemy->size.z * 1.12f };
        Vector3 left = { p.x - sx, p.y - sy * 0.34f, p.z - 0.84f };
        Vector3 right = { p.x + sx, p.y - sy * 0.34f, p.z - 0.84f };
        DrawThickTriangle3D(tip, left, right, 0.28f, armorEdge, armor);
        DrawThickTriangle3D((Vector3){ p.x, p.y + sy * 0.12f, p.z + 0.55f },
                            (Vector3){ p.x - sx * 0.58f, p.y + sy * 0.62f, p.z - 0.62f },
                            (Vector3){ p.x + sx * 0.58f, p.y + sy * 0.62f, p.z - 0.62f },
                            0.13f, armor, ScaleColor(armor, 0.72f, 0));
        DrawThickTriangle3D((Vector3){ p.x, p.y + sy * 0.25f, p.z + 1.26f },
                            (Vector3){ p.x - sx * 0.34f, p.y + sy * 0.28f, p.z - 0.48f },
                            (Vector3){ p.x + sx * 0.34f, p.y + sy * 0.28f, p.z - 0.48f },
                            0.09f, chaserMetal, armor);
        DrawSphereEx((Vector3){ p.x, p.y + sy * 0.33f, p.z + 0.18f },
                     0.20f, 6, 9, chaserMetal);
        DrawCylinderEx((Vector3){ p.x, p.y, p.z - 0.62f },
                       (Vector3){ p.x, p.y, p.z - 1.02f },
                       0.28f, 0.12f, 10, color);
        for (int side = -1; side <= 1; side += 2) {
            Vector3 cannonStart = { p.x + (float)side * sx * 0.58f,
                                    p.y - sy * 0.08f, p.z - 0.22f };
            Vector3 cannonEnd = { cannonStart.x, cannonStart.y, p.z + 0.72f };
            DrawCylinderEx(cannonStart, cannonEnd, 0.10f, 0.055f, 7, color);
            DrawSphereEx((Vector3){ cannonStart.x, cannonStart.y, p.z - 0.72f },
                         0.12f, 5, 8, game->secondaryColor);
            DrawCylinderEx((Vector3){ p.x + (float)side * sx * 0.48f,
                                      p.y - sy * 0.22f, p.z - 0.62f },
                           (Vector3){ p.x + (float)side * sx * 0.48f,
                                      p.y - sy * 0.22f, p.z - 1.22f },
                           0.22f, 0.13f, 9, chaserMetal);
            DrawCylinderEx(tip, (Vector3){ p.x + (float)side * sx * 0.92f,
                                           p.y - sy * 0.30f, p.z - 0.70f },
                           0.035f, 0.055f, 6, chaserMetal);
        }
    } else {
        float coreRadius = enemy->size.x * 0.46f;
        Color splitterMetal = MixColor(color, (Color){ 24, 30, 38, 255 }, 0.88f, 255);
        DrawSphereEx(p, coreRadius, 8, 12, armor);
        DrawCylinderEx((Vector3){ p.x, p.y, p.z - coreRadius * 0.62f },
                       (Vector3){ p.x, p.y, p.z + coreRadius * 0.82f },
                       coreRadius * 0.72f, coreRadius * 0.50f, 10, splitterMetal);
        DrawSegmentedRingXY((Vector3){ p.x, p.y, p.z - 0.18f }, coreRadius * 1.18f,
                            6, -runTime * 0.46f + (float)enemy->identity,
                            0.07f, 0.085f, splitterMetal);
        DrawSphereEx((Vector3){ p.x, p.y, p.z + enemy->size.z * 0.43f },
                     enemy->size.x * 0.16f, 6, 9, color);
        for (int i = 0; i < 3; i++) {
            float angle = runTime * 2.4f + (float)i * 2.0944f + (float)enemy->identity;
            Vector3 orb = { p.x + cosf(angle) * 1.6f, p.y + sinf(angle) * 1.6f, p.z };
            Vector3 joint = { p.x + cosf(angle) * 0.90f,
                              p.y + sinf(angle) * 0.90f, p.z + 0.08f };
            DrawCylinderEx(p, joint, 0.15f, 0.19f, 7, splitterMetal);
            DrawSphereEx(joint, 0.20f, 6, 8, armorEdge);
            DrawCylinderEx(joint, orb, 0.12f, 0.18f, 7, splitterMetal);
            DrawCylinderEx((Vector3){ orb.x, orb.y, orb.z - 0.30f },
                           (Vector3){ orb.x, orb.y, orb.z + 0.34f },
                           0.32f, 0.24f, 8, armor);
            DrawSphereEx((Vector3){ orb.x, orb.y, orb.z + 0.37f },
                         0.15f, 6, 8, color);
        }
    }

    if (enemy->phase == ENEMY_TELEGRAPH) {
        float charge = 0.55f + 0.25f * sinf(enemy->phaseTime * 24.0f);
        Vector3 emitter = { p.x, p.y, p.z + enemy->size.z * 0.72f };
        DrawSphereEx(emitter, 0.18f + charge * 0.14f, 6, 10,
                     (Color){ 245, 225, 255, 230 });
        DrawCircle3D(emitter, 0.54f + charge * 0.18f,
                     (Vector3){ 1.0f, 0.0f, 0.0f }, runTime * 95.0f, color);
    }

    if (enemy->type != ENEMY_BOSS_CORE && enemy->type != ENEMY_BOSS_NODE) {
        DrawLine3D(p, (Vector3){ p.x, p.y, p.z - 2.4f - game->difficulty * 2.5f },
                   (Color){ color.r, color.g, color.b, 132 });
    }

    if (enemy->locked) {
        float lockRadius = fmaxf(enemy->size.x, enemy->size.y) * 0.82f;
        Color lockColor = (Color){ 255, 220, 70, 225 };
        DrawCircle3D(p, lockRadius, (Vector3){ 1.0f, 0.0f, 0.0f },
                     runTime * 78.0f, lockColor);
        DrawCircle3D(p, lockRadius * 1.13f, (Vector3){ 0.0f, 1.0f, 0.0f },
                     runTime * -52.0f, (Color){ 255, 220, 70, 130 });
    }
}

static Vector3 FluxPoint(Vector3 p,Vector3 u,Vector3 v,float radius,float angle) {
    return Vector3Add(p,Vector3Add(Vector3Scale(u,cosf(angle)*radius),
                                  Vector3Scale(v,sinf(angle)*radius)));
}

// Thin, two-sided annular ribbons, not filled discs or extra bloom sprites.
static void DrawFluxRing(Vector3 p,Vector3 u,Vector3 v,float radius,float width,
                         int segments,float phase,float breakup,Color color) {
    for(int i=0;i<segments;i++) {
        float a=(float)i*6.2831853f/segments,b=(float)(i+1)*6.2831853f/segments;
        float modulation=0.72f+0.28f*sinf(a*7.0f+phase);
        float ra=radius*(1.0f+breakup*sinf(a*5.0f+phase));
        float rb=radius*(1.0f+breakup*sinf(b*5.0f+phase));
        Vector3 a0=FluxPoint(p,u,v,ra-width,a),b0=FluxPoint(p,u,v,rb-width,b);
        Vector3 a1=FluxPoint(p,u,v,ra+width,a),b1=FluxPoint(p,u,v,rb+width,b);
        Color c=color; c.a=(unsigned char)(color.a*modulation);
        DrawQuad3D(a0,b0,b1,a1,c);
        DrawQuad3D(a1,b1,b0,a0,c);
    }
}

static void DrawPulseProjectile(const PlayerProjectile *bolt,Color color,float time,int id) {
    Vector3 axis=Vector3Normalize(bolt->velocity);
    if(Vector3LengthSqr(axis)<0.1f) axis=(Vector3){0,0,-1};
    Vector3 u=Vector3Normalize(Vector3CrossProduct(axis,fabsf(axis.y)<0.85f?
                                                (Vector3){0,1,0}:(Vector3){1,0,0}));
    Vector3 v=Vector3CrossProduct(axis,u),p=bolt->position;
    float radius=bolt->charged?0.29f:0.16f;
    float phase=time*11.0f+(float)id*2.71f;
    float length=bolt->charged?1.20f:0.70f;
    Vector3 nose=Vector3Add(p,Vector3Scale(axis,length*0.63f));
    Vector3 rear=Vector3Add(p,Vector3Scale(axis,-length*0.56f));
    DrawCylinderEx(rear,p,radius*0.12f,radius,10,(Color){color.r,color.g,color.b,160});
    DrawCylinderEx(p,nose,radius,0.008f,10,(Color){color.r,color.g,color.b,220});
    DrawCylinderEx(rear,nose,radius*0.21f,radius*0.12f,8,(Color){220,239,248,240});
    DrawEnergyHalo(p,radius*3.3f,(Color){color.r,color.g,color.b,38});
    for(int ring=0;ring<3;ring++) {
        Vector3 center=Vector3Add(p,Vector3Scale(axis,-length*(float)ring*0.23f));
        DrawFluxRing(center,u,v,radius*(1.38f+ring*0.32f),radius*0.065f,
                     28,phase+ring,0.065f,(Color){color.r,color.g,color.b,(unsigned char)(175-ring*32)});
    }
    // Helical filaments follow velocity; they do not consume projectile/particle slots.
    for(int strand=0;strand<3;strand++) {
        Vector3 last=p;
        int segments=bolt->charged?32:24;
        float wakeLength=bolt->charged?5.4f:3.0f;
        for(int segment=0;segment<segments;segment++) {
            float t=(float)(segment+1)/segments;
            Vector3 center=Vector3Add(p,Vector3Scale(axis,-t*wakeLength));
            float a=phase+(float)strand*2.094395f-t*9.0f;
            Vector3 next=FluxPoint(center,u,v,radius*(1.3f+t*2.4f),a);
            float width=radius*(0.105f*(1-t)+0.012f);
            DrawCylinderEx(last,next,width,width*0.75f,4,
                           (Color){color.r,color.g,color.b,(unsigned char)(145*(1-t))});
            last=next;
        }
    }
    // Four split fins around the hot nose make a pulse read as a shaped discharge.
    for(int fin=0;fin<4;fin++) {
        float a=phase*0.7f+(float)fin*1.5707963f;
        Vector3 tip=FluxPoint(rear,u,v,radius*2.3f,a);
        Vector3 edge=FluxPoint(rear,u,v,radius*1.3f,a+0.22f);
        DrawLitTriangle3D(nose,tip,edge,(Color){color.r,color.g,color.b,95});
        DrawLitTriangle3D(nose,edge,tip,(Color){color.r,color.g,color.b,95});
    }
}

static void DrawDetailedBeam(const GameplaySystem *game) {
    float age=1.0f-ClampFloat(game->beamTimer/0.34f,0,1);
    float envelope=powf(sinf(age*3.14159265f),0.55f);
    Vector3 start=game->player.position; start.z-=1.05f;
    Vector3 end={start.x,start.y,-210.0f};
    Color tint=game->hotColor;
    unsigned char alpha=(unsigned char)(envelope*180.0f);
    DrawCylinderEx(start,end,0.46f*envelope,0.13f*envelope,16,(Color){tint.r,tint.g,tint.b,(unsigned char)(alpha/4)});
    DrawCylinderEx(start,end,0.095f*envelope,0.035f*envelope,10,(Color){221,244,250,alpha});
    // Six independently twisted strands and moving compression collars.
    for(int strand=0;strand<6;strand++) {
        Vector3 previous=start;
        for(int segment=0;segment<48;segment++) {
            float t=(float)(segment+1)/48.0f;
            float distance=4.0f*t+175.0f*t*t;
            float radius=(0.19f+0.25f*(1-t))*envelope;
            float a=(float)strand*1.0471976f+distance*0.78f-age*22.0f;
            Vector3 next={start.x+cosf(a)*radius,start.y+sinf(a)*radius,start.z-distance};
            float w=(0.021f+0.009f*(1-t))*envelope;
            DrawCylinderEx(previous,next,w,w,4,(Color){tint.r,tint.g,tint.b,(unsigned char)(alpha*0.67f)});
            previous=next;
        }
    }
    for(int ring=0;ring<19;ring++) {
        float t=(float)ring/19.0f;
        float distance=1.3f+t*t*152.0f+age*8.0f;
        Vector3 p={start.x,start.y,start.z-distance};
        DrawFluxRing(p,(Vector3){1,0,0},(Vector3){0,1,0},
                     (0.62f+0.28f*sinf(ring*1.7f-age*12))*envelope,
                     0.018f*envelope,32,age*8+ring,0.055f,
                     (Color){tint.r,tint.g,tint.b,(unsigned char)(alpha*0.75f)});
    }
    // Compact muzzle iris and short escaping sparks preserve target visibility.
    DrawFluxRing(start,(Vector3){1,0,0},(Vector3){0,1,0},0.59f*envelope,
                 0.045f,40,age*14,0.12f,(Color){203,227,244,alpha});
    for(int spark=0;spark<24;spark++) {
        float h=Hash01((uint32_t)spark*977u^game->runSeed);
        float a=(float)spark*2.399963f;
        float distance=fmodf(age*8.0f+h*5.0f,5.0f);
        Vector3 p={start.x+cosf(a)*(0.25f+distance*0.20f),
                   start.y+sinf(a)*(0.25f+distance*0.20f),start.z-distance};
        Vector3 tail={p.x-cosf(a)*0.10f,p.y-sinf(a)*0.10f,p.z+0.23f};
        DrawCylinderEx(tail,p,0.009f,0.020f,4,(Color){203,222,236,(unsigned char)(alpha*0.64f)});
    }
}

static void DrawDetailedBomb(const GameplaySystem *game) {
    float age=ClampFloat(1.0f-game->bombTimer/0.72f,0,1);
    float radius=4.0f+age*78.0f,fade=(1-age)*(1-age);
    Vector3 center=game->player.position;
    Color color=game->secondaryColor;
    for(int wave=0;wave<4;wave++) {
        float r=radius*(1.0f-wave*0.075f);
        Vector3 c=center; c.y+=(float)wave*0.16f;
        DrawFluxRing(c,(Vector3){1,0,0},(Vector3){0,0,1},r,0.06f+age*0.18f,
                     96,age*12+wave,0.012f,(Color){color.r,color.g,color.b,(unsigned char)(fade*(190-wave*30))});
    }
    for(int latitude=0;latitude<3;latitude++) {
        Vector3 c=center; c.y+=radius*(0.10f+latitude*0.09f);
        DrawFluxRing(c,(Vector3){1,0,0},(Vector3){0,0,1},radius*(0.96f-latitude*0.12f),
                     0.036f,80,-age*10+latitude,0.02f,(Color){168,192,217,(unsigned char)(fade*88)});
    }
    for(int spoke=0;spoke<48;spoke++) {
        float h=Hash01(game->runSeed^(uint32_t)spoke*391u);
        float a=(float)spoke*2.399963f;
        float r=radius*(0.70f+h*0.27f);
        Vector3 p={center.x+cosf(a)*r,center.y+sinf(age*3.14159f)*(0.3f+h*2),center.z+sinf(a)*r};
        Vector3 tail={p.x-cosf(a)*(0.4f+h*0.9f),p.y-0.12f,p.z-sinf(a)*(0.4f+h*0.9f)};
        DrawCylinderEx(tail,p,0.018f,0.036f,4,(Color){183,207,230,(unsigned char)(fade*155)});
    }
}

void DrawGameplay3D(const GameplaySystem *game, Shader craftShader, int objectClassLoc,
                    int enemyTypeLoc, int objectOriginLoc) {
    int objectClass = 0;
    rlDrawRenderBatchActive();
    SetShaderValue(craftShader, objectOriginLoc, &game->player.position, SHADER_UNIFORM_VEC3);
    if (objectClassLoc >= 0) {
        SetShaderValue(craftShader, objectClassLoc, &objectClass, SHADER_UNIFORM_INT);
    }
    DrawPlayerShip(game);
    if (game->loopTransitionTimer > 0.0f) {
        float progress = 1.0f - game->loopTransitionTimer / CORE_TRANSITION_DURATION;
        float collapse = ClampFloat(progress / 0.58f, 0.0f, 1.0f);
        float dissipate = 1.0f - ClampFloat((progress - 0.52f) / 0.48f, 0.0f, 1.0f);
        Vector3 core = game->boss.position;
        for (int ring = 0; ring < 11; ring++) {
            float phase = fmodf(collapse * 2.4f + (float)ring / 11.0f, 1.0f);
            float radius = (1.0f - phase) * (28.0f + (float)(ring & 2) * 3.0f) + 0.8f;
            unsigned char alpha = (unsigned char)(dissipate * (1.0f - phase) * 190.0f);
            Color color = (ring & 1)
                ? (Color){ game->secondaryColor.r, game->secondaryColor.g,
                           game->secondaryColor.b, alpha }
                : (Color){ game->hotColor.r, game->hotColor.g, game->hotColor.b, alpha };
            DrawCircle3D((Vector3){ core.x, core.y, core.z + (float)ring * 0.16f },
                         radius, (Vector3){ 1.0f, 0.0f, 0.0f },
                         game->runTime * (45.0f + (float)ring * 7.0f), color);
        }
        for (int spoke = 0; spoke < 24; spoke++) {
            float angle = (float)spoke * 0.2617994f + game->runTime * 0.42f;
            float radius = (1.0f - collapse) * 18.0f + 1.2f + (float)(spoke % 3) * 1.6f;
            Vector3 fragment = { core.x + cosf(angle) * radius,
                                 core.y + sinf(angle) * radius * 0.62f,
                                 core.z + sinf(angle * 3.0f) * radius * 0.24f };
            DrawLine3D(fragment, core,
                       (Color){ game->primaryColor.r, game->primaryColor.g,
                                game->primaryColor.b,
                                (unsigned char)(dissipate * 125.0f) });
            DrawSphereEx(fragment, 0.12f + (float)(spoke % 4) * 0.05f,
                         4, 6, (Color){ 235, 250, 255,
                                       (unsigned char)(dissipate * 210.0f) });
        }
    }
    rlDrawRenderBatchActive();
    objectClass = 1;
    if (objectClassLoc >= 0) {
        SetShaderValue(craftShader, objectClassLoc, &objectClass, SHADER_UNIFORM_INT);
    }
    if (game->boss.active && game->boss.coreSlot >= 0 &&
        game->enemies[game->boss.coreSlot].active) {
        Vector3 core = game->enemies[game->boss.coreSlot].position;
        if (game->boss.phase == BOSS_APPROACH) {
            float progress = ClampFloat(game->boss.phaseTime / 2.6f, 0.0f, 1.0f);
            float arrival = sinf(progress * 3.14159265f);
            for (int ring = 0; ring < 7; ring++) {
                float scale = game->boss.leviathan ? 1.48f : 1.0f;
                float radius = (4.0f + (float)ring * 2.25f + (1.0f - progress) * 6.0f) * scale;
                unsigned char alpha = (unsigned char)((0.22f + arrival * 0.55f) *
                                                       (155.0f - (float)ring * 13.0f));
                Color ringColor = (ring & 1)
                    ? (Color){ game->secondaryColor.r, game->secondaryColor.g,
                               game->secondaryColor.b, alpha }
                    : (Color){ game->primaryColor.r, game->primaryColor.g,
                               game->primaryColor.b, alpha };
                DrawCircle3D((Vector3){ core.x, core.y, core.z - 4.0f - (float)ring * 0.22f },
                             radius, (Vector3){ 1.0f, 0.0f, 0.0f },
                             game->runTime * (18.0f + (float)ring * 3.0f), ringColor);
            }
            for (int spoke = 0; spoke < 8; spoke++) {
                float angle = (float)spoke * 0.78539816f + game->runTime * 0.35f;
                Vector3 edge = { core.x + cosf(angle) * 15.5f,
                                 core.y + sinf(angle) * 10.0f,
                                 core.z - 4.6f };
                DrawLine3D(edge, core,
                           (Color){ game->hotColor.r, game->hotColor.g,
                                    game->hotColor.b,
                                    (unsigned char)(arrival * 125.0f) });
            }
        }
        float arenaScale = game->boss.leviathan ? 1.55f : 1.0f;
        DrawCircle3D((Vector3){ core.x, core.y, core.z - 3.0f }, 10.5f * arenaScale,
                     (Vector3){ 1.0f, 0.0f, 0.0f }, game->runTime * 8.0f,
                     (Color){ game->primaryColor.r, game->primaryColor.g,
                              game->primaryColor.b, 90 });
        DrawCircle3D((Vector3){ core.x, core.y, core.z - 3.2f }, 13.5f * arenaScale,
                     (Vector3){ 1.0f, 0.0f, 0.0f }, game->runTime * -5.0f,
                     (Color){ game->secondaryColor.r, game->secondaryColor.g,
                              game->secondaryColor.b, 70 });
        for (int node = 0; node < 4; node++) {
            int slot = game->boss.nodeSlots[node];
            if (slot < 0 || !game->enemies[slot].active) continue;
            DrawCylinderEx(core, game->enemies[slot].position,
                           game->boss.leviathan ? 0.17f : 0.11f,
                           game->boss.leviathan ? 0.08f : 0.06f, 6,
                           (Color){ game->secondaryColor.r, game->secondaryColor.g,
                                    game->secondaryColor.b, 135 });
        }
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!game->enemies[i].active) continue;
        rlDrawRenderBatchActive();
        SetShaderValue(craftShader, objectOriginLoc, &game->enemies[i].position, SHADER_UNIFORM_VEC3);
        if (enemyTypeLoc >= 0) {
            rlDrawRenderBatchActive();
            int enemyType = (int)game->enemies[i].type;
            SetShaderValue(craftShader, enemyTypeLoc, &enemyType, SHADER_UNIFORM_INT);
        }
        DrawEnemy(game, &game->enemies[i], game->runTime);
    }
    rlDrawRenderBatchActive();
    objectClass = 2;
    if (objectClassLoc >= 0) {
        SetShaderValue(craftShader, objectClassLoc, &objectClass, SHADER_UNIFORM_INT);
    }
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    DrawPlayerExhaust(game);
    for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
        const EnemyProjectile *projectile = &game->projectiles[i];
        if (!projectile->active) continue;
        Vector3 trail = { projectile->position.x - projectile->velocity.x * 0.065f,
                          projectile->position.y - projectile->velocity.y * 0.065f,
                          projectile->position.z - projectile->velocity.z * 0.065f };
        // An amber core distinguishes boost-armed homing missiles from the
        // ordinary straight shots without adding a light source or bloom.
        Color threat = projectile->homing
            ? MixColor(game->threatColor,(Color){255,172,83,255},0.48f,255)
            : game->threatColor;
        Color threatDark = ScaleColor(threat, 0.45f, 0);
        DrawEnergyHalo(projectile->position,0.9f,
                       (Color){threat.r,threat.g,threat.b,70});
        DrawCylinderEx(trail, projectile->position, 0.025f, 0.15f, 7,
                       (Color){ threat.r, threat.g, threat.b, 150 });
        DrawSphereEx(projectile->position, 0.36f, 6, 10,
                     (Color){ threatDark.r, threatDark.g, threatDark.b, 145 });
        DrawSphereEx(projectile->position, 0.18f, 6, 10, threat);
        DrawSphereEx((Vector3){ projectile->position.x, projectile->position.y,
                                projectile->position.z + 0.08f },
                     0.075f, 5, 8, (Color){ 255, 255, 255, 255 });
    }
    for (int i = 0; i < MAX_PLAYER_PROJECTILES; i++) {
        const PlayerProjectile *bolt = &game->playerProjectiles[i];
        if (!bolt->active) continue;
        Color core = bolt->charged ? game->hotColor : game->secondaryColor;
        rlDrawRenderBatchActive();
        objectClass=4;
        SetShaderValue(craftShader,objectClassLoc,&objectClass,SHADER_UNIFORM_INT);
        SetShaderValue(craftShader,objectOriginLoc,&bolt->position,SHADER_UNIFORM_VEC3);
        DrawPulseProjectile(bolt,core,game->runTime,i);
    }
    rlDrawRenderBatchActive();
    objectClass=2;
    SetShaderValue(craftShader,objectClassLoc,&objectClass,SHADER_UNIFORM_INT);
    if(game->bombTimer>0) DrawDetailedBomb(game);
    if(game->beamTimer>0) {
        rlDrawRenderBatchActive();
        objectClass=3;
        SetShaderValue(craftShader,objectClassLoc,&objectClass,SHADER_UNIFORM_INT);
        SetShaderValue(craftShader,objectOriginLoc,&game->player.position,SHADER_UNIFORM_VEC3);
        DrawDetailedBeam(game);
        rlDrawRenderBatchActive();
        objectClass=2;
        SetShaderValue(craftShader,objectClassLoc,&objectClass,SHADER_UNIFORM_INT);
    }
    for (int i = 0; i < MAX_COMBAT_PARTICLES; i++) {
        const CombatParticle *particle = &game->particles[i];
        if (!particle->active) continue;
        float scale = 0.08f + 0.24f * (particle->life / particle->maxLife);
        float speedScale = 0.018f + 0.025f * (particle->life / particle->maxLife);
        Vector3 tail = { particle->position.x - particle->velocity.x * speedScale,
                         particle->position.y - particle->velocity.y * speedScale,
                         particle->position.z - particle->velocity.z * speedScale };
        DrawLine3D(tail, particle->position,
                   (Color){ particle->color.r, particle->color.g,
                            particle->color.b, 150 });
        DrawCylinderEx(tail,particle->position,0.008f,scale*0.32f,4,
                       (Color){particle->color.r,particle->color.g,particle->color.b,200});
        DrawEnergyHalo(particle->position,scale*2.2f,
                       (Color){particle->color.r,particle->color.g,particle->color.b,100});
        if (particle->shockwave) {
            float age = 1.0f - particle->life / particle->maxLife;
            DrawEnergyHalo(particle->origin,1.1f+age*4.5f,
                           (Color){particle->color.r,particle->color.g,particle->color.b,
                                   (unsigned char)(110.0f*(1.0f-age)*(1.0f-age))});
            unsigned char alpha = (unsigned char)((1.0f - age) * 135.0f);
            DrawCircle3D(particle->origin, 0.35f + age * 3.8f,
                         (Vector3){ 1.0f, 0.0f, 0.0f }, 90.0f,
                         (Color){ particle->color.r, particle->color.g,
                                  particle->color.b, alpha });
            DrawCircle3D(particle->origin, 0.22f + age * 2.7f,
                         (Vector3){ 0.0f, 1.0f, 0.0f }, 90.0f,
                         (Color){ 220, 245, 255, (unsigned char)(alpha / 2) });
        }
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndBlendMode();
}

static void DrawHudPanel(int x, int y, int width, int height, Color accent) {
    DrawRectangle(x, y, width, height, (Color){ 3, 8, 20, 178 });
    DrawRectangleGradientV(x + 1, y + 1, width - 2, height - 2,
                           (Color){ accent.r, accent.g, accent.b, 18 },
                           (Color){ 2, 5, 14, 32 });
    const int corner = 13;
    DrawLine(x, y, x + corner, y, accent);
    DrawLine(x, y, x, y + corner, accent);
    DrawLine(x + width - corner, y, x + width, y, accent);
    DrawLine(x + width, y, x + width, y + corner, accent);
    DrawLine(x, y + height - corner, x, y + height, accent);
    DrawLine(x, y + height, x + corner, y + height, accent);
    DrawLine(x + width - corner, y + height, x + width, y + height, accent);
    DrawLine(x + width, y + height - corner, x + width, y + height, accent);
}

static void DrawSegmentMeter(int x, int y, int width, int height, float amount,
                             int segments, Color active, Color inactive) {
    amount = ClampFloat(amount, 0.0f, 1.0f);
    int gap = 2;
    int segmentWidth = (width - gap * (segments - 1)) / segments;
    for (int i = 0; i < segments; i++) {
        float threshold = (float)(i + 1) / (float)segments;
        Color color = amount + 0.0001f >= threshold ? active : inactive;
        DrawRectangle(x + i * (segmentWidth + gap), y, segmentWidth, height, color);
    }
}

void DrawGameplayHUD(const GameplaySystem *game, Camera3D camera, int screenWidth, int screenHeight) {
    if (game->boss.active && game->boss.phase == BOSS_APPROACH) {
        float progress = ClampFloat(game->boss.phaseTime / 2.6f, 0.0f, 1.0f);
        float envelope = sinf(progress * 3.14159265f);
        int barHeight = (int)(envelope * 62.0f);
        unsigned char alpha = (unsigned char)(envelope * 235.0f);
        DrawRectangle(0, 0, screenWidth, barHeight, (Color){ 1, 2, 8, 245 });
        DrawRectangle(0, screenHeight - barHeight, screenWidth, barHeight,
                      (Color){ 1, 2, 8, 245 });
        int scanY = (int)((float)screenHeight * progress);
        DrawRectangle(0, scanY - 1, screenWidth, 3,
                      (Color){ game->secondaryColor.r, game->secondaryColor.g,
                               game->secondaryColor.b, alpha });
        const char *compileText = "HOSTILE RECURSION // COMPILING ARENA";
        DrawText(compileText, screenWidth / 2 - MeasureText(compileText, 16) / 2,
                 screenHeight - barHeight + 18, 16,
                 (Color){ game->primaryColor.r, game->primaryColor.g,
                          game->primaryColor.b, alpha });
    }
    const int barX = 20;
    const int barY = 18;
    const int barWidth = 286;
    Color energyColor = game->player.energy < 28.0f
        ? (Color){ 255, 50, 105, 255 }
        : (Color){ 0, 235, 190, 255 };
    DrawHudPanel(barX, barY, barWidth, 62,
                 (Color){ game->primaryColor.r, game->primaryColor.g,
                          game->primaryColor.b, 145 });
    DrawText("CORE // ENERGY", barX + 12, barY + 9, 13,
             (Color){ 155, 205, 220, 235 });
    const char *energyText = TextFormat("%03d", (int)game->player.energy);
    DrawText(energyText, barX + barWidth - MeasureText(energyText, 15) - 12,
             barY + 7, 15, energyColor);
    DrawSegmentMeter(barX + 12, barY + 34, barWidth - 24, 10,
                     game->player.energy / 100.0f, 18, energyColor,
                     (Color){ 18, 33, 49, 205 });
    DrawLine(barX + 12, barY + 50, barX + 76, barY + 50,
             (Color){ game->secondaryColor.r, game->secondaryColor.g,
                      game->secondaryColor.b, 95 });

    int linkPanelY = barY + 70;
    Color linkColor = game->chainLinks >= BOMB_LINK_COST ? game->hotColor
                    : game->chainLinks >= BEAM_LINK_COST ? game->secondaryColor
                    : (Color){ 95, 145, 180, 235 };
    DrawHudPanel(barX, linkPanelY, barWidth, 58,
                 (Color){ game->hotColor.r, game->hotColor.g, game->hotColor.b, 125 });
    DrawText("CHAIN LINKS", barX + 12, linkPanelY + 8, 13,
             (Color){ 155, 205, 220, 235 });
    const char *linkText = TextFormat("%03d", game->chainLinks);
    DrawText(linkText, barX + barWidth - MeasureText(linkText, 15) - 12,
             linkPanelY + 7, 15, linkColor);
    DrawSegmentMeter(barX + 12, linkPanelY + 30, barWidth - 24, 9,
                     (float)game->chainLinks / (float)MAX_CHAIN_LINKS, 20, linkColor,
                     (Color){ 18, 33, 49, 205 });
    const char *specialText = game->chainLinks >= BOMB_LINK_COST ? "K BEAM 50 // L BOMB READY"
                            : game->chainLinks >= BEAM_LINK_COST ? "K BEAM READY // L BOMB 100"
                            : "K BEAM 50 // L BOMB 100";
    DrawText(specialText, barX + 12, linkPanelY + 43, 10,
             (Color){ linkColor.r, linkColor.g, linkColor.b, 220 });

    const char *scoreText = TextFormat("SCORE %08d", game->score);
    int rightPanelWidth = 290;
    int rightPanelX = screenWidth - rightPanelWidth - 20;
    int rightPanelHeight = game->weaponTier < 3 ? 100 : 82;
    DrawHudPanel(rightPanelX, 18, rightPanelWidth, rightPanelHeight,
                 (Color){ game->secondaryColor.r, game->secondaryColor.g,
                          game->secondaryColor.b, 145 });
    DrawText(scoreText, rightPanelX + rightPanelWidth - MeasureText(scoreText, 22) - 12,
             26, 22, (Color){ 195, 230, 240, 255 });
    if (game->combo > 1) {
        const char *comboText = TextFormat("x%d CHAIN", game->combo);
        DrawText(comboText, rightPanelX + 12, 28, 17, game->hotColor);
    }

    static const char *weaponNames[4] = {
        "PULSE", "ACCELERATOR", "MULTILOCK-6", "OVERDRIVE"
    };
    static const int nextThreshold[4] = { 4, 8, 12, 12 };
    const char *weaponText = TextFormat("WEAPON  %s", weaponNames[game->weaponTier]);
    int weaponY = 55;
    Color weaponColor = game->weaponTier == 0 ? (Color){ 120, 175, 205, 235 }
                        : (game->weaponTier >= 3 ? game->hotColor : game->secondaryColor);
    DrawText(weaponText, rightPanelX + rightPanelWidth - MeasureText(weaponText, 15) - 12,
             weaponY, 15, weaponColor);
    if (game->weaponTier < 3) {
        const char *nextText = TextFormat("NEXT UPLINK  x%d", nextThreshold[game->weaponTier]);
        DrawText(nextText, rightPanelX + rightPanelWidth - MeasureText(nextText, 11) - 12,
                 weaponY + 22, 11, (Color){ 100, 145, 175, 210 });
    }

    int campaignY = 18 + rightPanelHeight + 8;
    DrawHudPanel(rightPanelX, campaignY, rightPanelWidth, 50,
                 (Color){ game->primaryColor.r, game->primaryColor.g,
                          game->primaryColor.b, 112 });
    const char *campaignText = TextFormat("DEPTH %d  //  ROUND %d/%d",
                                          GetGameplayRecursionDepth(game),
                                          GetGameplayRound(game), BOSSES_PER_RECURSION);
    DrawText(campaignText, rightPanelX + 12, campaignY + 8, 13,
             (Color){ 160, 205, 220, 235 });
    float proximity = (float)(GetGameplayRound(game) - 1) /
                      (float)(BOSSES_PER_RECURSION - 1);
    DrawSegmentMeter(rightPanelX + 12, campaignY + 29, rightPanelWidth - 24, 7,
                     proximity, BOSSES_PER_RECURSION - 1, game->hotColor,
                     (Color){ 24, 34, 43, 205 });
    DrawText(GetGameplayRound(game) == BOSSES_PER_RECURSION ? "EVENT HORIZON" : "SINGULARITY RANGE",
             rightPanelX + 12, campaignY + 38, 9,
             (Color){ 105, 145, 165, 205 });

    if (game->boss.active && game->boss.coreSlot >= 0) {
        const Enemy *core = &game->enemies[game->boss.coreSlot];
        static const char *phaseNames[] = {
            "DORMANT", "INBOUND", "FIREWALL", "CORE EXPOSED", "ENRAGED", "SINGULARITY"
        };
        const int bossBarWidth = 420;
        int bossBarX = screenWidth / 2 - bossBarWidth / 2;
        float healthRatio = core->active && game->boss.maxHealth > 0.0f
            ? ClampFloat(core->health / game->boss.maxHealth, 0.0f, 1.0f) : 0.0f;
        static const char *bossNames[BOSSES_PER_RECURSION] = {
            "RECURSIVE CORE", "IRON BASTION", "PRISM WRAITH",
            "SIEGE MANTIS", "FRACTAL LEVIATHAN"
        };
        const char *bossTitle = TextFormat("%s // %s",
            bossNames[game->boss.encounterIndex % BOSSES_PER_RECURSION],
            phaseNames[game->boss.phase]);
        DrawHudPanel(bossBarX - 16, 17, bossBarWidth + 32, 64,
                     (Color){ game->hotColor.r, game->hotColor.g,
                              game->hotColor.b, 150 });
        DrawText(bossTitle, screenWidth / 2 - MeasureText(bossTitle, 16) / 2,
                 25, 16, game->hotColor);
        DrawSegmentMeter(bossBarX, 49, bossBarWidth, 9, healthRatio, 28,
                         game->hotColor, (Color){ 30, 14, 34, 210 });
        if (game->boss.phase == BOSS_APPROACH) {
            const char *lockingText = game->boss.leviathan
                ? "EVENT HORIZON LOCK // GRAVITY FIELD RISING"
                : "CORE SIGNATURE LOCKING";
            DrawText(lockingText, screenWidth / 2 - MeasureText(lockingText, 13) / 2,
                     64, 12, game->primaryColor);
        } else if (game->boss.shieldNodes > 0) {
            const char *shieldText = TextFormat(game->boss.leviathan
                                                ? "BREAK HORIZON ANCHORS  %d/4"
                                                : "DESTROY FIREWALL NODES  %d/4",
                                                game->boss.shieldNodes);
            DrawText(shieldText, screenWidth / 2 - MeasureText(shieldText, 13) / 2,
                     64, 12, game->primaryColor);
        }
    }

    Vector2 reticle = GetWorldToScreen(
        (Vector3){ game->player.position.x, game->player.position.y, -34.0f }, camera);
    float reticleRadius = 8.0f + ClampFloat(game->player.chargeTime, 0.0f, 1.0f) * 25.0f;
    Color reticleColor = game->player.chargeTime > TAP_THRESHOLD
        ? (Color){ 255, 225, 65, 255 }
        : (Color){ 0, 235, 255, 255 };
    for (int quadrant = 0; quadrant < 4; quadrant++) {
        float angle = (float)quadrant * 90.0f + 18.0f;
        DrawRing(reticle, reticleRadius - 1.0f, reticleRadius + 1.0f,
                 angle, angle + 38.0f, 8, reticleColor);
    }
    DrawLine((int)reticle.x - 14, (int)reticle.y, (int)reticle.x - 5, (int)reticle.y, reticleColor);
    DrawLine((int)reticle.x + 5, (int)reticle.y, (int)reticle.x + 14, (int)reticle.y, reticleColor);
    DrawLine((int)reticle.x, (int)reticle.y - 14, (int)reticle.x, (int)reticle.y - 5, reticleColor);
    DrawLine((int)reticle.x, (int)reticle.y + 5, (int)reticle.x, (int)reticle.y + 14, reticleColor);

    if (game->player.chargeTime > TAP_THRESHOLD) {
        int chargeWidth = (int)(ClampFloat(game->player.chargeTime / 1.2f, 0.0f, 1.0f) * 180.0f);
        DrawRectangle(screenWidth / 2 - 92, screenHeight - 70, 184, 10, (Color){ 10, 20, 34, 230 });
        DrawRectangle(screenWidth / 2 - 90, screenHeight - 68, chargeWidth, 6,
                      (Color){ 255, 205, 45, 255 });
        int lockCapacity = game->weaponTier >= 2 ? 6 : 4;
        const char *lockText = TextFormat("LOCKS %d/%d", game->lockedCount, lockCapacity);
        DrawText(lockText, screenWidth / 2 - MeasureText(lockText, 16) / 2,
                 screenHeight - 94, 16, reticleColor);
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *enemy = &game->enemies[i];
        if (!enemy->active || !enemy->locked) continue;
        Vector2 marker = GetWorldToScreen(enemy->position, camera);
        DrawText("LOCK", (int)marker.x - 19, (int)marker.y - 30, 12,
                 (Color){ 255, 225, 65, 255 });
    }

    float helpFade = ClampFloat((12.0f - game->runTime) / 3.0f, 0.0f, 1.0f);
    if (helpFade > 0.001f) {
        const char *help = "MOVE WASD / ARROWS   FIRE J / MOUSE   BEAM K   BOMB L   BOOST SPACE";
        unsigned char alpha = (unsigned char)(helpFade * 195.0f);
        int helpWidth = MeasureText(help, 14);
        DrawRectangle(18, screenHeight - 38, helpWidth + 22, 25,
                      (Color){ 2, 7, 18, (unsigned char)(helpFade * 130.0f) });
        DrawLine(18, screenHeight - 38, 56, screenHeight - 38,
                 (Color){ game->primaryColor.r, game->primaryColor.g,
                          game->primaryColor.b, alpha });
        DrawText(help, 29, screenHeight - 32, 14,
                 (Color){ 135, 175, 205, alpha });
    }

    if (game->weaponFlash > 0.0f) {
        unsigned char alpha = (unsigned char)(ClampFloat(game->weaponFlash, 0.0f, 1.0f) * 235.0f);
        const char *upgrade = TextFormat("CHAIN WEAPON // %s", weaponNames[game->weaponTier]);
        int width = MeasureText(upgrade, 24);
        DrawRectangle(screenWidth / 2 - width / 2 - 18, 112, width + 36, 43,
                      (Color){ 3, 8, 20, (unsigned char)(alpha * 3 / 4) });
        DrawRectangleLines(screenWidth / 2 - width / 2 - 18, 112, width + 36, 43,
                           (Color){ weaponColor.r, weaponColor.g, weaponColor.b, alpha });
        DrawText(upgrade, screenWidth / 2 - width / 2, 122, 24,
                 (Color){ weaponColor.r, weaponColor.g, weaponColor.b, alpha });
    }

    if (game->boss.introFlash > 0.0f) {
        unsigned char alpha = (unsigned char)(game->boss.introFlash * 230.0f);
        const char *warning = game->boss.leviathan
            ? "FRACTAL LEVIATHAN // EVENT HORIZON"
            : "RECURSIVE CORE DETECTED";
        DrawText(warning, screenWidth / 2 - MeasureText(warning, 30) / 2,
                 174, 30, (Color){ game->hotColor.r, game->hotColor.g,
                                    game->hotColor.b, alpha });
    } else if (game->boss.defeatFlash > 0.0f) {
        unsigned char alpha = (unsigned char)(game->boss.defeatFlash * 230.0f);
        const char *clear = game->boss.leviathan
            ? "LEVIATHAN COLLAPSED // DEEPER RECURSION UNLOCKED"
            : "ROUND CLEARED // DESCENT CONTINUES";
        DrawText(clear, screenWidth / 2 - MeasureText(clear, 25) / 2,
                 174, 25, (Color){ game->primaryColor.r, game->primaryColor.g,
                                    game->primaryColor.b, alpha });
    }

    if (game->hitFlash > 0.0f) {
        unsigned char alpha = (unsigned char)(ClampFloat(game->hitFlash, 0.0f, 1.0f) * 145.0f);
        Color damage = (Color){ 255, 18, 82, alpha };
        Color clear = (Color){ 255, 18, 82, 0 };
        DrawRectangle(0, 0, screenWidth, screenHeight,
                      (Color){ 255, 18, 82, (unsigned char)(alpha / 9) });
        DrawRectangleGradientH(0, 0, 190, screenHeight, damage, clear);
        DrawRectangleGradientH(screenWidth - 190, 0, 190, screenHeight, clear, damage);
        DrawRectangleGradientV(0, 0, screenWidth, 105, damage, clear);
        DrawRectangleGradientV(0, screenHeight - 105, screenWidth, 105, clear, damage);
    }
    if (game->overdriveActive && !game->gameOver) {
        const char *warning="BOOST // GUIDED FIRE // HIT DAMAGE +15%";
        int x=screenWidth-MeasureText(warning,13)-32;
        int y=screenHeight-127;
        DrawRectangle(x-9,y-4,screenWidth-x-11,24,(Color){27,16,16,222});
        DrawRectangleLines(x-9,y-4,screenWidth-x-11,24,(Color){185,94,77,220});
        DrawText(warning,x,y,13,(Color){242,175,130,255});
    }
    if (game->gameOver) {
        DrawRectangle(0, 0, screenWidth, screenHeight, (Color){ 2, 4, 12, 205 });
        const char *title = "CORE DESYNCHRONIZED";
        const char *restart = "PRESS R TO RECOMPILE RUN";
        DrawText(title, screenWidth / 2 - MeasureText(title, 38) / 2,
                 screenHeight / 2 - 48, 38, (Color){ 255, 45, 125, 255 });
        DrawText(restart, screenWidth / 2 - MeasureText(restart, 20) / 2,
                 screenHeight / 2 + 18, 20, (Color){ 180, 225, 240, 255 });
    }
}
