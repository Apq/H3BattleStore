// ========== BattleCapture.inc.cpp ==========
// 从当前战斗只读采集时刻状态。任何越界、空指针或未知容器都返回失败，不写游戏内存。

static bool Readable_(const void* address, size_t size)
{
    return address && !IsBadReadPtr(address, size);
}

static bool StackIdentity_(const H3CombatManager* mgr, const H3CombatCreature* stack, int* side, int* slot)
{
    if (!mgr || !stack) return false;
    const uintptr_t base = (uintptr_t)&mgr->stacks[0][0];
    const uintptr_t value = (uintptr_t)stack;
    if (value < base) return false;
    const uintptr_t offset = value - base;
    if (offset % sizeof(H3CombatCreature) != 0) return false;
    const uintptr_t index = offset / sizeof(H3CombatCreature);
    if (index >= 42) return false;
    *side = (int)(index / 21);
    *slot = (int)(index % 21);
    return true;
}

static bool ReadDequeInts_(const uint8_t* object, std::vector<int32_t>* out)
{
    out->clear();
    const uint32_t size = *(const uint32_t*)(object + 0x2C);
    if (!size) return true;
    if (size > 100000) return false;
    const uint8_t* current = *(const uint8_t* const*)(object + 0x0C);
    const uint8_t* blockEnd = *(const uint8_t* const*)(object + 0x08);
    const uint8_t* const* map = *(const uint8_t* const* const*)(object + 0x10);
    const uint8_t* const endCurrent = *(const uint8_t* const*)(object + 0x1C);
    const uint8_t* const* const endMap = *(const uint8_t* const* const*)(object + 0x20);
    if (!Readable_(current, 4) || !Readable_(map, sizeof(void*))) return false;
    out->reserve(size);
    for (uint32_t read = 0; read < size; ++read) {
        if (current == blockEnd) {
            ++map;
            if (map > endMap || !Readable_(map, sizeof(void*)) || !Readable_(*map, 4)) return false;
            current = *map;
            blockEnd = current + 0x1000;
        }
        if (!Readable_(current, 4)) return false;
        out->push_back(*(const int32_t*)current);
        current += 4;
        if (read + 1 == size && (current != endCurrent || map != endMap)) return false;
    }
    return true;
}

static bool ReadPointerRelations_(const H3CombatManager* mgr, const H3Vector<H3CombatCreature*>& vector, std::vector<CodecIdentity>* out)
{
    out->clear();
    const UINT count = vector.Count();
    if (!count) return true;
    if (count > 42 || !Readable_(vector.CFirst(), count * sizeof(H3CombatCreature*))) return false;
    out->reserve(count);
    for (UINT i = 0; i < count; ++i) {
        CodecIdentity identity = { -1, -1 };
        if (!StackIdentity_(mgr, vector[i], &identity.side, &identity.slot)) return false;
        out->push_back(identity);
    }
    return true;
}

static bool CaptureBattle_(const H3CombatManager* mgr, CodecCapture* out, std::string* error)
{
    if (!Readable_(mgr, sizeof(H3CombatManager)) || !out) {
        if (error) *error = "combat manager is not readable";
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->version = kCodecVersion;
    out->action = mgr->action;
    out->actionParameter = mgr->actionParameter;
    out->actionTarget = mgr->actionTarget;
    out->actionParameter2 = mgr->actionParameter2;
    out->landType = mgr->landType;
    out->absoluteObstacleId = mgr->absoluteObstacleId;
    out->siegeKind = mgr->siegeKind;
    out->hasMoat = mgr->hasMoat;
    out->specialTerrain = mgr->specialTerrain;
    out->antiMagicGarrison = mgr->antiMagicGarrison;
    out->creatureBank = mgr->creatureBank;
    out->boatCombat = mgr->boatCombat;
    out->currentMonSide = mgr->currentMonSide;
    out->currentMonIndex = mgr->currentMonIndex;
    out->currentActiveSide = mgr->currentActiveSide;
    out->autoCombat = mgr->autoCombat;
    out->creatureAtMousePos = mgr->creatureAtMousePos;
    out->mouseCoord = mgr->mouseCoord;
    out->attackerCoord = mgr->attackerCoord;
    out->moveType = mgr->moveType;
    out->siegeKind2 = mgr->siegeKind2;
    out->finished = mgr->finished;
    out->necromancyRaisedAmount = mgr->necromancyRaisedAmount;
    out->necromancyRaisedMonsters = mgr->necromancyRaisedMonsters;
    out->tacticsPhase = mgr->tacticsPhase;
    out->turn = mgr->turn;
    out->tacticsDifference = mgr->tacticsDifference;
    out->waitPhase = mgr->waitPhase;
    memcpy(out->heroSpellPower, mgr->heroSpellPower, sizeof(out->heroSpellPower));
    memcpy(out->isNotAI, mgr->isNotAI, sizeof(out->isNotAI));
    memcpy(out->isHuman, mgr->isHuman, sizeof(out->isHuman));
    memcpy(out->heroOwner, mgr->heroOwner, sizeof(out->heroOwner));
    memcpy(out->artifactAutoCast, mgr->artifactAutoCast, sizeof(out->artifactAutoCast));
    memcpy(out->heroCasted, mgr->heroCasted, sizeof(out->heroCasted));
    memcpy(out->heroMonCount, mgr->heroMonCount, sizeof(out->heroMonCount));
    memcpy(out->turnsSinceLastEnchanterCast, mgr->turnsSinceLastEnchanterCast, sizeof(out->turnsSinceLastEnchanterCast));
    memcpy(out->summonedMonster, mgr->summonedMonster, sizeof(out->summonedMonster));
    memcpy(out->fortWallsHp, mgr->fortWallsHp, sizeof(out->fortWallsHp));
    memcpy(out->fortWallsAlive, mgr->fortWallsAlive, sizeof(out->fortWallsAlive));
    memcpy(out->massSpellTarget, mgr->massSpellTarget, sizeof(out->massSpellTarget));
    memcpy(out->accessibleSquares, mgr->accessibleSquares, sizeof(out->accessibleSquares));
    memcpy(out->accessibleSquares2, mgr->accessibleSquares2, sizeof(out->accessibleSquares2));

    for (int side = 0; side < 2; ++side) {
        if (mgr->hero[side] && Readable_(mgr->hero[side], 0x1A))
            out->spellPoints[side] = *(const int16_t*)((const uint8_t*)mgr->hero[side] + 0x18);
        const H3Vector<INT32>* eagle = (const H3Vector<INT32>*)((const uint8_t*)mgr + 0x545C + side * 0x10);
        const UINT eyeCount = eagle->Count();
        if (eyeCount > 1024 || (eyeCount && !Readable_(eagle->CFirst(), eyeCount * sizeof(int32_t)))) {
            if (error) *error = "eagle eye container is not readable";
            return false;
        }
        out->eagleEye[side].assign(eagle->CFirst(), eagle->CFirst() + eyeCount);
    }

    for (int squareIndex = 0; squareIndex < 187; ++squareIndex) {
        const H3CombatSquare& source = mgr->squares[squareIndex];
        CodecSquare& square = out->squares[squareIndex];
        square.obstacleBits = source.obstacleBits;
        square.obstacleIndex = source.obstacleIndex;
        square.stackSide = source.stackSide;
        square.stackIndex = source.stackIndex;
        square.twoHexMonsterSquare = source.twoHexMonsterSquare;
        square.deadStacksNumber = source.deadStacksNumber;
        if (square.deadStacksNumber < 0 || square.deadStacksNumber > 14) return false;
        memcpy(square.deadStackSide, source.deadStackSide, sizeof(square.deadStackSide));
        memcpy(square.deadStackIndex, source.deadStackIndex, sizeof(square.deadStackIndex));
        memcpy(square.belongsToAttacker, source.belongsToAttacker, sizeof(square.belongsToAttacker));
        square.availableForLeftSquare = source.availableForLeftSquare;
        square.availableForRightSquare = source.availableForRightSquare;
    }

    const UINT obstacleCount = mgr->obstacleInfo.Count();
    if (obstacleCount > 4096 || (obstacleCount && !Readable_(mgr->obstacleInfo.CFirst(), obstacleCount * sizeof(H3Obstacle)))) {
        if (error) *error = "obstacle container is not readable";
        return false;
    }
    out->obstacles.reserve(obstacleCount);
    const H3ObstacleInfo* obstacleTable = (const H3ObstacleInfo*)0x63C7C8;
    for (UINT i = 0; i < obstacleCount; ++i) {
        const H3Obstacle& source = mgr->obstacleInfo[i];
        CodecObstacle item;
        memset(&item, 0, sizeof(item));
        item.infoIndex = -1;
        if (source.info && obstacleTable && source.info >= obstacleTable)
            item.infoIndex = (int32_t)(source.info - obstacleTable);
        item.anchorHex = source.anchorHex;
        item.ownerSide = source.ownerSide;
        item.featureTriggered = source.featureTriggered;
        item.featureDamage = source.featureDamage;
        item.featureDuration = source.featureDuration;
        item.animationIndex = source.animationIndex;
        out->obstacles.push_back(item);
    }

    if (mgr->dlg && Readable_(mgr->dlg, sizeof(H3CombatDlg))) {
        const H3Vector<H3String*>& log = *(const H3Vector<H3String*>*)((const uint8_t*)mgr->dlg + 0x54);
        const UINT logCount = log.Count();
        if (logCount > 100000 || (logCount && !Readable_(log.CFirst(), logCount * sizeof(H3String*)))) return false;
        for (UINT i = 0; i < logCount; ++i) {
            const H3String* line = log[i];
            if (!line || !Readable_(line, sizeof(H3String)) || line->Length() > 0xFFFF
                || (line->Length() && !Readable_(line->String(), line->Length()))) return false;
            out->logLines.push_back(std::string(line->String(), line->String() + line->Length()));
        }
    }

    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            const H3CombatCreature& source = mgr->stacks[side][slot];
            CodecStack& stack = out->stacks[side][slot];
            stack.occupied = source.type != -1;
            if (!stack.occupied) continue;
            stack.type = source.type;
            stack.position = source.position;
            stack.animation = source.animation;
            stack.animationFrame = source.animationFrame;
            stack.secondHexOrientation = source.secondHexOrientation;
            stack.numberAlive = source.numberAlive;
            stack.previousNumber = source.previousNumber;
            stack.numberForeverDead = source.numberForeverDead;
            stack.healthLost = source.healthLost;
            stack.slotIndex = source.slotIndex;
            stack.numberAtStart = source.numberAtStart;
            stack.baseHP = source.baseHP;
            stack.isLucky = source.isLucky;
            stack.side = source.side;
            stack.sideIndex = source.sideIndex;
            stack.activeSpellNumber = source.activeSpellNumber;
            memcpy(stack.activeSpellDuration, source.activeSpellDuration, sizeof(stack.activeSpellDuration));
            memcpy(stack.activeSpellLevel, source.activeSpellLevel, sizeof(stack.activeSpellLevel));
            stack.retaliations = source.retaliations;
            stack.blessDamage = source.blessDamage;
            stack.curseDamage = source.curseDamage;
            stack.antiMagic = source.antiMagic;
            stack.bloodlustEffect = source.bloodlustEffect;
            stack.precisionEffect = source.precisionEffect;
            stack.weaknessEffect = source.weaknessEffect;
            stack.stoneSkinEffect = source.stoneSkinEffect;
            stack.prayerEffect = source.prayerEffect;
            stack.mirthEffect = source.mirthEffect;
            stack.sorrowEffect = source.sorrowEffect;
            stack.fortuneEffect = source.fortuneEffect;
            stack.misfortuneEffect = source.misfortuneEffect;
            stack.slayerType = source.slayerType;
            stack.hexesTraveled = source.hexesTraveled;
            stack.counterstrikeEffect = source.counterstrikeEffect;
            stack.frenzyMultiplier = source.frenzyMultiplier;
            stack.blindEffect = source.blindEffect;
            stack.fireShieldEffect = source.fireShieldEffect;
            stack.protectionAirEffect = source.protectionAirEffect;
            stack.protectionFireEffect = source.protectionFireEffect;
            stack.protectionWaterEffect = source.protectionWaterEffect;
            stack.protectionEarthEffect = source.protectionEarthEffect;
            stack.shieldEffect = source.shieldEffect;
            stack.airShieldEffect = source.airShieldEffect;
            stack.blinded = source.blinded;
            stack.paralyzed = source.paralyzed;
            stack.forgetfulnessLevel = source.forgetfulnessLevel;
            stack.slowEffect = source.slowEffect;
            stack.hasteEffect = source.hasteEffect;
            stack.diseaseAttackEffect = source.diseaseAttackEffect;
            stack.diseaseDefenseEffect = source.diseaseDefenseEffect;
            stack.faerieDragonSpell = source.faerieDragonSpell;
            stack.magicMirrorEffect = source.magicMirrorEffect;
            stack.morale = source.morale;
            stack.luck = source.luck;
            stack.isDone = source.isDone;
            stack.highlightContour = source.highlightContour;
            const uint8_t* raw = (const uint8_t*)&source;
            stack.attackedAlready = *raw;
            stack.isDead = raw[0xEB];
            stack.hasLosses = raw[0xE9];
            stack.hasLosses2 = raw[0xEA];
            stack.cloneId = source.cloneId;
            stack.cloneDuration = source.cloneDuration;
            stack.spellToApply = source.spellToApply;
            stack.visibility = source.visibility;
            memcpy(stack.creatureInfo, &source.info, sizeof(stack.creatureInfo));
            const uint8_t* rawRelations = (const uint8_t*)&source;
            const H3Vector<H3CombatCreature*>* relations[] = {
                (const H3Vector<H3CombatCreature*>*)(rawRelations + 0x4F4),
                (const H3Vector<H3CombatCreature*>*)(rawRelations + 0x504),
                (const H3Vector<H3CombatCreature*>*)(rawRelations + 0x514),
                (const H3Vector<H3CombatCreature*>*)(rawRelations + 0x524)
            };
            for (int relation = 0; relation < 4; ++relation) {
                if (!ReadPointerRelations_(mgr, *relations[relation], &stack.relations[relation])) {
                    if (error) *error = "stack relation points outside the combat manager";
                    return false;
                }
            }
            if (!ReadDequeInts_((const uint8_t*)&source + 0x420, &stack.spellIds)) {
                if (error) *error = "spell deque is not readable";
                return false;
            }
        }
    }
    return true;
}
