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
#define BOSSES_PER_RECURSION 5
#define MAX_CHAIN_LINKS 100
#define BEAM_LINK_COST 50
#define BOMB_LINK_COST 100
#define UPGRADE_COUNT 13
#define UPGRADE_MAX_LEVEL 3

typedef enum {
    UPG_PIERCE, UPG_FOCUS, UPG_BRANCH, UPG_CAPACITOR, UPG_EFFICIENCY,
    UPG_AGILITY, UPG_GRAZE, UPG_CHAIN, UPG_WIDELOCK, UPG_TWIN,
    UPG_QUICKCHARGE, UPG_BOMB_GUARD, UPG_HARVEST
} UpgradeType;

typedef enum { MUT_NONE, MUT_CROSSFIRE, MUT_GRAVITY, MUT_ESCORTS, MUT_COUNT } RecursionModifier;

typedef enum {
    ENEMY_DRIFTER = 0,
    ENEMY_CHASER,
    ENEMY_SPLITTER,
    ENEMY_BOSS_NODE,
    ENEMY_BOSS_CORE,
    ENEMY_SNIPER, ENEMY_CARRIER, ENEMY_SUPPORT
} EnemyType;

typedef enum {
    ENEMY_APPROACH = 0,
    ENEMY_HOVER,
    ENEMY_TELEGRAPH,
    ENEMY_ATTACK,
    ENEMY_WITHDRAW
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
    Vector3 attackAim;
} Enemy;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float life;
    bool homing; // Mild boost guidance; retains lock until the shot expires.
    bool active;
    bool grazed;
    bool dissipating; // Boss-arrival cleanup: visible, harmless, no graze rewards.
    unsigned char kind; // 0 regular, 1 sniper, 2 boss, 3 large boss lattice; patterns never home.
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
    int pierces;
    uint64_t hitMask;
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
    float fireHoldTime;
    float bank;
    bool fireHeldLast;
    float dodgeTimer;
    float dodgeCooldown;
    Vector2 dodgeDirection;
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
    bool recursionCompleted;
    bool attackWarning;
    bool dodged;
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
    float encounterTime;
    bool warningArmed;
    Vector3 attackAim;
    float safeLane;
    float prismFrom[3], prismTo[3]; // Identity 0 is real; slots move, identity/health do not.
    float prismBlend;
    unsigned int prismShuffles;
    bool arrivalPending;
    float arrivalTime;
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
    float activeTime;
    float roundTime;
    int bestCombo;
    int hitsTaken;
    int nearMisses;
    int shotsFired;
    int chargeBursts;
    int dodges;
    int upgrades[UPGRADE_COUNT];
    int upgradeOffers[3];
    bool upgradePending;
    bool practice;
    RecursionModifier modifier;
    float modifierBanner;
    float grazeFlash;
    Vector2 hitDirection;
    int lastHitSource; // 0 projectile, 1 guided, 2 sniper, 3 boss, 4 collision.
    Vector3 beamBranches[3];
    int beamBranchCount;
} GameplaySystem;

void InitGameplay(GameplaySystem *game, uint32_t runSeed);
void AdvanceGameplayLoop(GameplaySystem *game, uint32_t runSeed, double virtualPlayerZ);
int GetGameplayRound(const GameplaySystem *game);
int GetGameplayRecursionDepth(const GameplaySystem *game);
bool CanGameplayBoost(const GameplaySystem *game);
float GetGameplayTravelSpeed(const GameplaySystem *game, double virtualPlayerZ,
                             bool boosting);
GameplayEvents UpdateGameplay(GameplaySystem *game, float dt, double virtualPlayerZ,
                              bool boosting);
void UpdateGameplayCamera(const GameplaySystem *game, Camera3D *camera, float dt, bool boosting);
void DrawGameplay3D(const GameplaySystem *game, Shader craftShader, int objectClassLoc,
                    int enemyTypeLoc, int objectOriginLoc);
void DrawGameplayHUD(const GameplaySystem *game, Camera3D camera, int screenWidth, int screenHeight);
bool ValidateGameplaySpecials(void);
bool ValidatePlayability(void);
void OfferGameplayUpgrade(GameplaySystem *game);
bool ChooseGameplayUpgrade(GameplaySystem *game, int choice);
const char *GameplayUpgradeName(int upgrade);
const char *GameplayUpgradeDescription(int upgrade);
const char *GameplayUpgradePreview(const GameplaySystem *game,int upgrade);
const char *GameplayModifierName(RecursionModifier modifier);
int GameplayBeamCost(const GameplaySystem *game);
int GameplayBombCost(const GameplaySystem *game);

#endif // GAMEPLAY_H
