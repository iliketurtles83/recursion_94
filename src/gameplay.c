#include "gameplay.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define TAP_THRESHOLD 0.12f
#define PLAYER_MIN_X -10.0f
#define PLAYER_MAX_X 10.0f
#define PLAYER_MIN_Y 0.8f
#define PLAYER_MAX_Y 7.0f
#define PLAYER_BOLT_SPEED 85.0f
#define PLAYER_BALL_SPEED 52.0f
#define PLAYER_HOMING_TURN_RATE 7.5f
#define PLAYER_PROJECTILE_LIFE 3.0f

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

static float LoopPressure(const GameplaySystem *game) {
    return fminf((float)game->boss.encounterIndex * 0.12f, 0.36f);
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
    static const Color palettes[][3] = {
        { { 0, 235, 255, 255 }, { 255, 35, 170, 255 }, { 255, 230, 70, 255 } },
        { { 80, 255, 150, 255 }, { 130, 75, 255, 255 }, { 255, 105, 55, 255 } },
        { { 100, 155, 255, 255 }, { 255, 70, 210, 255 }, { 120, 255, 245, 255 } },
        { { 255, 125, 45, 255 }, { 35, 225, 255, 255 }, { 255, 245, 120, 255 } },
        { { 185, 75, 255, 255 }, { 20, 255, 195, 255 }, { 255, 80, 120, 255 } },
        { { 70, 215, 255, 255 }, { 255, 80, 95, 255 }, { 190, 255, 70, 255 } }
    };
    int palette = (int)(MixBits(game->runSeed ^ 0x94d31a7bu) % 6u);
    game->primaryColor = palettes[palette][0];
    game->secondaryColor = palettes[palette][1];
    game->hotColor = palettes[palette][2];
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
            enemy->size = (Vector3){ 1.8f, 1.8f, 1.8f };
            enemy->health = 2.0f + game->boss.encounterIndex * 0.5f;
            break;
        case ENEMY_BOSS_CORE:
            enemy->size = (Vector3){ 6.2f, 6.2f, 6.2f };
            enemy->health = 34.0f + game->boss.encounterIndex * 14.0f;
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
    if (count > 4) count = 4;

    float centerX = -7.5f + Hash01(seed + 3u) * 15.0f;
    float centerY = 1.6f + Hash01(seed + 4u) * 4.6f;
    for (int i = 0; i < count; i++) {
        float typeRoll = Hash01(seed + 11u + (uint32_t)i * 7u);
        EnemyType type = ENEMY_DRIFTER;
        if (game->difficulty > 0.08f && typeRoll > 0.50f) type = ENEMY_CHASER;
        if (game->difficulty > 0.25f && typeRoll > 0.80f) type = ENEMY_SPLITTER;

        float offset = ((float)i - (float)(count - 1) * 0.5f) * 3.2f;
        Vector3 position = {
            ClampFloat(centerX + offset, -9.0f, 9.0f),
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

static void SpawnBoss(GameplaySystem *game, float virtualPlayerZ, GameplayEvents *events) {
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
    boss->phase = BOSS_APPROACH;
    boss->phaseTime = 0.0f;
    boss->fireTimer = 1.1f;
    boss->reinforcementTimer = 3.4f;
    boss->rotation = Hash01(encounterSeed) * 6.2831853f;
    boss->position = (Vector3){ 0.0f, 4.1f, -125.0f };
    boss->shieldNodes = 4;
    boss->introFlash = 1.0f;
    boss->defeatFlash = 0.0f;
    boss->coreSlot = -1;
    for (int node = 0; node < 4; node++) boss->nodeSlots[node] = -1;
    boss->nextSpawnDistance = virtualPlayerZ + 2250.0f + Hash01(encounterSeed ^ 0x51c3u) * 500.0f;
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

static void UpdateBoss(GameplaySystem *game, float dt, float virtualPlayerZ,
                       GameplayEvents *events) {
    BossState *boss = &game->boss;
    if (!boss->active) {
        if (virtualPlayerZ >= boss->nextSpawnDistance) SpawnBoss(game, virtualPlayerZ, events);
        return;
    }

    boss->phaseTime += dt;
    boss->rotation += dt * (boss->phase == BOSS_ENRAGED ? 1.85f : 1.05f);
    boss->position.z = Approach(boss->position.z, -46.0f, 1.15f, dt);
    boss->position.x = sinf(game->runTime * 0.31f + (float)boss->encounterIndex) * 2.2f;
    boss->position.y = 4.1f + sinf(game->runTime * 0.57f) * 0.65f;

    if (boss->phase == BOSS_APPROACH && boss->phaseTime >= 2.6f) {
        boss->phase = BOSS_SHIELDED;
        boss->phaseTime = 0.0f;
        boss->fireTimer = 0.75f;
    }

    Enemy *core = boss->coreSlot >= 0 ? &game->enemies[boss->coreSlot] : NULL;
    if (core != NULL && core->active) {
        core->position = boss->position;
        core->basePosition = boss->position;
        if (boss->shieldNodes == 0 && core->health <= boss->maxHealth * 0.46f &&
            boss->phase != BOSS_ENRAGED) {
            boss->phase = BOSS_ENRAGED;
            boss->phaseTime = 0.0f;
            boss->fireTimer = 0.2f;
        }
    }

    for (int node = 0; node < 4; node++) {
        int slot = boss->nodeSlots[node];
        if (slot < 0 || !game->enemies[slot].active) continue;
        float angle = boss->rotation + (float)node * 1.5707963f;
        game->enemies[slot].position = (Vector3){
            boss->position.x + cosf(angle) * 8.35f,
            boss->position.y + sinf(angle) * 4.65f,
            boss->position.z + sinf(angle * 0.5f) * 2.1f
        };
        game->enemies[slot].basePosition = game->enemies[slot].position;
    }

    if (boss->phase == BOSS_APPROACH) return;

    boss->reinforcementTimer -= dt;
    if (boss->reinforcementTimer <= 0.0f) {
        int reinforcements = boss->phase == BOSS_ENRAGED ? 2 : 1;
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
        boss->reinforcementTimer = boss->phase == BOSS_ENRAGED ? 2.7f : 3.8f;
    }

    boss->fireTimer -= dt;
    if (boss->fireTimer > 0.0f) return;

    Vector3 origin = boss->position;
    int shotCount = boss->phase == BOSS_ENRAGED ? 5
                    : (boss->phase == BOSS_EXPOSED ? 3 : 2);
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
                            (boss->phase == BOSS_ENRAGED ? 4.0f : 0.0f);
    for (int shot = 0; shot < shotCount; shot++) {
        float spread = ((float)shot - (float)(shotCount - 1) * 0.5f) * 1.45f;
        SpawnProjectile(game, origin, projectileSpeed, spread);
    }

    float cooldown = boss->phase == BOSS_ENRAGED ? 0.48f - game->difficulty * 0.16f
                     : (boss->phase == BOSS_EXPOSED ? 0.84f - game->difficulty * 0.28f
                                                    : 1.10f - game->difficulty * 0.34f);
    boss->fireTimer = fmaxf(cooldown, 0.28f);
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
        game->boss.active = false;
        game->boss.phase = BOSS_DORMANT;
        game->boss.defeatFlash = 1.0f;
        game->boss.encounterIndex++;
        game->player.energy = 100.0f;
        game->score += 10000 * game->boss.encounterIndex;
        events->bossDefeated = true;
        for (int node = 0; node < 4; node++) {
            int slot = game->boss.nodeSlots[node];
            if (slot >= 0 && slot < MAX_ENEMIES) game->enemies[slot].active = false;
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
    game->score += 100 * game->combo * (deadType == ENEMY_SPLITTER ? 2 : 1);
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
    game->player.energy = fmaxf(0.0f, game->player.energy - 18.0f);
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

    game->player.velocity.x = Approach(game->player.velocity.x, inputX * 14.0f, 10.0f, dt);
    game->player.velocity.y = Approach(game->player.velocity.y, inputY * 10.0f, 10.0f, dt);
    game->player.position.x += game->player.velocity.x * dt;
    game->player.position.y += game->player.velocity.y * dt;
    game->player.position.x = ClampFloat(game->player.position.x, PLAYER_MIN_X, PLAYER_MAX_X);
    game->player.position.y = ClampFloat(game->player.position.y, PLAYER_MIN_Y, PLAYER_MAX_Y);
    game->player.bank = Approach(game->player.bank, -inputX * 0.7f, 8.0f, dt);

    if (boosting) game->player.energy = fmaxf(0.0f, game->player.energy - 4.5f * dt);
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

static void UpdateEnemy(GameplaySystem *game, Enemy *enemy, float dt, float worldSpeed,
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
            enemy->position.z += (12.0f + worldSpeed * 0.22f) * dt;
            enemy->position.x += sinf(enemy->age * 1.8f + identity) * dt * 1.1f;
            if (enemy->position.z >= holdZ) {
                enemy->position.z = holdZ;
                enemy->basePosition = enemy->position;
                SetEnemyPhase(enemy, ENEMY_HOVER);
            }
            break;

        case ENEMY_HOVER:
            if (enemy->type == ENEMY_DRIFTER) {
                enemy->position.x = enemy->basePosition.x + sinf(enemy->age * 1.45f + identity) * 3.1f;
                enemy->position.y = enemy->basePosition.y + cosf(enemy->age * 1.1f + identity) * 0.9f;
            } else if (enemy->type == ENEMY_SPLITTER) {
                enemy->position.x = enemy->basePosition.x + sinf(enemy->age * 1.7f + identity) * 2.0f;
                enemy->position.y = enemy->basePosition.y + cosf(enemy->age * 2.2f + identity) * 1.4f;
            } else {
                enemy->position.x = Approach(enemy->position.x, game->player.position.x, 0.35f, dt);
                enemy->position.y += sinf(enemy->age * 3.0f + identity) * dt * 0.8f;
            }
            enemy->fireTimer -= dt;
            if (enemy->fireTimer <= 0.0f) SetEnemyPhase(enemy, ENEMY_TELEGRAPH);
            break;

        case ENEMY_TELEGRAPH: {
            float track = enemy->type == ENEMY_CHASER ? 2.8f : 1.4f;
            enemy->position.x = Approach(enemy->position.x, game->player.position.x, track, dt);
            enemy->position.y = Approach(enemy->position.y, game->player.position.y, track, dt);
            float delay = enemy->type == ENEMY_CHASER
                ? 0.62f - game->difficulty * 0.12f
                : 0.90f - game->difficulty * 0.24f;
            if (enemy->phaseTime >= delay) {
                if (enemy->type == ENEMY_CHASER) {
                    float travel = fmaxf(0.2f, -enemy->position.z / (32.0f + game->difficulty * 10.0f));
                    enemy->velocity.x = (game->player.position.x - enemy->position.x) / travel;
                    enemy->velocity.y = (game->player.position.y - enemy->position.y) / travel;
                    enemy->velocity.z = 32.0f + game->difficulty * 10.0f;
                } else if (enemy->type == ENEMY_SPLITTER) {
                    SpawnProjectile(game, enemy->position, 21.0f + game->difficulty * 6.0f, -1.6f);
                    SpawnProjectile(game, enemy->position, 23.0f + game->difficulty * 6.0f, 0.0f);
                    SpawnProjectile(game, enemy->position, 21.0f + game->difficulty * 6.0f, 1.6f);
                } else {
                    SpawnProjectile(game, enemy->position, 22.0f + game->difficulty * 7.0f, 0.0f);
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
                    (1.0f - game->difficulty * 0.52f);
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
        projectile->position.x += projectile->velocity.x * dt;
        projectile->position.y += projectile->velocity.y * dt;
        projectile->position.z += projectile->velocity.z * dt;
        projectile->life -= dt;

        float dx = projectile->position.x - game->player.position.x;
        float dy = projectile->position.y - game->player.position.y;
        float dz = projectile->position.z - game->player.position.z;
        if (fabsf(dz) < 1.0f && LengthSquared2D(dx, dy) < 0.72f) {
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
    game->boss.nextSpawnDistance = 1600.0f + Hash01(runSeed ^ 0xb05594u) * 400.0f;
}

void AdvanceGameplayLoop(GameplaySystem *game, uint32_t runSeed, float virtualPlayerZ) {
    game->runSeed = runSeed;
    SetGameplayPalette(game);

    for (int i = 0; i < MAX_ENEMIES; i++) game->enemies[i].active = false;
    for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) game->projectiles[i].active = false;
    for (int i = 0; i < MAX_PLAYER_PROJECTILES; i++) game->playerProjectiles[i].active = false;
    ClearLocks(game);

    game->boss.coreSlot = -1;
    for (int node = 0; node < 4; node++) game->boss.nodeSlots[node] = -1;
    game->boss.nextSpawnDistance = virtualPlayerZ + 1900.0f +
        Hash01(runSeed ^ (uint32_t)game->boss.encounterIndex * 0x51c3u) * 400.0f;
    game->spawnTimer = 0.45f;
    game->loopTransitionTimer = 3.0f;
}

bool CanGameplayBoost(const GameplaySystem *game) {
    return !game->gameOver && game->player.energy > 1.0f;
}

GameplayEvents UpdateGameplay(GameplaySystem *game, float dt, float virtualPlayerZ,
                              float worldSpeed, bool boosting) {
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

    UpdatePlayer(game, dt, boosting, &events);
    game->difficulty = ClampFloat(log1pf(fmaxf(virtualPlayerZ, 0.0f) / 350.0f) *
                                  0.43429448f, 0.0f, 1.0f);
    UpdateBoss(game, dt, virtualPlayerZ, &events);
    if (!game->boss.active) {
        game->spawnTimer -= dt;
        if (game->spawnTimer <= 0.0f) {
            SpawnFormation(game);
            game->spawnTimer = (1.18f - game->difficulty * 0.68f) *
                               (1.0f - LoopPressure(game) * 0.45f);
        }
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (game->enemies[i].active) UpdateEnemy(game, &game->enemies[i], dt, worldSpeed, &events);
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
        case ENEMY_CHASER: return game->hotColor;
        case ENEMY_SPLITTER: return game->secondaryColor;
        case ENEMY_BOSS_NODE: return MixColor(game->primaryColor, game->secondaryColor, 0.5f, 255);
        case ENEMY_BOSS_CORE: return MixColor(game->hotColor, game->secondaryColor, 0.45f, 255);
        default: return game->secondaryColor;
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

static void DrawQuad3D(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color) {
    DrawTriangle3D(a, b, c, color);
    DrawTriangle3D(a, c, d, color);
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
    DrawTriangle3D(at, bt, ct, top);
    DrawTriangle3D(ab, cb, bb, edge);
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

static void DrawPlayerShip(const GameplaySystem *game) {
    Vector3 p = game->player.position;
    Color normalHull = ScaleColor(game->primaryColor, 0.30f, 38);
    Color body = game->player.invulnerability > 0.0f && ((int)(game->runTime * 18.0f) & 1)
        ? (Color){ 180, 225, 240, 255 }
        : normalHull;
    Color hullDark = MixColor(game->primaryColor, (Color){ 6, 11, 25, 255 }, 0.86f, 255);
    Color hullEdge = ScaleColor(game->primaryColor, 0.24f, 14);
    Color canopy = MixColor(game->primaryColor, (Color){ 12, 42, 70, 255 }, 0.66f, 255);
    Color panelMetal = MixColor(game->primaryColor, (Color){ 31, 38, 47, 255 }, 0.80f, 255);
    bool boosting = CanGameplayBoost(game) && IsKeyDown(KEY_SPACE);
    float bankLift = game->player.bank * 0.38f;

    // Multi-stage fuselage forms a continuous arrowhead with a broad engine
    // shoulder, inset keel and a fine nose instead of a single cone.
    DrawCylinderEx((Vector3){ p.x, p.y, p.z + 0.82f },
                   (Vector3){ p.x, p.y + 0.02f, p.z - 0.46f },
                   0.44f, 0.30f, 16, body);
    DrawCylinderEx((Vector3){ p.x, p.y + 0.02f, p.z - 0.42f },
                   (Vector3){ p.x, p.y - 0.01f, p.z - 1.78f },
                   0.31f, 0.025f, 16, ScaleColor(body, 1.10f, 4));
    DrawCylinderEx((Vector3){ p.x, p.y - 0.13f, p.z + 0.52f },
                   (Vector3){ p.x, p.y - 0.08f, p.z - 1.34f },
                   0.28f, 0.035f, 10, hullDark);

    Vector3 nose = { p.x, p.y - 0.01f, p.z - 1.52f };
    Vector3 leftTip = { p.x - 1.86f, p.y - bankLift, p.z + 0.48f };
    Vector3 leftRoot = { p.x - 0.24f, p.y, p.z + 0.38f };
    Vector3 rightRoot = { p.x + 0.24f, p.y, p.z + 0.38f };
    Vector3 rightTip = { p.x + 1.86f, p.y + bankLift, p.z + 0.48f };
    DrawThickTriangle3D(nose, leftTip, leftRoot, 0.13f, body, hullDark);
    DrawThickTriangle3D(nose, rightRoot, rightTip, 0.13f, body, hullDark);

    // Inset wing panels and continuous leading-edge light strips improve the
    // material read without turning the entire vessel into a white glow.
    DrawTriangle3D((Vector3){ p.x - 0.18f, p.y + 0.075f, p.z - 0.70f },
                   (Vector3){ p.x - 1.43f, p.y - bankLift * 0.78f + 0.075f, p.z + 0.34f },
                   (Vector3){ p.x - 0.37f, p.y + 0.075f, p.z + 0.23f }, hullEdge);
    DrawTriangle3D((Vector3){ p.x + 0.18f, p.y + 0.075f, p.z - 0.70f },
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

    DrawSphereEx((Vector3){ p.x, p.y + 0.25f, p.z - 0.48f },
                 0.31f, 8, 16, canopy);
    DrawCylinderEx((Vector3){ p.x, p.y + 0.23f, p.z - 0.56f },
                   (Vector3){ p.x, p.y + 0.19f, p.z - 1.10f },
                   0.22f, 0.07f, 12, canopy);
    DrawLine3D((Vector3){ p.x, p.y + 0.48f, p.z - 0.40f },
               (Vector3){ p.x, p.y + 0.17f, p.z - 1.16f }, game->secondaryColor);

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

    float exhaustLength = boosting ? 1.80f : 0.72f;
    for (int engine = -1; engine <= 1; engine += 2) {
        float engineX = p.x + (float)engine * 0.52f;
        DrawCylinderEx((Vector3){ engineX, p.y - 0.035f, p.z + 0.82f },
                       (Vector3){ engineX - (float)engine * 0.06f, p.y, p.z - 0.30f },
                       0.22f, 0.12f, 12, body);
        DrawCylinderEx((Vector3){ engineX, p.y - 0.035f, p.z + 0.86f },
                       (Vector3){ engineX, p.y - 0.035f, p.z + 0.86f + exhaustLength },
                       0.17f, 0.025f, 10,
                       (Color){ game->secondaryColor.r, game->secondaryColor.g,
                                game->secondaryColor.b, boosting ? 205 : 125 });
        DrawCylinderEx((Vector3){ engineX, p.y - 0.035f, p.z + 0.88f },
                       (Vector3){ engineX, p.y - 0.035f,
                                  p.z + 0.88f + exhaustLength * 0.62f },
                       0.075f, 0.012f, 8, (Color){ 205, 245, 255, 225 });
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
                           0.12f, 0.055f, 8, game->secondaryColor);
        }
    }
    if (game->weaponTier >= 2) {
        DrawCylinderEx((Vector3){ p.x - 0.62f, p.y, p.z - 0.15f },
                       (Vector3){ p.x - 0.62f, p.y, p.z - 0.92f },
                       0.10f, 0.06f, 6, game->hotColor);
        DrawCylinderEx((Vector3){ p.x + 0.62f, p.y, p.z - 0.15f },
                       (Vector3){ p.x + 0.62f, p.y, p.z - 0.92f },
                       0.10f, 0.06f, 6, game->hotColor);
    }
}

static void DrawEnemy(const GameplaySystem *game, const Enemy *enemy, float runTime) {
    Color color = EnemyColor(game, enemy);
    Vector3 p = enemy->position;
    Color armor = MixColor(color, (Color){ 6, 10, 24, 255 }, 0.76f, 255);
    Color armorEdge = ScaleColor(color, 0.36f, 14);
    if (enemy->type == ENEMY_BOSS_CORE) {
        float pulse = sinf(runTime * 5.0f) * 0.5f + 0.5f;
        Color bossArmor = MixColor(game->secondaryColor, (Color){ 8, 11, 19, 255 }, 0.94f, 255);
        Color bossPanel = MixColor(game->primaryColor, (Color){ 34, 42, 50, 255 }, 0.80f, 255);
        Color bossMetal = MixColor(game->secondaryColor, (Color){ 19, 23, 30, 255 }, 0.94f, 255);
        Color bossTrim = MixColor(game->hotColor, (Color){ 8, 11, 22, 255 }, 0.78f, 255);
        float plateSpin = runTime * (game->boss.phase == BOSS_ENRAGED ? 0.36f : 0.14f);
        float plateSpread = game->boss.phase == BOSS_ENRAGED ? 0.72f
                            : (game->boss.phase == BOSS_EXPOSED ? 0.38f : 0.0f);
        float outerRadius = 5.42f + plateSpread;

        // Main pressure vessel and rear machinery establish a heavy axial
        // silhouette before the segmented front armor is layered over it.
        DrawSphereEx(p, 3.55f, 9, 14, bossArmor);
        DrawSphereEx(p, 2.72f, 8, 12, bossPanel);
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
        DrawCylinderEx((Vector3){ p.x, p.y, p.z + 2.15f },
                       (Vector3){ p.x, p.y, p.z + 2.76f },
                       2.20f, 1.68f, 18, bossPanel);
        DrawCylinderEx((Vector3){ p.x, p.y, p.z + 2.72f },
                       (Vector3){ p.x, p.y, p.z + 3.15f },
                       1.46f, 1.04f, 16, bossArmor);
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
    } else if (enemy->type == ENEMY_BOSS_NODE) {
        Color nodeMetal = MixColor(game->secondaryColor, (Color){ 10, 14, 24, 255 }, 0.82f, 255);
        Color nodeArmor = MixColor(color, (Color){ 7, 11, 21, 255 }, 0.90f, 255);
        float nodeSpin = runTime * 0.58f + (float)enemy->generation * 0.67f;
        DrawSphereEx(p, 1.16f, 10, 16, nodeArmor);
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

void DrawGameplay3D(const GameplaySystem *game, Shader craftShader, int objectClassLoc,
                    int enemyTypeLoc) {
    int objectClass = 0;
    if (objectClassLoc >= 0) {
        SetShaderValue(craftShader, objectClassLoc, &objectClass, SHADER_UNIFORM_INT);
    }
    DrawPlayerShip(game);
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
                float radius = 4.0f + (float)ring * 2.25f + (1.0f - progress) * 6.0f;
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
        DrawCircle3D((Vector3){ core.x, core.y, core.z - 3.0f }, 10.5f,
                     (Vector3){ 1.0f, 0.0f, 0.0f }, game->runTime * 8.0f,
                     (Color){ game->primaryColor.r, game->primaryColor.g,
                              game->primaryColor.b, 90 });
        DrawCircle3D((Vector3){ core.x, core.y, core.z - 3.2f }, 13.5f,
                     (Vector3){ 1.0f, 0.0f, 0.0f }, game->runTime * -5.0f,
                     (Color){ game->secondaryColor.r, game->secondaryColor.g,
                              game->secondaryColor.b, 70 });
        for (int node = 0; node < 4; node++) {
            int slot = game->boss.nodeSlots[node];
            if (slot < 0 || !game->enemies[slot].active) continue;
            DrawCylinderEx(core, game->enemies[slot].position, 0.11f, 0.06f, 6,
                           (Color){ game->secondaryColor.r, game->secondaryColor.g,
                                    game->secondaryColor.b, 135 });
        }
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!game->enemies[i].active) continue;
        if (enemyTypeLoc >= 0) {
            int enemyType = (int)game->enemies[i].type;
            SetShaderValue(craftShader, enemyTypeLoc, &enemyType, SHADER_UNIFORM_INT);
        }
        DrawEnemy(game, &game->enemies[i], game->runTime);
    }
    objectClass = 2;
    if (objectClassLoc >= 0) {
        SetShaderValue(craftShader, objectClassLoc, &objectClass, SHADER_UNIFORM_INT);
    }
    for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
        const EnemyProjectile *projectile = &game->projectiles[i];
        if (!projectile->active) continue;
        Vector3 trail = { projectile->position.x - projectile->velocity.x * 0.065f,
                          projectile->position.y - projectile->velocity.y * 0.065f,
                          projectile->position.z - projectile->velocity.z * 0.065f };
        DrawCylinderEx(trail, projectile->position, 0.025f, 0.15f, 7,
                       (Color){ 255, 45, 145, 150 });
        DrawSphereEx(projectile->position, 0.36f, 6, 10,
                     (Color){ 110, 18, 85, 145 });
        DrawSphereEx(projectile->position, 0.18f, 6, 10,
                     (Color){ 255, 105, 195, 255 });
        DrawSphereEx((Vector3){ projectile->position.x, projectile->position.y,
                                projectile->position.z + 0.08f },
                     0.075f, 5, 8, (Color){ 245, 250, 255, 255 });
    }
    for (int i = 0; i < MAX_PLAYER_PROJECTILES; i++) {
        const PlayerProjectile *bolt = &game->playerProjectiles[i];
        if (!bolt->active) continue;
        Color core = bolt->charged ? game->hotColor : game->secondaryColor;
        float pulse = 0.85f + 0.15f * sinf(game->runTime * (bolt->charged ? 14.0f : 22.0f) + (float)i);
        float radius = (bolt->charged ? 0.32f : 0.16f) * pulse;
        Vector3 trail = { bolt->position.x - bolt->velocity.x * (bolt->charged ? 0.05f : 0.035f),
                          bolt->position.y - bolt->velocity.y * (bolt->charged ? 0.05f : 0.035f),
                          bolt->position.z - bolt->velocity.z * (bolt->charged ? 0.05f : 0.035f) };
        DrawCylinderEx(trail, bolt->position, radius * 0.18f, radius * 0.85f, 8,
                       (Color){ core.r, core.g, core.b, 130 });
        DrawSphereEx(bolt->position, radius * 1.6f, 6, 10,
                     (Color){ core.r, core.g, core.b, 90 });
        DrawSphereEx(bolt->position, radius, 6, 10, core);
        DrawSphereEx(bolt->position, radius * 0.42f, 5, 8, (Color){ 245, 250, 255, 255 });
        if (bolt->charged) {
            DrawCircle3D(bolt->position, radius * 2.1f, (Vector3){ 0.0f, 0.0f, 1.0f },
                        game->runTime * 200.0f, (Color){ core.r, core.g, core.b, 120 });
        }
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
        DrawSphereEx(particle->position, scale, 4, 6, particle->color);
        if (particle->shockwave) {
            float age = 1.0f - particle->life / particle->maxLife;
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
    if (game->loopTransitionTimer > 0.0f) {
        float fade = ClampFloat(game->loopTransitionTimer, 0.0f, 1.0f);
        const char *loopText = TextFormat("RECURSION %02d // SEED %u",
                                          game->boss.encounterIndex + 1, game->runSeed);
        int textWidth = MeasureText(loopText, 22);
        DrawRectangle(screenWidth / 2 - textWidth / 2 - 18, 96,
                      textWidth + 36, 42, (Color){ 2, 5, 14, (unsigned char)(190.0f * fade) });
        DrawText(loopText, screenWidth / 2 - textWidth / 2, 106, 22,
                 (Color){ game->primaryColor.r, game->primaryColor.g,
                          game->primaryColor.b, (unsigned char)(255.0f * fade) });
    }
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

    if (game->boss.active && game->boss.coreSlot >= 0) {
        const Enemy *core = &game->enemies[game->boss.coreSlot];
        static const char *phaseNames[] = {
            "DORMANT", "INBOUND", "FIREWALL", "CORE EXPOSED", "ENRAGED"
        };
        const int bossBarWidth = 420;
        int bossBarX = screenWidth / 2 - bossBarWidth / 2;
        float healthRatio = core->active && game->boss.maxHealth > 0.0f
            ? ClampFloat(core->health / game->boss.maxHealth, 0.0f, 1.0f) : 0.0f;
        const char *bossTitle = TextFormat("RECURSIVE CORE // %s",
                                           phaseNames[game->boss.phase]);
        DrawHudPanel(bossBarX - 16, 17, bossBarWidth + 32, 64,
                     (Color){ game->hotColor.r, game->hotColor.g,
                              game->hotColor.b, 150 });
        DrawText(bossTitle, screenWidth / 2 - MeasureText(bossTitle, 16) / 2,
                 25, 16, game->hotColor);
        DrawSegmentMeter(bossBarX, 49, bossBarWidth, 9, healthRatio, 28,
                         game->hotColor, (Color){ 30, 14, 34, 210 });
        if (game->boss.phase == BOSS_APPROACH) {
            const char *lockingText = "CORE SIGNATURE LOCKING";
            DrawText(lockingText, screenWidth / 2 - MeasureText(lockingText, 13) / 2,
                     64, 12, game->primaryColor);
        } else if (game->boss.shieldNodes > 0) {
            const char *shieldText = TextFormat("DESTROY FIREWALL NODES  %d/4",
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
        const char *help = "MOVE  WASD / ARROWS     FIRE  J / LEFT MOUSE     BOOST  SPACE";
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
        const char *warning = "RECURSIVE CORE DETECTED";
        DrawText(warning, screenWidth / 2 - MeasureText(warning, 30) / 2,
                 174, 30, (Color){ game->hotColor.r, game->hotColor.g,
                                    game->hotColor.b, alpha });
    } else if (game->boss.defeatFlash > 0.0f) {
        unsigned char alpha = (unsigned char)(game->boss.defeatFlash * 230.0f);
        const char *clear = "CORE COLLAPSED // ENERGY RESTORED";
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
