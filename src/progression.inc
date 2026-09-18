// Included after seeded helpers in gameplay.c. No allocations or external assets.
static const char *upgradeNames[UPGRADE_COUNT]={
    "Piercing pulse","Focused charge","Branching beam","Link capacitor",
    "Efficient boost","Vector thrusters","Graze collector","Chain memory",
    "Wide acquisition","Twin pulse","Rapid charge","Bomb shelter","Link harvester"
};
static const char *upgradeDescriptions[UPGRADE_COUNT]={
    "+25% pulse damage, including bosses. Pierce one more target per level.",
    "+50% charged damage per level, including bosses. Maximum two locks.",
    "Beam arcs to another nearby target per level, at half damage.",
    "Use beam and bomb more often: lower Link costs every level.",
    "20% less boost drain per level. Guided-fire risk remains.",
    "Faster steering and a shorter evade cooldown every level.",
    "+2 Links per close dodge per level. Each bullet pays once.",
    "Keep weapon tiers longer: +0.6s kill-chain timeout per level.",
    "Wider lock-on cone each level. Catch moving formations more easily.",
    "Two pulse bolts, each at 70%, 85%, then 100% damage. Both hit bosses.",
    "Charge builds 25% faster per level. Lock targets with shorter holds.",
    "Bomb clears bullets and grants 0.4s protection per level.",
    "+1 Link per pulse or charge kill per level. Beam/bomb kills never refund."
};
const char *GameplayUpgradeName(int up) {
    return up>=0&&up<UPGRADE_COUNT?upgradeNames[up]:up==-2?"Chain ignition":up==-3?"Navigation archive":"Link cache";
}
const char *GameplayUpgradeDescription(int up) {
    return up>=0&&up<UPGRADE_COUNT?upgradeDescriptions[up]:up==-2?"Begin the next round with a twelve-kill weapon chain.":
        up==-3?"Archive recovered flight data for 2,500 bonus points.":"Recover 30 Chain Links, up to the normal 100-Link limit.";
}
const char *GameplayModifierName(RecursionModifier modifier) {
    static const char *names[]={"STANDARD SPACE","CROSSFIRE: WIDER ENEMY VOLLEYS","GRAVITY TIDES: GENTLE CROSS-CURRENTS","ESCORT FLEETS: SUPPORT SHIPS"};
    return names[modifier>=0&&modifier<MUT_COUNT?modifier:0];
}
int GameplayBeamCost(const GameplaySystem *game) { return BEAM_LINK_COST-5*game->upgrades[UPG_CAPACITOR]; }
int GameplayBombCost(const GameplaySystem *game) { return BOMB_LINK_COST-10*game->upgrades[UPG_CAPACITOR]; }
const char *GameplayUpgradePreview(const GameplaySystem *game,int up) {
    int level=up>=0&&up<UPGRADE_COUNT?game->upgrades[up]:0;
    switch(up) {
        case UPG_CAPACITOR: return TextFormat("Beam %d > %d   Bomb %d > %d",GameplayBeamCost(game),GameplayBeamCost(game)-5,GameplayBombCost(game),GameplayBombCost(game)-10);
        case UPG_HARVEST: return TextFormat("Links / basic kill: %d > %d",1+level,2+level);
        case UPG_PIERCE: return TextFormat("Pulse damage: %d%% > %d%%",100+25*level,125+25*level);
        case UPG_FOCUS: return TextFormat("Charge damage: %d%% > %d%%",100+50*level,150+50*level);
        case UPG_GRAZE: return TextFormat("Links / close dodge: %d > %d",1+2*level,3+2*level);
        case UPG_AGILITY: return TextFormat("Evade: %.1fs > %.1fs",3.2f-.4f*level,2.8f-.4f*level);
        case UPG_CHAIN: return TextFormat("Chain window: %.1fs > %.1fs",2.6f+.6f*level,3.2f+.6f*level);
        case UPG_QUICKCHARGE: return TextFormat("Charge rate: %d%% > %d%%",100+25*level,125+25*level);
        case UPG_TWIN: return TextFormat("Total pulse output: %d%% > %d%%",level?110+30*level:100,140+30*level);
        case UPG_EFFICIENCY: return TextFormat("Boost drain: %d%% > %d%%",100-20*level,80-20*level);
        case UPG_BRANCH: return TextFormat("Additional beam arcs: %d > %d",level,level+1);
        case UPG_WIDELOCK: return TextFormat("Acquisition width: +%.2f > +%.2f",.85f*level,.85f*(level+1));
        case UPG_BOMB_GUARD: return TextFormat("Bomb protection: %.1fs > %.1fs",.4f*level,.4f*(level+1));
        default: return "";
    }
}

static bool UpgradeFitsLane(int up,int lane) {
    if(lane==0) return up==UPG_CAPACITOR || up==UPG_HARVEST || up==UPG_GRAZE;
    if(lane==1) return up==UPG_PIERCE || up==UPG_FOCUS || up==UPG_TWIN || up==UPG_QUICKCHARGE || up==UPG_BRANCH;
    return true;
}
static void AddGameplayScore(GameplaySystem *game,int64_t points) {
    int64_t total=(int64_t)game->score+points;
    game->score=total>INT_MAX?INT_MAX:total<0?0:(int)total;
}
void OfferGameplayUpgrade(GameplaySystem *game) {
    bool chosen[UPGRADE_COUNT]={false};
    uint32_t seed=game->runSeed ^ (uint32_t)game->boss.encounterIndex*0xa511e9b3u;
    for(int i=0;i<3;i++) {
        int start=(int)(MixBits(seed+(uint32_t)i*97u)%UPGRADE_COUNT),best=-1;
        // One faster-specials option, one offensive option, one open choice.
        // Once a lane is capped, fall back to any uncapped distinct upgrade.
        for(int pass=0;pass<2 && best<0;pass++) for(int j=0;j<UPGRADE_COUNT;j++) {
            int candidate=(start+j)%UPGRADE_COUNT;
            if(!chosen[candidate] && game->upgrades[candidate]<UPGRADE_MAX_LEVEL &&
               (pass || UpgradeFitsLane(candidate,i))) { best=candidate; break; }
        }
        game->upgradeOffers[i]=best>=0?best:-1-i;
        if(best>=0) chosen[best]=true;
    }
    game->upgradePending=true;
}
bool ChooseGameplayUpgrade(GameplaySystem *game,int choice) {
    if(!game->upgradePending || choice<0 || choice>=3) return false;
    int up=game->upgradeOffers[choice];
    if(up>=0 && up<UPGRADE_COUNT) {
        if(game->upgrades[up]>=UPGRADE_MAX_LEVEL) return false;
        game->upgrades[up]++;
    } else if(up==-2) {
        game->combo=12; game->weaponTier=3; game->comboTimer=6.0f+.6f*game->upgrades[UPG_CHAIN];
    } else if(up==-3) AddGameplayScore(game,2500);
    else game->chainLinks=(int)fminf(MAX_CHAIN_LINKS,game->chainLinks+30);
    game->upgradePending=false;
    game->player.fireHeldLast=false;
    game->player.chargeTime=0;
    game->player.fireHoldTime=0;
    return true;
}
