// ========== BattleRestore.inc.cpp ==========
// 同场战斗恢复。只有完整校验通过后才写战斗内存；任一步失败都停止并记录。

using StackResetFn = void(__thiscall*)(H3CombatCreature*);
using StackSummonFn = void(__thiscall*)(H3CombatCreature*, int, int, H3Hero*, int, int, int, int);
using DequeClearFn = void(__thiscall*)(void*);
using DequeInsertFn = void(__thiscall*)(void*, int);
using ObstacleReleaseFn = void(__thiscall*)(H3CombatManager*, unsigned);
using RefreshCreatureFn = void(__thiscall*)(H3CombatManager*, H3CombatCreature*);
using RefreshFieldFn = void(__thiscall*)(H3CombatManager*);
using RecalcMoraleFn = void(__thiscall*)(H3CombatManager*);
using RngSetFn = void(__stdcall*)(unsigned);

static StackResetFn const kStackReset = (StackResetFn)0x43D2E0;
static StackSummonFn const kStackSummon = (StackSummonFn)0x43D5D0;
static DequeClearFn const kDequeClear = (DequeClearFn)0x448A80;
static DequeInsertFn const kDequeInsert = (DequeInsertFn)0x43C810;
static ObstacleReleaseFn const kObstacleRelease = (ObstacleReleaseFn)0x466710;
static RefreshCreatureFn const kRefreshCreature = (RefreshCreatureFn)0x495770;
static RefreshFieldFn const kRefreshField = (RefreshFieldFn)0x493FC0;
static RecalcMoraleFn const kRecalcMorale = (RecalcMoraleFn)0x469330;
static RngSetFn const kRngSet = (RngSetFn)0x61841F;

static bool RestoreCaptureValid_(const CodecCapture& capture, std::string* error)
{
    if (capture.version != kCodecVersion) { if (error) *error = "capture version"; return false; }
    if (capture.currentMonSide < 0 || capture.currentMonSide > 1
        || capture.currentMonIndex < 0 || capture.currentMonIndex > 20) {
        if (error) *error = "active stack index";
        return false;
    }
    int occupied = 0;
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            const CodecStack& stack = capture.stacks[side][slot];
            if (!stack.occupied) continue;
            ++occupied;
            if (stack.type < 0 || stack.type > 0x95 || stack.position < -1 || stack.position >= 187) {
                if (error) *error = "stack identity";
                return false;
            }
            for (int vector = 0; vector < 4; ++vector) {
                for (size_t i = 0; i < stack.relations[vector].size(); ++i) {
                    const CodecIdentity& id = stack.relations[vector][i];
                    if (id.side < 0 || id.side > 1 || id.slot < 0 || id.slot > 20
                        || !capture.stacks[id.side][id.slot].occupied) {
                        if (error) *error = "relation target";
                        return false;
                    }
                }
            }
        }
    }
    if (!occupied) { if (error) *error = "empty capture"; return false; }
    return true;
}

static void WriteVector_(H3Vector<H3CombatCreature*>* vector, const std::vector<CodecIdentity>& items, H3CombatManager* mgr)
{
    vector->RemoveAll();
    for (size_t i = 0; i < items.size(); ++i)
        vector->Add(&mgr->stacks[items[i].side][items[i].slot]);
}

static void RestoreStackScalars_(H3CombatCreature* target, const CodecStack& source)
{
    target->position = source.position;
    target->animation = source.animation;
    target->animationFrame = source.animationFrame;
    target->secondHexOrientation = source.secondHexOrientation;
    target->numberAlive = source.numberAlive;
    target->previousNumber = source.previousNumber;
    target->numberForeverDead = source.numberForeverDead;
    target->healthLost = source.healthLost;
    target->slotIndex = source.slotIndex;
    target->numberAtStart = source.numberAtStart;
    target->baseHP = source.baseHP;
    target->isLucky = source.isLucky;
    target->side = source.side;
    target->sideIndex = source.sideIndex;
    target->activeSpellNumber = source.activeSpellNumber;
    memcpy(target->activeSpellDuration, source.activeSpellDuration, sizeof(source.activeSpellDuration));
    memcpy(target->activeSpellLevel, source.activeSpellLevel, sizeof(source.activeSpellLevel));
    target->retaliations = source.retaliations;
    target->blessDamage = source.blessDamage;
    target->curseDamage = source.curseDamage;
    target->antiMagic = source.antiMagic;
    target->bloodlustEffect = source.bloodlustEffect;
    target->precisionEffect = source.precisionEffect;
    target->weaknessEffect = source.weaknessEffect;
    target->stoneSkinEffect = source.stoneSkinEffect;
    target->prayerEffect = source.prayerEffect;
    target->mirthEffect = source.mirthEffect;
    target->sorrowEffect = source.sorrowEffect;
    target->fortuneEffect = source.fortuneEffect;
    target->misfortuneEffect = source.misfortuneEffect;
    target->slayerType = source.slayerType;
    target->hexesTraveled = source.hexesTraveled;
    target->counterstrikeEffect = source.counterstrikeEffect;
    target->frenzyMultiplier = source.frenzyMultiplier;
    target->blindEffect = source.blindEffect;
    target->fireShieldEffect = source.fireShieldEffect;
    target->protectionAirEffect = source.protectionAirEffect;
    target->protectionFireEffect = source.protectionFireEffect;
    target->protectionWaterEffect = source.protectionWaterEffect;
    target->protectionEarthEffect = source.protectionEarthEffect;
    target->shieldEffect = source.shieldEffect;
    target->airShieldEffect = source.airShieldEffect;
    target->blinded = source.blinded;
    target->paralyzed = source.paralyzed;
    target->forgetfulnessLevel = source.forgetfulnessLevel;
    target->slowEffect = source.slowEffect;
    target->hasteEffect = source.hasteEffect;
    target->diseaseAttackEffect = source.diseaseAttackEffect;
    target->diseaseDefenseEffect = source.diseaseDefenseEffect;
    target->faerieDragonSpell = source.faerieDragonSpell;
    target->magicMirrorEffect = source.magicMirrorEffect;
    target->morale = source.morale;
    target->luck = source.luck;
    target->isDone = source.isDone;
    target->highlightContour = source.highlightContour;
    uint8_t* raw = (uint8_t*)target;
    raw[0x00] = source.attackedAlready;
    raw[0xE9] = source.hasLosses;
    raw[0xEA] = source.hasLosses2;
    raw[0xEB] = source.isDead;
    target->cloneId = source.cloneId;
    target->cloneDuration = source.cloneDuration;
    target->spellToApply = source.spellToApply;
    target->visibility = source.visibility;
}

static bool RestoreSameBattle_(H3CombatManager* mgr, const CodecCapture& capture, std::string* error)
{
    if (!CombatIsReadable_(mgr) || !RestoreCaptureValid_(capture, error)) return false;
    std::string fingerprint;
    if (!BattleFingerprint_(mgr, &fingerprint, error)) return false;

    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            H3CombatCreature& target = mgr->stacks[side][slot];
            const CodecStack& source = capture.stacks[side][slot];
            if (target.type != -1 && !source.occupied) kStackReset(&target);
        }
    }
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            H3CombatCreature& target = mgr->stacks[side][slot];
            const CodecStack& source = capture.stacks[side][slot];
            if (target.type == -1 && source.occupied) {
                kStackSummon(&target, source.type, source.numberAtStart,
                    mgr->hero[side], side, slot, source.position, source.slotIndex);
            }
        }
    }
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            H3CombatCreature& target = mgr->stacks[side][slot];
            const CodecStack& source = capture.stacks[side][slot];
            if (!source.occupied) continue;
            uint8_t* raw = (uint8_t*)&target;
            H3Vector<H3CombatCreature*>* relations[] = {
                (H3Vector<H3CombatCreature*>*)(raw + 0x4F4),
                (H3Vector<H3CombatCreature*>*)(raw + 0x504),
                (H3Vector<H3CombatCreature*>*)(raw + 0x514),
                (H3Vector<H3CombatCreature*>*)(raw + 0x524)
            };
            for (int vector = 0; vector < 4; ++vector)
                WriteVector_(relations[vector], source.relations[vector], mgr);
            void* deque = raw + 0x420;
            kDequeClear(deque);
            for (size_t i = 0; i < source.spellIds.size(); ++i)
                kDequeInsert(deque, source.spellIds[i]);
            RestoreStackScalars_(&target, source);
        }
    }

    for (int i = 0; i < 187; ++i) {
        H3CombatSquare& target = mgr->squares[i];
        const CodecSquare& source = capture.squares[i];
        target.obstacleBits = source.obstacleBits;
        target.obstacleIndex = source.obstacleIndex;
        target.stackSide = source.stackSide;
        target.stackIndex = source.stackIndex;
        target.twoHexMonsterSquare = source.twoHexMonsterSquare;
        target.deadStacksNumber = source.deadStacksNumber;
        memcpy(target.deadStackSide, source.deadStackSide, sizeof(source.deadStackSide));
        memcpy(target.deadStackIndex, source.deadStackIndex, sizeof(source.deadStackIndex));
        memcpy(target.belongsToAttacker, source.belongsToAttacker, sizeof(source.belongsToAttacker));
        target.availableForLeftSquare = source.availableForLeftSquare;
        target.availableForRightSquare = source.availableForRightSquare;
    }

    const UINT obstacleCount = mgr->obstacleInfo.Count();
    for (UINT i = 0; i < obstacleCount; ++i) {
        if (mgr->obstacleInfo[i].def) kObstacleRelease(mgr, i);
    }
    mgr->obstacleInfo.RemoveAll();
    const H3ObstacleInfo* table = (const H3ObstacleInfo*)0x63C7C8;
    for (size_t i = 0; i < capture.obstacles.size(); ++i) {
        H3Obstacle item;
        memset(&item, 0, sizeof(item));
        const CodecObstacle& source = capture.obstacles[i];
        if (source.infoIndex >= 0 && source.infoIndex < 256) item.info = (H3ObstacleInfo*)(table + source.infoIndex);
        item.anchorHex = source.anchorHex;
        item.ownerSide = source.ownerSide;
        item.featureTriggered = source.featureTriggered;
        item.featureDamage = source.featureDamage;
        item.featureDuration = source.featureDuration;
        item.animationIndex = source.animationIndex;
        mgr->obstacleInfo.Add(item);
    }

    if (mgr->dlg) {
        H3Vector<H3String*>* log = (H3Vector<H3String*>*)((uint8_t*)mgr->dlg + 0x54);
        for (UINT i = 0; i < log->Count(); ++i) {
            H3String* line = (*log)[i];
            if (!line) continue;
            line->~H3String();
            FASTCALL_1(void, 0x60B0F0, line);
        }
        log->RemoveAll();
        for (size_t i = 0; i < capture.logLines.size(); ++i) {
            H3String* line = (H3String*)FASTCALL_1(void*, 0x617492, 0x10);
            if (!line) return false;
            line->Init();
            line->Assign(capture.logLines[i].c_str(), (UINT)capture.logLines[i].size());
            log->Add(line);
        }
    }

    mgr->action = (eCombatAction)capture.action;
    mgr->actionParameter = capture.actionParameter;
    mgr->actionTarget = capture.actionTarget;
    mgr->actionParameter2 = capture.actionParameter2;
    mgr->currentMonSide = capture.currentMonSide;
    mgr->currentMonIndex = capture.currentMonIndex;
    mgr->currentActiveSide = capture.currentActiveSide;
    mgr->activeStack = &mgr->stacks[capture.currentMonSide][capture.currentMonIndex];
    mgr->turn = capture.turn;
    mgr->waitPhase = capture.waitPhase;
    mgr->tacticsDifference = capture.tacticsDifference;
    memcpy(mgr->fortWallsHp, capture.fortWallsHp, sizeof(capture.fortWallsHp));
    memcpy(mgr->fortWallsAlive, capture.fortWallsAlive, sizeof(capture.fortWallsAlive));
    memcpy(mgr->accessibleSquares, capture.accessibleSquares, sizeof(capture.accessibleSquares));
    memcpy(mgr->accessibleSquares2, capture.accessibleSquares2, sizeof(capture.accessibleSquares2));
    for (int side = 0; side < 2; ++side) {
        if (mgr->hero[side]) *(int16_t*)((uint8_t*)mgr->hero[side] + 0x18) = capture.spellPoints[side];
        H3Vector<INT32>* eagle = (H3Vector<INT32>*)((uint8_t*)mgr + 0x545C + side * 0x10);
        eagle->RemoveAll();
        for (size_t i = 0; i < capture.eagleEye[side].size(); ++i) eagle->Add(capture.eagleEye[side][i]);
    }
    kRecalcMorale(mgr);
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            if (!capture.stacks[side][slot].occupied) continue;
            mgr->stacks[side][slot].morale = capture.stacks[side][slot].morale;
            mgr->stacks[side][slot].luck = capture.stacks[side][slot].luck;
        }
    }
    *(uint32_t*)0x67FBE4 = capture.rngMirrorSeed;
    kRngSet(capture.rngTlsSeed);
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 21; ++slot)
            if (capture.stacks[side][slot].occupied)
                kRefreshCreature(mgr, &mgr->stacks[side][slot]);
    kRefreshField(mgr);
    return true;
}
