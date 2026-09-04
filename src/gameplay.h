#ifndef GAMEPLAY_H
#define GAMEPLAY_H

#include "raylib.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_ENEMIES 48
#define MAX_ENEMY_PROJECTILES 128
#define MAX_PLAYER_PROJECTILES 32
#define MAX_COMBAT_PARTICLES 96
#define MAX_LOCK_TARGETS 6
#define CORE_TRANSITION_DURATION 5.2f
#define MAX_CHAIN_LINKS 100
#define BEAM_LINK_COST 50
#define BOMB_LINK_COST 100

typedef enum {
    ENEMY_DRIFTER = 0,
    ENEMY_CHASER,
    ENEMY_SPLITTER,
    ENEMY_BOSS_NODE,
    ENEMY_BOSS_CORE
} EnemyType;

typedef enum {
    ENEMY_APPROACH = 0,
    ENEMY_HOVER,
    ENEMY_TELEGRAPH,
    ENEMY_ATTACK
} EnemyPhase;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    Vector3 basePosition;
    Vector3 size;
    EnemyType type;
    EnemyPhase phase;
    float age;
    float phaseTime;
    float health;
    float fireTimer;
    int identity;
    int generation;
    bool active;
    bool locked;
} Enemy;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float life;
    bool active;
} EnemyProjectile;

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
    float chargeTime;
    float bank;
    bool fireHeldLast;
} PlayerState;

typedef struct {
    int tapShots;
    int chargeShots;
    int enemiesDestroyed;
    bool beamFired;
    bool bombFired;
    bool playerHit;
    bool weaponTierAdvanced;
    bool bossStarted;
    bool bossDefeated;
} GameplayEvents;

typedef enum {
    BOSS_DORMANT = 0,
    BOSS_APPROACH,
    BOSS_SHIELDED,
    BOSS_EXPOSED,
    BOSS_ENRAGED
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
    BossPhase phase;
    bool active;
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
    float beamTimer;
    float bombTimer;
    float loopTransitionTimer;
    float runTime;
    int weaponTier;
    bool suppressLinkRewards;
    bool overdriveActive;
    bool gameOver;
} GameplaySystem;

void InitGameplay(GameplaySystem *game, uint32_t runSeed);
void AdvanceGameplayLoop(GameplaySystem *game, uint32_t runSeed, double virtualPlayerZ);
bool CanGameplayBoost(const GameplaySystem *game);
float GetGameplayTravelSpeed(const GameplaySystem *game, double virtualPlayerZ,
                             bool boosting);
GameplayEvents UpdateGameplay(GameplaySystem *game, float dt, double virtualPlayerZ,
                              bool boosting);
void UpdateGameplayCamera(const GameplaySystem *game, Camera3D *camera, float dt, bool boosting);
void DrawGameplay3D(const GameplaySystem *game, Shader craftShader, int objectClassLoc,
                    int enemyTypeLoc);
void DrawGameplayHUD(const GameplaySystem *game, Camera3D camera, int screenWidth, int screenHeight);
bool ValidateGameplaySpecials(void);

#endif // GAMEPLAY_H
