// ========== BattleCodec.inc.cpp ==========
// 把一次战斗时刻编码成长度明确的二进制段。
// 本文件不访问游戏对象；超限直接失败，不截断日志或容器。

static const uint32_t kSectionBattle = 1;
static const uint32_t kSectionStacks = 2;
static const uint32_t kSectionSquares = 3;
static const uint32_t kSectionObstacles = 4;
static const uint32_t kSectionLog = 5;
static const uint32_t kSectionHeroes = 6;
static const uint32_t kSectionRelations = 7;
static const uint32_t kSectionSpells = 8;
static const uint32_t kCodecVersion = 1;

struct CodecWriter
{
    std::vector<uint8_t> bytes;
    bool ok;

    CodecWriter() : ok(true) {}

    void Fail() { ok = false; }

    void U8(uint8_t value)
    {
        if (ok) bytes.push_back(value);
    }

    void U16(uint16_t value)
    {
        U8((uint8_t)value);
        U8((uint8_t)(value >> 8));
    }

    void U32(uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            U8((uint8_t)(value >> shift));
    }

    void I32(int32_t value) { U32((uint32_t)value); }

    void U64(uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8)
            U8((uint8_t)(value >> shift));
    }

    void F32(float value)
    {
        uint32_t bits = 0;
        memcpy(&bits, &value, sizeof(bits));
        U32(bits);
    }

    void Bytes(const void* data, size_t size)
    {
        const uint8_t* source = (const uint8_t*)data;
        for (size_t i = 0; ok && i < size; ++i) bytes.push_back(source[i]);
    }

    void Text(const char* text, size_t size)
    {
        if (size > 0xFFFF) { Fail(); return; }
        U16((uint16_t)size);
        Bytes(text, size);
    }
};

struct CodecReader
{
    const uint8_t* data;
    size_t size;
    size_t pos;
    bool ok;

    CodecReader(const uint8_t* data_, size_t size_)
        : data(data_), size(size_), pos(0), ok(data_ != nullptr) {}

    void Need(size_t count)
    {
        if (!ok || count > size - pos) ok = false;
    }

    uint8_t U8()
    {
        Need(1);
        return ok ? data[pos++] : 0;
    }

    uint16_t U16()
    {
        const uint16_t low = U8();
        return (uint16_t)(low | ((uint16_t)U8() << 8));
    }

    uint32_t U32()
    {
        uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8)
            value |= ((uint32_t)U8() << shift);
        return value;
    }

    int32_t I32() { return (int32_t)U32(); }

    uint64_t U64()
    {
        uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8)
            value |= ((uint64_t)U8() << shift);
        return value;
    }

    float F32()
    {
        const uint32_t bits = U32();
        float value = 0;
        memcpy(&value, &bits, sizeof(value));
        return value;
    }

    void Bytes(void* out, size_t count)
    {
        Need(count);
        if (ok) memcpy(out, data + pos, count);
        if (ok) pos += count;
    }

    bool Finish() const { return ok && pos == size; }
};

struct CodecIdentity
{
    int32_t side;
    int32_t slot;
};

struct CodecStack
{
    bool occupied;
    int32_t type;
    int32_t position;
    int32_t animation;
    int32_t animationFrame;
    int32_t secondHexOrientation;
    int32_t numberAlive;
    int32_t previousNumber;
    int32_t numberForeverDead;
    int32_t healthLost;
    int32_t slotIndex;
    int32_t numberAtStart;
    int32_t baseHP;
    int32_t isLucky;
    int32_t side;
    int32_t sideIndex;
    int32_t activeSpellNumber;
    int32_t activeSpellDuration[81];
    int32_t activeSpellLevel[81];
    int32_t retaliations;
    int32_t blessDamage;
    int32_t curseDamage;
    int32_t antiMagic;
    int32_t bloodlustEffect;
    int32_t precisionEffect;
    int32_t weaknessEffect;
    int32_t stoneSkinEffect;
    int32_t prayerEffect;
    int32_t mirthEffect;
    int32_t sorrowEffect;
    int32_t fortuneEffect;
    int32_t misfortuneEffect;
    int32_t slayerType;
    int32_t hexesTraveled;
    int32_t counterstrikeEffect;
    float frenzyMultiplier;
    float blindEffect;
    float fireShieldEffect;
    float protectionAirEffect;
    float protectionFireEffect;
    float protectionWaterEffect;
    float protectionEarthEffect;
    float shieldEffect;
    float airShieldEffect;
    uint8_t blinded;
    uint8_t paralyzed;
    int32_t forgetfulnessLevel;
    float slowEffect;
    int32_t hasteEffect;
    int32_t diseaseAttackEffect;
    int32_t diseaseDefenseEffect;
    int32_t faerieDragonSpell;
    int32_t magicMirrorEffect;
    int32_t morale;
    int32_t luck;
    uint8_t isDone;
    uint8_t highlightContour;
    uint8_t attackedAlready;
    uint8_t isDead;
    uint8_t hasLosses;
    uint8_t hasLosses2;
    int32_t cloneId;
    int32_t cloneDuration;
    int32_t spellToApply;
    int32_t visibility;
    uint8_t creatureInfo[0x74];
    std::vector<int32_t> spellIds;
    std::vector<CodecIdentity> relations[4];
};

struct CodecSquare
{
    uint8_t obstacleBits;
    int32_t obstacleIndex;
    int8_t stackSide;
    int8_t stackIndex;
    uint8_t twoHexMonsterSquare;
    int32_t deadStacksNumber;
    int8_t deadStackSide[14];
    int8_t deadStackIndex[14];
    uint8_t belongsToAttacker[14];
    uint8_t availableForLeftSquare;
    uint8_t availableForRightSquare;
};

struct CodecObstacle
{
    int32_t infoIndex;
    uint8_t anchorHex;
    int8_t ownerSide;
    uint8_t featureTriggered;
    uint32_t featureDamage;
    uint32_t featureDuration;
    uint32_t animationIndex;
};

struct CodecCapture
{
    uint32_t version;
    int32_t action;
    int32_t actionParameter;
    int32_t actionTarget;
    int32_t actionParameter2;
    int32_t landType;
    int32_t absoluteObstacleId;
    int32_t siegeKind;
    int32_t hasMoat;
    int32_t specialTerrain;
    uint8_t antiMagicGarrison;
    uint8_t creatureBank;
    uint8_t boatCombat;
    int32_t heroSpellPower[2];
    uint8_t isNotAI[2];
    uint8_t isHuman[2];
    int32_t heroOwner[2];
    uint8_t artifactAutoCast[2];
    uint8_t heroCasted[2];
    int32_t heroMonCount[2];
    int32_t turnsSinceLastEnchanterCast[2];
    int32_t summonedMonster[2];
    int32_t currentMonSide;
    int32_t currentMonIndex;
    int32_t currentActiveSide;
    int32_t autoCombat;
    int32_t creatureAtMousePos;
    int32_t mouseCoord;
    int32_t attackerCoord;
    int32_t moveType;
    int32_t siegeKind2;
    int32_t finished;
    int32_t necromancyRaisedAmount;
    int32_t necromancyRaisedMonsters;
    uint8_t tacticsPhase;
    int32_t turn;
    int32_t tacticsDifference;
    int32_t waitPhase;
    int32_t fortWallsHp[18];
    int32_t fortWallsAlive[18];
    uint8_t massSpellTarget[2][20];
    uint8_t accessibleSquares[187];
    uint8_t accessibleSquares2[187];
    int16_t spellPoints[2];
    uint32_t rngTlsSeed;
    uint32_t rngMirrorSeed;
    std::vector<int32_t> eagleEye[2];
    CodecSquare squares[187];
    std::vector<CodecObstacle> obstacles;
    std::vector<std::string> logLines;
    CodecStack stacks[2][21];
};

static void WriteStackScalars_(CodecWriter* writer, const CodecStack& stack)
{
    writer->U8(stack.occupied ? 1 : 0);
    if (!stack.occupied) return;
    const int32_t values[] = {
        stack.type, stack.position, stack.animation, stack.animationFrame,
        stack.secondHexOrientation, stack.numberAlive, stack.previousNumber,
        stack.numberForeverDead, stack.healthLost, stack.slotIndex,
        stack.numberAtStart, stack.baseHP, stack.isLucky, stack.side,
        stack.sideIndex, stack.activeSpellNumber
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        writer->I32(values[i]);
    for (int i = 0; i < 81; ++i) writer->I32(stack.activeSpellDuration[i]);
    for (int i = 0; i < 81; ++i) writer->I32(stack.activeSpellLevel[i]);
    const int32_t effects[] = {
        stack.retaliations, stack.blessDamage, stack.curseDamage, stack.antiMagic,
        stack.bloodlustEffect, stack.precisionEffect, stack.weaknessEffect,
        stack.stoneSkinEffect, stack.prayerEffect, stack.mirthEffect,
        stack.sorrowEffect, stack.fortuneEffect, stack.misfortuneEffect,
        stack.slayerType, stack.hexesTraveled, stack.counterstrikeEffect
    };
    for (size_t i = 0; i < sizeof(effects) / sizeof(effects[0]); ++i)
        writer->I32(effects[i]);
    writer->F32(stack.frenzyMultiplier);
    writer->F32(stack.blindEffect);
    writer->F32(stack.fireShieldEffect);
    writer->F32(stack.protectionAirEffect);
    writer->F32(stack.protectionFireEffect);
    writer->F32(stack.protectionWaterEffect);
    writer->F32(stack.protectionEarthEffect);
    writer->F32(stack.shieldEffect);
    writer->F32(stack.airShieldEffect);
    writer->U8(stack.blinded);
    writer->U8(stack.paralyzed);
    writer->I32(stack.forgetfulnessLevel);
    writer->F32(stack.slowEffect);
    writer->I32(stack.hasteEffect);
    writer->I32(stack.diseaseAttackEffect);
    writer->I32(stack.diseaseDefenseEffect);
    writer->I32(stack.faerieDragonSpell);
    writer->I32(stack.magicMirrorEffect);
    writer->I32(stack.morale);
    writer->I32(stack.luck);
    writer->U8(stack.isDone);
    writer->U8(stack.highlightContour);
    writer->U8(stack.attackedAlready);
    writer->U8(stack.isDead);
    writer->U8(stack.hasLosses);
    writer->U8(stack.hasLosses2);
    writer->I32(stack.cloneId);
    writer->I32(stack.cloneDuration);
    writer->I32(stack.spellToApply);
    writer->I32(stack.visibility);
    writer->Bytes(stack.creatureInfo, sizeof(stack.creatureInfo));
}

static void ReadStackScalars_(CodecReader* reader, CodecStack* stack)
{
    stack->occupied = reader->U8() != 0;
    if (!stack->occupied || !reader->ok) return;
    int32_t* values[] = {
        &stack->type, &stack->position, &stack->animation, &stack->animationFrame,
        &stack->secondHexOrientation, &stack->numberAlive, &stack->previousNumber,
        &stack->numberForeverDead, &stack->healthLost, &stack->slotIndex,
        &stack->numberAtStart, &stack->baseHP, &stack->isLucky, &stack->side,
        &stack->sideIndex, &stack->activeSpellNumber
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        *values[i] = reader->I32();
    for (int i = 0; i < 81; ++i) stack->activeSpellDuration[i] = reader->I32();
    for (int i = 0; i < 81; ++i) stack->activeSpellLevel[i] = reader->I32();
    int32_t* effects[] = {
        &stack->retaliations, &stack->blessDamage, &stack->curseDamage, &stack->antiMagic,
        &stack->bloodlustEffect, &stack->precisionEffect, &stack->weaknessEffect,
        &stack->stoneSkinEffect, &stack->prayerEffect, &stack->mirthEffect,
        &stack->sorrowEffect, &stack->fortuneEffect, &stack->misfortuneEffect,
        &stack->slayerType, &stack->hexesTraveled, &stack->counterstrikeEffect
    };
    for (size_t i = 0; i < sizeof(effects) / sizeof(effects[0]); ++i)
        *effects[i] = reader->I32();
    stack->frenzyMultiplier = reader->F32();
    stack->blindEffect = reader->F32();
    stack->fireShieldEffect = reader->F32();
    stack->protectionAirEffect = reader->F32();
    stack->protectionFireEffect = reader->F32();
    stack->protectionWaterEffect = reader->F32();
    stack->protectionEarthEffect = reader->F32();
    stack->shieldEffect = reader->F32();
    stack->airShieldEffect = reader->F32();
    stack->blinded = reader->U8();
    stack->paralyzed = reader->U8();
    stack->forgetfulnessLevel = reader->I32();
    stack->slowEffect = reader->F32();
    stack->hasteEffect = reader->I32();
    stack->diseaseAttackEffect = reader->I32();
    stack->diseaseDefenseEffect = reader->I32();
    stack->faerieDragonSpell = reader->I32();
    stack->magicMirrorEffect = reader->I32();
    stack->morale = reader->I32();
    stack->luck = reader->I32();
    stack->isDone = reader->U8();
    stack->highlightContour = reader->U8();
    stack->attackedAlready = reader->U8();
    stack->isDead = reader->U8();
    stack->hasLosses = reader->U8();
    stack->hasLosses2 = reader->U8();
    stack->cloneId = reader->I32();
    stack->cloneDuration = reader->I32();
    stack->spellToApply = reader->I32();
    stack->visibility = reader->I32();
    reader->Bytes(stack->creatureInfo, sizeof(stack->creatureInfo));
}

static bool CodecEncode(const CodecCapture& capture, std::vector<hbs::ArchiveSection>* out, std::string* error)
{
    if (!out) return false;
    out->clear();
    CodecWriter battle;
    battle.U32(kCodecVersion);
    const int32_t fields[] = {
        capture.action, capture.actionParameter, capture.actionTarget, capture.actionParameter2,
        capture.landType, capture.absoluteObstacleId, capture.siegeKind, capture.hasMoat,
        capture.specialTerrain, capture.heroSpellPower[0], capture.heroSpellPower[1],
        capture.heroOwner[0], capture.heroOwner[1], capture.heroMonCount[0], capture.heroMonCount[1],
        capture.turnsSinceLastEnchanterCast[0], capture.turnsSinceLastEnchanterCast[1],
        capture.summonedMonster[0], capture.summonedMonster[1], capture.currentMonSide,
        capture.currentMonIndex, capture.currentActiveSide, capture.autoCombat,
        capture.creatureAtMousePos, capture.mouseCoord, capture.attackerCoord, capture.moveType,
        capture.siegeKind2, capture.finished, capture.necromancyRaisedAmount,
        capture.necromancyRaisedMonsters, capture.turn, capture.tacticsDifference, capture.waitPhase
    };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) battle.I32(fields[i]);
    battle.U8(capture.antiMagicGarrison);
    battle.U8(capture.creatureBank);
    battle.U8(capture.boatCombat);
    for (int side = 0; side < 2; ++side) {
        battle.U8(capture.isNotAI[side]);
        battle.U8(capture.isHuman[side]);
        battle.U8(capture.artifactAutoCast[side]);
        battle.U8(capture.heroCasted[side]);
    }
    battle.U8(capture.tacticsPhase);
    for (int i = 0; i < 18; ++i) battle.I32(capture.fortWallsHp[i]);
    for (int i = 0; i < 18; ++i) battle.I32(capture.fortWallsAlive[i]);
    battle.Bytes(capture.massSpellTarget, sizeof(capture.massSpellTarget));
    battle.Bytes(capture.accessibleSquares, sizeof(capture.accessibleSquares));
    battle.Bytes(capture.accessibleSquares2, sizeof(capture.accessibleSquares2));
    for (int side = 0; side < 2; ++side) battle.U16((uint16_t)capture.spellPoints[side]);
    battle.U32(capture.rngTlsSeed);
    battle.U32(capture.rngMirrorSeed);
    for (int side = 0; side < 2; ++side) {
        if (capture.eagleEye[side].size() > 1024) { battle.Fail(); break; }
        battle.U32((uint32_t)capture.eagleEye[side].size());
        for (size_t i = 0; i < capture.eagleEye[side].size(); ++i)
            battle.I32(capture.eagleEye[side][i]);
    }
    if (!battle.ok) { if (error) *error = "battle section overflow"; return false; }

    CodecWriter stacks;
    stacks.U32(kCodecVersion);
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 21; ++slot)
            WriteStackScalars_(&stacks, capture.stacks[side][slot]);
    if (!stacks.ok) return false;

    CodecWriter squares;
    squares.U32(kCodecVersion);
    squares.U32(187);
    for (int i = 0; i < 187; ++i) {
        const CodecSquare& square = capture.squares[i];
        squares.U8(square.obstacleBits);
        squares.I32(square.obstacleIndex);
        squares.U8((uint8_t)square.stackSide);
        squares.U8((uint8_t)square.stackIndex);
        squares.U8(square.twoHexMonsterSquare);
        if (square.deadStacksNumber < 0 || square.deadStacksNumber > 14) squares.Fail();
        squares.I32(square.deadStacksNumber);
        for (int dead = 0; dead < square.deadStacksNumber; ++dead) {
            squares.U8((uint8_t)square.deadStackSide[dead]);
            squares.U8((uint8_t)square.deadStackIndex[dead]);
            squares.U8(square.belongsToAttacker[dead]);
        }
        squares.U8(square.availableForLeftSquare);
        squares.U8(square.availableForRightSquare);
    }

    CodecWriter obstacles;
    obstacles.U32(kCodecVersion);
    if (capture.obstacles.size() > 4096) obstacles.Fail();
    obstacles.U32((uint32_t)capture.obstacles.size());
    for (size_t i = 0; i < capture.obstacles.size(); ++i) {
        const CodecObstacle& item = capture.obstacles[i];
        obstacles.I32(item.infoIndex);
        obstacles.U8(item.anchorHex);
        obstacles.U8((uint8_t)item.ownerSide);
        obstacles.U8(item.featureTriggered);
        obstacles.U32(item.featureDamage);
        obstacles.U32(item.featureDuration);
        obstacles.U32(item.animationIndex);
    }

    CodecWriter log;
    log.U32(kCodecVersion);
    if (capture.logLines.size() > 100000) log.Fail();
    log.U32((uint32_t)capture.logLines.size());
    for (size_t i = 0; i < capture.logLines.size(); ++i)
        log.Text(capture.logLines[i].data(), capture.logLines[i].size());

    CodecWriter heroes;
    heroes.U32(kCodecVersion);
    for (int side = 0; side < 2; ++side) heroes.U16((uint16_t)capture.spellPoints[side]);

    CodecWriter relations;
    relations.U32(kCodecVersion);
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            for (int vector = 0; vector < 4; ++vector) {
                const std::vector<CodecIdentity>& items = capture.stacks[side][slot].relations[vector];
                if (items.size() > 42) relations.Fail();
                relations.U16((uint16_t)items.size());
                for (size_t i = 0; i < items.size(); ++i) {
                    relations.I32(items[i].side);
                    relations.I32(items[i].slot);
                }
            }
        }
    }

    CodecWriter spells;
    spells.U32(kCodecVersion);
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            const std::vector<int32_t>& ids = capture.stacks[side][slot].spellIds;
            if (ids.size() > 100000) spells.Fail();
            spells.U32((uint32_t)ids.size());
            for (size_t i = 0; i < ids.size(); ++i) {
                if (ids[i] < 0 || ids[i] >= 81) spells.Fail();
                spells.I32(ids[i]);
            }
        }
    }

    CodecWriter* writers[] = { &battle, &stacks, &squares, &obstacles, &log, &heroes, &relations, &spells };
    const uint32_t ids[] = {
        kSectionBattle, kSectionStacks, kSectionSquares, kSectionObstacles,
        kSectionLog, kSectionHeroes, kSectionRelations, kSectionSpells
    };
    for (size_t i = 0; i < sizeof(writers) / sizeof(writers[0]); ++i) {
        if (!writers[i]->ok) {
            if (error) *error = "capture exceeds an exact-save limit";
            return false;
        }
        hbs::ArchiveSection section;
        section.id = ids[i];
        section.bytes.swap(writers[i]->bytes);
        out->push_back(section);
    }
    return true;
}

static const hbs::ArchiveSection* FindSection_(const std::vector<hbs::ArchiveSection>& sections, uint32_t id)
{
    const hbs::ArchiveSection* found = nullptr;
    for (size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].id != id) continue;
        if (found) return nullptr;
        found = &sections[i];
    }
    return found;
}

static bool ReadIntArray_(CodecReader* reader, int32_t* out, int count)
{
    for (int i = 0; i < count; ++i) out[i] = reader->I32();
    return reader->ok;
}

static bool CodecDecode(const std::vector<hbs::ArchiveSection>& sections, CodecCapture* out, std::string* error)
{
    if (!out) return false;
    *out = CodecCapture();
    const hbs::ArchiveSection* battle = FindSection_(sections, kSectionBattle);
    const hbs::ArchiveSection* stacks = FindSection_(sections, kSectionStacks);
    const hbs::ArchiveSection* squares = FindSection_(sections, kSectionSquares);
    const hbs::ArchiveSection* obstacles = FindSection_(sections, kSectionObstacles);
    const hbs::ArchiveSection* log = FindSection_(sections, kSectionLog);
    const hbs::ArchiveSection* heroes = FindSection_(sections, kSectionHeroes);
    const hbs::ArchiveSection* relations = FindSection_(sections, kSectionRelations);
    const hbs::ArchiveSection* spells = FindSection_(sections, kSectionSpells);
    if (!battle || !stacks || !squares || !obstacles || !log || !heroes || !relations || !spells) {
        if (error) *error = "capture is missing a required section";
        return false;
    }

    CodecReader battleReader(battle->bytes.data(), battle->bytes.size());
    out->version = battleReader.U32();
    if (out->version != kCodecVersion) { if (error) *error = "unsupported capture version"; return false; }
    int32_t* fields[] = {
        &out->action, &out->actionParameter, &out->actionTarget, &out->actionParameter2,
        &out->landType, &out->absoluteObstacleId, &out->siegeKind, &out->hasMoat,
        &out->specialTerrain, &out->heroSpellPower[0], &out->heroSpellPower[1],
        &out->heroOwner[0], &out->heroOwner[1], &out->heroMonCount[0], &out->heroMonCount[1],
        &out->turnsSinceLastEnchanterCast[0], &out->turnsSinceLastEnchanterCast[1],
        &out->summonedMonster[0], &out->summonedMonster[1], &out->currentMonSide,
        &out->currentMonIndex, &out->currentActiveSide, &out->autoCombat,
        &out->creatureAtMousePos, &out->mouseCoord, &out->attackerCoord, &out->moveType,
        &out->siegeKind2, &out->finished, &out->necromancyRaisedAmount,
        &out->necromancyRaisedMonsters, &out->turn, &out->tacticsDifference, &out->waitPhase
    };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) *fields[i] = battleReader.I32();
    out->antiMagicGarrison = battleReader.U8();
    out->creatureBank = battleReader.U8();
    out->boatCombat = battleReader.U8();
    for (int side = 0; side < 2; ++side) {
        out->isNotAI[side] = battleReader.U8();
        out->isHuman[side] = battleReader.U8();
        out->artifactAutoCast[side] = battleReader.U8();
        out->heroCasted[side] = battleReader.U8();
    }
    out->tacticsPhase = battleReader.U8();
    if (!ReadIntArray_(&battleReader, out->fortWallsHp, 18)
        || !ReadIntArray_(&battleReader, out->fortWallsAlive, 18)) return false;
    battleReader.Bytes(out->massSpellTarget, sizeof(out->massSpellTarget));
    battleReader.Bytes(out->accessibleSquares, sizeof(out->accessibleSquares));
    battleReader.Bytes(out->accessibleSquares2, sizeof(out->accessibleSquares2));
    for (int side = 0; side < 2; ++side) out->spellPoints[side] = (int16_t)battleReader.U16();
    out->rngTlsSeed = battleReader.U32();
    out->rngMirrorSeed = battleReader.U32();
    for (int side = 0; side < 2; ++side) {
        const uint32_t count = battleReader.U32();
        if (count > 1024) battleReader.ok = false;
        out->eagleEye[side].resize(battleReader.ok ? count : 0);
        for (uint32_t i = 0; i < count && battleReader.ok; ++i)
            out->eagleEye[side][i] = battleReader.I32();
    }
    if (!battleReader.Finish()) { if (error) *error = "battle section is corrupt"; return false; }

    CodecReader stackReader(stacks->bytes.data(), stacks->bytes.size());
    if (stackReader.U32() != kCodecVersion) return false;
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 21; ++slot)
            ReadStackScalars_(&stackReader, &out->stacks[side][slot]);
    if (!stackReader.Finish()) { if (error) *error = "stack section is corrupt"; return false; }

    CodecReader squareReader(squares->bytes.data(), squares->bytes.size());
    if (squareReader.U32() != kCodecVersion || squareReader.U32() != 187) return false;
    for (int i = 0; i < 187; ++i) {
        CodecSquare& square = out->squares[i];
        square.obstacleBits = squareReader.U8();
        square.obstacleIndex = squareReader.I32();
        square.stackSide = (int8_t)squareReader.U8();
        square.stackIndex = (int8_t)squareReader.U8();
        square.twoHexMonsterSquare = squareReader.U8();
        square.deadStacksNumber = squareReader.I32();
        if (square.deadStacksNumber < 0 || square.deadStacksNumber > 14) squareReader.ok = false;
        for (int dead = 0; squareReader.ok && dead < square.deadStacksNumber; ++dead) {
            square.deadStackSide[dead] = (int8_t)squareReader.U8();
            square.deadStackIndex[dead] = (int8_t)squareReader.U8();
            square.belongsToAttacker[dead] = squareReader.U8();
        }
        square.availableForLeftSquare = squareReader.U8();
        square.availableForRightSquare = squareReader.U8();
    }
    if (!squareReader.Finish()) { if (error) *error = "square section is corrupt"; return false; }

    CodecReader obstacleReader(obstacles->bytes.data(), obstacles->bytes.size());
    if (obstacleReader.U32() != kCodecVersion) return false;
    const uint32_t obstacleCount = obstacleReader.U32();
    if (obstacleCount > 4096) return false;
    out->obstacles.resize(obstacleCount);
    for (uint32_t i = 0; i < obstacleCount; ++i) {
        CodecObstacle& item = out->obstacles[i];
        item.infoIndex = obstacleReader.I32();
        item.anchorHex = obstacleReader.U8();
        item.ownerSide = (int8_t)obstacleReader.U8();
        item.featureTriggered = obstacleReader.U8();
        item.featureDamage = obstacleReader.U32();
        item.featureDuration = obstacleReader.U32();
        item.animationIndex = obstacleReader.U32();
    }
    if (!obstacleReader.Finish()) return false;

    CodecReader logReader(log->bytes.data(), log->bytes.size());
    if (logReader.U32() != kCodecVersion) return false;
    const uint32_t logCount = logReader.U32();
    if (logCount > 100000) return false;
    out->logLines.resize(logCount);
    for (uint32_t i = 0; i < logCount; ++i) {
        const uint16_t length = logReader.U16();
        out->logLines[i].assign(length, '\0');
        if (length) logReader.Bytes(&out->logLines[i][0], length);
    }
    if (!logReader.Finish()) return false;

    CodecReader heroReader(heroes->bytes.data(), heroes->bytes.size());
    if (heroReader.U32() != kCodecVersion) return false;
    for (int side = 0; side < 2; ++side) {
        const int16_t points = (int16_t)heroReader.U16();
        if (points != out->spellPoints[side]) heroReader.ok = false;
    }
    if (!heroReader.Finish()) return false;

    CodecReader relationReader(relations->bytes.data(), relations->bytes.size());
    if (relationReader.U32() != kCodecVersion) return false;
    for (int side = 0; side < 2 && relationReader.ok; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            for (int vector = 0; vector < 4; ++vector) {
                const uint16_t count = relationReader.U16();
                if (count > 42) relationReader.ok = false;
                std::vector<CodecIdentity>& items = out->stacks[side][slot].relations[vector];
                items.resize(relationReader.ok ? count : 0);
                for (uint16_t i = 0; relationReader.ok && i < count; ++i) {
                    items[i].side = relationReader.I32();
                    items[i].slot = relationReader.I32();
                    if (items[i].side < 0 || items[i].side > 1 || items[i].slot < 0 || items[i].slot > 20)
                        relationReader.ok = false;
                }
            }
        }
    }
    if (!relationReader.Finish()) return false;

    CodecReader spellReader(spells->bytes.data(), spells->bytes.size());
    if (spellReader.U32() != kCodecVersion) return false;
    for (int side = 0; side < 2 && spellReader.ok; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            const uint32_t count = spellReader.U32();
            if (count > 100000) spellReader.ok = false;
            std::vector<int32_t>& ids = out->stacks[side][slot].spellIds;
            ids.resize(spellReader.ok ? count : 0);
            for (uint32_t i = 0; spellReader.ok && i < count; ++i) {
                ids[i] = spellReader.I32();
                if (ids[i] < 0 || ids[i] >= 81) spellReader.ok = false;
            }
        }
    }
    if (!spellReader.Finish()) { if (error) *error = "spell section is corrupt"; return false; }
    return true;
}
