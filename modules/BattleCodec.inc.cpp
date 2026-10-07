// ========== BattleCodec.inc.cpp ==========
// 把一次战斗时刻编码成长度明确的二进制段。
// 本文件不访问游戏对象；超限直接失败，不截断日志或容器。

#include <memory>
#include <utility>

static const uint32_t kSectionBattle = 1;
static const uint32_t kSectionStacks = 2;
static const uint32_t kSectionSquares = 3;
static const uint32_t kSectionObstacles = 4;
static const uint32_t kSectionLog = 5;
static const uint32_t kSectionHeroes = 6;
static const uint32_t kSectionRelations = 7;
static const uint32_t kSectionSpells = 8;
static const uint32_t kCodecVersion = 6;

struct CodecScalarRange_ { uint32_t offset, size; };
// Audited numeric fields omitted by H3API; never includes resource/container pointers.
static constexpr CodecScalarRange_ kStackExtraRanges_[] = {
    {0x01, 4}, {0x08, 5}, {0x10, 0x11}, {0x24, 4}, {0x30, 1}, {0x64, 8},
    {0x74, 8}, {0x94, 0x2C}, {0xE0, 8}, {0xE8, 1}, {0xF0, 1},
    {0xFC, 4}, {0x108, 0x5C}, {0x16C, 4}, {0x190, 4}, {0x450, 4},
    {0x474, 4}, {0x4A4, 4}, {0x4D8, 1}, {0x534, 4}, {0x53C, 12}
};
static constexpr size_t ScalarRangesSize_(const CodecScalarRange_* ranges, size_t count) {
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) total += ranges[i].size;
    return total;
}
static constexpr size_t StackExtraSize_() {
    return ScalarRangesSize_(kStackExtraRanges_, sizeof(kStackExtraRanges_) / sizeof(kStackExtraRanges_[0]));
}
static constexpr CodecScalarRange_ kManagerExtraRanges_[] = {
    {0x5398, 8}, {0x53A9, 1}, {0x53C7, 1}, {0x53DC, 0x28},
    {0x5414, 8}, {0x54B2, 2}, {0x1329C, 2},
    {0x132B0, 4}, {0x13430, 0x31}, {0x13D48, 4},
    {0x13D74, 3}, {0x13DE8, 0x10}, {0x13FFC, 4}, {0x1402F, 2},
    // Native creature path cache; the shallow shade source is accessibleSquares2.
    // H3API's tail fields are one byte early; use the audited native range.
    {0x14031, 187}
};
static constexpr size_t ManagerExtraSize_() {
    return ScalarRangesSize_(kManagerExtraRanges_, sizeof(kManagerExtraRanges_) / sizeof(kManagerExtraRanges_[0]));
}

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
    // Preserve resource ownership: flags and audited numeric combat fields only.
    uint32_t infoFlags;
    // v3: numeric combat info +0x4C..+0x68; never serialize resource strings.
    int32_t infoCombat[8];
    int32_t defendingDelta;
    int32_t animationSpeed;
    int32_t movementDirection;
    int32_t renderOffsetY;
    int32_t renderOffsetX;
    uint8_t extraScalars[StackExtraSize_()];
    CodecIdentity aiTarget = {-1, -1};
    std::vector<int32_t> spellIds;
    std::vector<CodecIdentity> relations[4];
};

static constexpr CodecScalarRange_ kSquareExtraRanges_[] = {{0, 14}, {0x4D, 1}, {0x50, 32}};
static constexpr size_t SquareExtraSize_() {
    return ScalarRangesSize_(kSquareExtraRanges_, sizeof(kSquareExtraRanges_) / sizeof(kSquareExtraRanges_[0]));
}
struct CodecSquare
{
    uint8_t extraScalars[SquareExtraSize_()];
    // v4: obstacleIndex was dropped. It is a product of the live vector layout and
    // is recomputed from the rebuilt entry order; only the bits are ground truth.
    uint8_t obstacleBits;
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
    // v4: stable kind identity replaces the old pointer-difference infoIndex, which
    // truncated for spell obstacles (their static H3ObstacleInfo entries are not on
    // the 0x14 terrain-table grid). 0..90 = terrain table index, 100..104 = the five
    // spell obstacles (quicksand, land mine, force field 2/3 cells, fire wall).
    uint16_t kindId;
    uint8_t anchorHex;
    int8_t ownerSide;
    uint8_t featureTriggered;
    uint32_t featureDamage;
    uint32_t featureDuration;
    uint32_t animationIndex;
    // Squares the game itself would touch (FUN_00466590 row-parity logic applied
    // at capture time), so policy and rebuild never re-derive layout from pointers.
    uint8_t cellCount;
    uint8_t cells[8];
    // Def name cross-checks the live kind table at restore time; never a load key.
    char defName[16];
};

static bool ObstacleKindValid_(uint16_t kindId)
{
    return kindId <= 90 || (kindId >= 100 && kindId <= 104);
}

// Square bits each kind sets on its relative cells (anchor hex additionally gets bit 0).
static inline uint8_t ObstacleKindBits_(uint16_t kindId)
{
    switch (kindId) {
        case 100: return 0x04;    // quicksand
        case 101: return 0x08;    // land mine
        case 102: return 0x22;    // force field (localObstacle | forcefield)
        case 103: return 0x22;    // force field
        case 104: return 0x10;    // fire wall
        default: return 0x02;     // terrain table obstacle
    }
}

static bool CodecObstacleKeyLess_(const CodecObstacle& a, const CodecObstacle& b)
{
    if (a.kindId != b.kindId) return a.kindId < b.kindId;
    return a.anchorHex < b.anchorHex;
}

struct CodecTower_ {
    // type, x, y, facing, sequence, frame index, defending stack slot.
    int32_t scalars[7];
    char defName[13], missileName[13];
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
    uint32_t heroCasted[2];
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
    uint8_t extraScalars[ManagerExtraSize_()];
    CodecTower_ towers[3];
    char wallPcxNames[90][13];
    CodecSquare squares[187];
    std::vector<CodecObstacle> obstacles;
    std::vector<std::string> logLines;
    CodecStack stacks[2][21];
};

static void CodecReset_(CodecCapture* capture)
{
    std::unique_ptr<CodecCapture> empty(new CodecCapture{});
    *capture = std::move(*empty);
}

// 2026-10-07 玩家日志实证：换阵重打的新战斗（同指纹、generation 递增）里，原生
// aiTarget(+0x538) 可指向当前空置槽（type==-1 但指针仍在管理器 42 格数组内）。
// 采集合法（StackIdentity_ 只认指针在数组内），RestorePolicy_ 对 before 快照跑同
// 一检查却会拒绝整个读档。这类悬挂链接是原生瞬态：AI 目标和光环/缠绕关系向量都
// 可能残留上一场或初始化预置的陈旧指针；重放它们正是 0x43E720 一类陈旧链接崩溃
// 的来源。采集完成后与旧档解码后统一规范化：只保留指向常规占用槽（side 0..1、
// slot 0..19 且 occupied）的瞬态链接，其余丢弃并计数，AI 之后自行重算目标。
struct CodecLinkDropReport_
{
    uint32_t aiTargets;
    uint32_t relationEntries;
};

static CodecLinkDropReport_ CodecNormalizeStaleLinks_(CodecCapture& capture)
{
    CodecLinkDropReport_ report = {0, 0};
    const auto targetOccupied = [&capture](const CodecIdentity& id) {
        return id.side >= 0 && id.side <= 1 && id.slot >= 0 && id.slot < 20
            && capture.stacks[id.side][id.slot].occupied;
    };
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            CodecStack& stack = capture.stacks[side][slot];
            if (!stack.occupied) continue;
            const bool aiMismatched = (stack.aiTarget.side == -1) != (stack.aiTarget.slot == -1);
            if (aiMismatched || (stack.aiTarget.side != -1 && !targetOccupied(stack.aiTarget))) {
                stack.aiTarget = {-1, -1};
                ++report.aiTargets;
            }
            for (int vector = 0; vector < 4; ++vector) {
                std::vector<CodecIdentity>& items = stack.relations[vector];
                size_t kept = 0;
                for (size_t i = 0; i < items.size(); ++i) {
                    if (targetOccupied(items[i])) items[kept++] = items[i];
                    else ++report.relationEntries;
                }
                items.resize(kept);
            }
        }
    }
    return report;
}

// Native bottom-control gates, independent of unit action/turn progression.
static bool CodecWaitControlEnabled_(uint8_t waitPhase, bool tacticsPhase)
{
    return waitPhase == 0 && !tacticsPhase;
}

static bool CodecDefendControlEnabled_(bool tacticsPhase)
{
    return !tacticsPhase;
}

static bool CodecSpellControlEnabled_(bool tacticsPhase, bool hasHero,
    uint32_t heroCasted, bool castOverride, bool hasSpellbook)
{
    return !tacticsPhase && hasHero && (!heroCasted || castOverride) && hasSpellbook;
}

// 145 catapult .. 148 ammo cart war machines plus the 149 arrow tower use
// native machine DEFs that never satisfy the strict creature-frame gate;
// same-battle restore renders them through the DEF the game already draws.
static bool CodecDefFrameGateApplies_(int32_t type)
{
    return type < 145;
}

static void CodecInvalidateHover_(CodecCapture* capture)
{
    capture->creatureAtMousePos = -1;
    capture->mouseCoord = -1;
    capture->attackerCoord = -1;
    capture->moveType = -99;
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 20; ++slot)
            capture->stacks[side][slot].highlightContour = 0;
}

// Restore caches independently of the player's display option. Native drawing
// gates shallow shading on 0x698814 or tacticsPhase, not on cache emptiness.
static void CodecRestoreDisplayCaches_(void* previous, void* current, void* path, const CodecCapture& capture)
{
    memset(previous, 0, sizeof(capture.accessibleSquares));
    memset(current, 0, sizeof(capture.accessibleSquares2));
    memset(path, 0, 187);
    memcpy(previous, capture.accessibleSquares, sizeof(capture.accessibleSquares));
    memcpy(current, capture.accessibleSquares2, sizeof(capture.accessibleSquares2));
    size_t packed = 0;
    for (const auto& range : kManagerExtraRanges_) {
        if (range.offset == 0x14031u) {
            memcpy(path, capture.extraScalars + packed, range.size);
            return;
        }
        packed += range.size;
    }
}

static float CodecStackFloat_(const CodecStack& stack, uint32_t offset) {
    size_t packed = 0;
    for (const auto& range : kStackExtraRanges_) {
        if (offset >= range.offset && offset + 4 <= range.offset + range.size) {
            float value = 0;
            memcpy(&value, stack.extraScalars + packed + offset - range.offset, 4);
            return value;
        }
        packed += range.size;
    }
    return 0;
}
static bool CodecStackPositionValid_(const CodecStack& stack) {
    return stack.type == 149 ? (stack.side == 1 && (stack.position == 251 || stack.position == 254 || stack.position == 255))
        : (stack.position >= -1 && stack.position < 187 && (!stack.numberAlive || stack.position >= 0));
}

// Pure preflight on the saved payload itself. The live battlefield is replaced
// wholesale on restore, so it never participates in these checks; only the
// archive's own physical ranges (array bounds, native-call safety) are gated.
static bool RestorePolicy_(const CodecCapture& saved, std::string* error)
{
    auto reject = [&](const char* why) { if (error) *error = why; return false; };
    if (saved.version != kCodecVersion)
        return reject("存档数据版本不兼容，请重新保存战场存档");
    if (saved.action || saved.finished || saved.autoCombat || saved.tacticsPhase
        || saved.currentMonSide < 0 || saved.currentMonSide > 1
        || saved.currentMonIndex < 0 || saved.currentMonIndex >= 20
        || saved.currentActiveSide < 0 || saved.currentActiveSide > 1
        || !saved.isHuman[saved.currentActiveSide] || saved.turn < 0)
        return reject("saved state is not a player waiting turn");
    for (int side = 0; side < 2; ++side)
        if (saved.heroMonCount[side] < 0 || saved.heroMonCount[side] > 20)
            return reject("battle participant count exceeds supported slots");
    if (saved.siegeKind2 < 0 || saved.siegeKind2 > 3)
        return reject("城防等级超出有效范围");
    // Non-fortified battles never index door resources and may retain stale door bytes.
    if (saved.siegeKind2 > 0 && (saved.siegeKind < 0 || saved.siegeKind > 3))
        return reject("城门状态超出有效范围");
    for (int wall = 0; wall < 18; ++wall)
        if (saved.fortWallsHp[wall] < 0 || saved.fortWallsHp[wall] > 1000000
            || saved.fortWallsAlive[wall] < 0 || saved.fortWallsAlive[wall] > 4)
            return reject("城墙状态超出有效范围");
    for (const auto& name : saved.wallPcxNames)
        if (!memchr(name, 0, sizeof(name))) return reject("城墙图像资源名无效");
    const int towerHex[] = {254, 251, 255};
    for (int tower = 0; tower < 3; ++tower) {
        const CodecTower_& t = saved.towers[tower];
        if (!memchr(t.defName, 0, sizeof(t.defName)) || !memchr(t.missileName, 0, sizeof(t.missileName)))
            return reject("箭塔资源名无效");
        if (!t.defName[0] && !t.missileName[0]) continue;
        const int slot = t.scalars[6];
        if (!saved.siegeKind2 || t.scalars[0] < 0 || t.scalars[0] > 150
            || t.scalars[3] < 0 || t.scalars[3] > 1 || t.scalars[4] < 0 || t.scalars[4] > 1023
            || t.scalars[5] < 0 || slot < 0 || slot >= 20
            || !saved.stacks[1][slot].occupied || saved.stacks[1][slot].type != 149
            || saved.stacks[1][slot].position != towerHex[tower])
            return reject("箭塔状态或关联槽位无效");
    }
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            const CodecStack& s = saved.stacks[side][slot];
            if (s.occupied) {
                if (s.spellIds.size() > 100000) return reject("saved spell deque too large");
                for (int spell : s.spellIds)
                    if (spell < 0 || spell >= 81) return reject("saved spell id invalid");
                for (int v = 0; v < 4; ++v) {
                    if (s.relations[v].size() > 100000) return reject("saved relation vector too large");
                    for (const CodecIdentity& id : s.relations[v])
                        if (id.side < 0 || id.side > 1 || id.slot < 0 || id.slot >= 20
                            || !saved.stacks[id.side][id.slot].occupied)
                            return reject("saved relation target invalid");
                }
            }
            if (slot == 20) {
                // Reserved storage: partially constructed and never consumed by
                // native combat logic. Save and write back as-is; only physical
                // ranges and the AI pointer target apply (no emptiness or
                // side/slot identity requirements).
                if (s.occupied) {
                    if (s.type < 0 || s.type > 0x95 || !CodecStackPositionValid_(s)
                        || s.activeSpellNumber < 0 || s.activeSpellNumber > 81
                        || s.renderOffsetX < -1000000 || s.renderOffsetX > 1000000
                        || s.renderOffsetY < -1000000 || s.renderOffsetY > 1000000)
                        return reject("reserved slot scalar outside valid range");
                    if ((s.aiTarget.side == -1) != (s.aiTarget.slot == -1)
                        || (s.aiTarget.side != -1 && (s.aiTarget.side < 0 || s.aiTarget.side > 1
                        || s.aiTarget.slot < 0 || s.aiTarget.slot >= 20
                        || !saved.stacks[s.aiTarget.side][s.aiTarget.slot].occupied)))
                        return reject("reserved slot AI target invalid");
                }
                continue;
            }
            if (!s.occupied) continue;
            if (s.side != side || s.sideIndex != slot)
                return reject("saved stack slot reference invalid");
            if (s.type < 0 || s.type > 0x95 || s.numberAlive < 0 || s.numberForeverDead < 0
                || s.numberAtStart < 0 || s.numberAlive > s.numberAtStart
                || s.healthLost < 0 || s.infoCombat[0] <= 0 || s.healthLost >= s.infoCombat[0]
                || !CodecStackPositionValid_(s)
                || s.secondHexOrientation < -1 || s.secondHexOrientation > 1
                || s.activeSpellNumber < 0 || s.activeSpellNumber > 81
                || s.renderOffsetX < -1000000 || s.renderOffsetX > 1000000
                || s.renderOffsetY < -1000000 || s.renderOffsetY > 1000000)
                return reject("saved stack scalar outside valid range");
            const float effects[] = {s.frenzyMultiplier, s.blindEffect, s.fireShieldEffect,
                s.protectionAirEffect, s.protectionFireEffect, s.protectionWaterEffect,
                s.protectionEarthEffect, s.shieldEffect, s.airShieldEffect, s.slowEffect,
                CodecStackFloat_(s, 0x450), CodecStackFloat_(s, 0x4A4)};
            for (float f : effects)
                if (!(f >= -1000000.0f && f <= 1000000.0f)) return reject("non-finite spell effect");
            if ((s.aiTarget.side == -1 && s.aiTarget.slot != -1)
                || (s.aiTarget.side != -1 && (s.aiTarget.side < 0 || s.aiTarget.side > 1
                || s.aiTarget.slot < 0 || s.aiTarget.slot >= 20
                || !saved.stacks[s.aiTarget.side][s.aiTarget.slot].occupied)))
                return reject("saved AI target invalid");
        }
    }
    const CodecStack& active = saved.stacks[saved.currentMonSide][saved.currentMonIndex];
    if (!active.occupied || active.numberAlive <= 0) return reject("saved active stack is not alive");
    // v4: obstacles are a rebuild payload. The live set is re-derived by the diff in
    // RestoreApply_, so live-vs-saved equality is no longer required here; instead the
    // saved payload needs bounded geometry and unique claims for safe native calls.
    // Grid bits are independent saved data: HD/plugin processing can move an anchor
    // flag without moving the obstacle entry. Restore writes the recorded bits.
    {
        if (saved.obstacles.size() > 4096) return reject("saved obstacle count exceeds 4096");
        uint16_t claimed[187];
        memset(claimed, 0, sizeof(claimed));
        for (size_t i = 0; i < saved.obstacles.size(); ++i) {
            const CodecObstacle& s = saved.obstacles[i];
            if (!ObstacleKindValid_(s.kindId)) return reject("saved obstacle kind outside known table");
            if (s.anchorHex >= 187 || s.cellCount > 8) return reject("saved obstacle geometry invalid");
            if (s.ownerSide < -1 || s.ownerSide > 1) return reject("saved obstacle owner side invalid");
            if (s.defName[0] == '\0' || memchr(s.defName, '\0', sizeof(s.defName)) == nullptr)
                return reject("saved obstacle def name invalid");
            for (uint8_t c = 0; c < s.cellCount; ++c)
                if (s.cells[c] >= 187) return reject("saved obstacle cell off board");
            if (claimed[s.anchorHex])
                return reject("saved obstacles overlap on one hex");
            claimed[s.anchorHex] = (uint16_t)(i + 1);
            for (uint8_t c = 0; c < s.cellCount; ++c) {
                const uint8_t hex = s.cells[c];
                if (claimed[hex] && claimed[hex] != (uint16_t)(i + 1))
                    return reject("saved obstacles overlap on one hex");
                claimed[hex] = (uint16_t)(i + 1);
            }
        }
    }
    for (int i = 0; i < 187; ++i) {
        const CodecSquare& s = saved.squares[i];
        if (s.stackSide >= 0 && s.stackIndex == 20)
            return reject("battlefield references reserved slot");
        if (s.deadStacksNumber < 0 || s.deadStacksNumber > 14) return reject("corpse count invalid");
        auto validRef = [&](int side, int slot) {
            return side >= 0 && side < 2 && slot >= 0 && slot < 20 && saved.stacks[side][slot].occupied;
        };
        if (s.stackSide >= 0 && (!validRef(s.stackSide, s.stackIndex)
            || saved.stacks[s.stackSide][s.stackIndex].numberAlive <= 0))
            return reject("square references absent stack");
        if (s.stackSide >= 0) {
            const CodecStack& unit = saved.stacks[s.stackSide][s.stackIndex];
            const int second = unit.position + (unit.secondHexOrientation ? 1 : -1);
            const bool wide = (unit.infoFlags & 1) != 0;
            const int primaryFlag = wide ? (unit.secondHexOrientation == 0) : 0xFF;
            const bool primary = unit.position == i && s.twoHexMonsterSquare == primaryFlag;
            const bool secondary = wide && i == second
                && s.twoHexMonsterSquare == (unit.secondHexOrientation != 0);
            if (!primary && !secondary)
                return reject("square position or second hex inconsistent");
        }
        for (int d = 0; d < s.deadStacksNumber; ++d)
            if (!validRef(s.deadStackSide[d], s.deadStackIndex[d])) return reject("corpse identity invalid");
    }
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 20; ++slot) {
            const CodecStack& s = saved.stacks[side][slot];
            if (!s.occupied || !s.numberAlive || s.type == 149) continue;
            const CodecSquare& square = saved.squares[s.position];
            const int primaryFlag = (s.infoFlags & 1) ? (s.secondHexOrientation == 0) : 0xFF;
            if (square.stackSide != side || square.stackIndex != slot || square.twoHexMonsterSquare != primaryFlag)
                return reject("stack and battlefield position disagree");
            if (s.infoFlags & 1) {
                const int second = s.position + (s.secondHexOrientation ? 1 : -1);
                if (s.secondHexOrientation < 0 || second < 0 || second >= 187
                    || second / 17 != s.position / 17
                    || saved.squares[second].stackSide != side || saved.squares[second].stackIndex != slot
                    || saved.squares[second].twoHexMonsterSquare != (s.secondHexOrientation != 0))
                    return reject("double-wide second hex missing");
            }
        }

    return true;
}
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
    writer->U32(stack.infoFlags);
    for (int i = 0; i < 8; ++i) writer->I32(stack.infoCombat[i]);
    writer->I32(stack.defendingDelta);
    writer->I32(stack.animationSpeed);
    writer->I32(stack.movementDirection);
    writer->I32(stack.renderOffsetY);
    writer->I32(stack.renderOffsetX);
    writer->Bytes(stack.extraScalars, sizeof(stack.extraScalars));
    writer->I32(stack.aiTarget.side);
    writer->I32(stack.aiTarget.slot);
}

// Slot 20 stays partially constructed native storage: captured as-is, restored
// as-is, and never required to be empty or match participant identity fields.

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
    stack->infoFlags = reader->U32();
    for (int i = 0; i < 8; ++i) stack->infoCombat[i] = reader->I32();
    stack->defendingDelta = reader->I32();
    stack->animationSpeed = reader->I32();
    stack->movementDirection = reader->I32();
    stack->renderOffsetY = reader->I32();
    stack->renderOffsetX = reader->I32();
    reader->Bytes(stack->extraScalars, sizeof(stack->extraScalars));
    stack->aiTarget.side = reader->I32();
    stack->aiTarget.slot = reader->I32();
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
        battle.U32(capture.heroCasted[side]);
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
    battle.Bytes(capture.extraScalars, sizeof(capture.extraScalars));
    for (const auto& tower : capture.towers) {
        for (int scalar : tower.scalars) battle.I32(scalar);
        battle.Bytes(tower.defName, sizeof(tower.defName));
        battle.Bytes(tower.missileName, sizeof(tower.missileName));
    }
    battle.Bytes(capture.wallPcxNames, sizeof(capture.wallPcxNames));
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
        squares.Bytes(square.extraScalars, sizeof(square.extraScalars));
        squares.U8(square.obstacleBits);
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
        const uint8_t nameLength = (uint8_t)strnlen(item.defName, sizeof(item.defName));
        if (!nameLength || nameLength >= sizeof(item.defName) || item.cellCount > 8) {
            obstacles.Fail();
            break;
        }
        obstacles.U16(item.kindId);
        obstacles.U8(item.anchorHex);
        obstacles.U8((uint8_t)item.ownerSide);
        obstacles.U8(item.featureTriggered);
        obstacles.U32(item.featureDamage);
        obstacles.U32(item.featureDuration);
        obstacles.U32(item.animationIndex);
        obstacles.U8(item.cellCount);
        obstacles.Bytes(item.cells, item.cellCount);
        obstacles.U8(nameLength);
        obstacles.Bytes(item.defName, nameLength);
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

// 比较编码字节，不比较对象/容器头；长度不同时 firstDiff 指向共同前缀末尾。
static bool CodecSectionEqual_(const hbs::ArchiveSection& expected,
    const hbs::ArchiveSection& actual, size_t* firstDiff)
{
    size_t first = 0;
    const size_t limit = (std::min)(expected.bytes.size(), actual.bytes.size());
    while (first < limit && expected.bytes[first] == actual.bytes[first]) ++first;
    if (firstDiff) *firstDiff = first;
    return expected.id == actual.id && first == limit
        && expected.bytes.size() == actual.bytes.size();
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
    if (error) error->clear();
    if (!out) { if (error) *error = "null capture output"; return false; }
    CodecReset_(out);
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
        out->heroCasted[side] = battleReader.U32();
    }
    out->tacticsPhase = battleReader.U8();
    if (!ReadIntArray_(&battleReader, out->fortWallsHp, 18)
        || !ReadIntArray_(&battleReader, out->fortWallsAlive, 18)) { if (error) *error = "battle wall arrays are truncated"; return false; }
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
    battleReader.Bytes(out->extraScalars, sizeof(out->extraScalars));
    for (auto& tower : out->towers) {
        for (int32_t& scalar : tower.scalars) scalar = battleReader.I32();
        battleReader.Bytes(tower.defName, sizeof(tower.defName));
        battleReader.Bytes(tower.missileName, sizeof(tower.missileName));
    }
    battleReader.Bytes(out->wallPcxNames, sizeof(out->wallPcxNames));
    if (!battleReader.Finish()) { if (error) *error = "battle section is corrupt"; return false; }

    CodecReader stackReader(stacks->bytes.data(), stacks->bytes.size());
    if (stackReader.U32() != kCodecVersion) { if (error) *error = "stack section version mismatch"; return false; }
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 21; ++slot)
            ReadStackScalars_(&stackReader, &out->stacks[side][slot]);
    if (!stackReader.Finish()) { if (error) *error = "stack section is corrupt"; return false; }

    CodecReader squareReader(squares->bytes.data(), squares->bytes.size());
    if (squareReader.U32() != kCodecVersion || squareReader.U32() != 187) { if (error) *error = "square section header mismatch"; return false; }
    for (int i = 0; i < 187; ++i) {
        CodecSquare& square = out->squares[i];
        squareReader.Bytes(square.extraScalars, sizeof(square.extraScalars));
        square.obstacleBits = squareReader.U8();
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
    if (obstacleReader.U32() != kCodecVersion) { if (error) *error = "obstacle section version mismatch"; return false; }
    const uint32_t obstacleCount = obstacleReader.U32();
    if (obstacleCount > 4096) { if (error) *error = "obstacle count exceeds 4096"; return false; }
    out->obstacles.resize(obstacleCount);
    for (uint32_t i = 0; i < obstacleCount; ++i) {
        CodecObstacle& item = out->obstacles[i];
        item.kindId = obstacleReader.U16();
        item.anchorHex = obstacleReader.U8();
        item.ownerSide = (int8_t)obstacleReader.U8();
        item.featureTriggered = obstacleReader.U8();
        item.featureDamage = obstacleReader.U32();
        item.featureDuration = obstacleReader.U32();
        item.animationIndex = obstacleReader.U32();
        item.cellCount = obstacleReader.U8();
        if (item.cellCount > 8) obstacleReader.ok = false;
        memset(item.cells, 0, sizeof(item.cells));
        obstacleReader.Bytes(item.cells, obstacleReader.ok ? item.cellCount : 0);
        const uint8_t nameLength = obstacleReader.U8();
        if (!nameLength || nameLength >= sizeof(item.defName)) obstacleReader.ok = false;
        memset(item.defName, 0, sizeof(item.defName));
        obstacleReader.Bytes(item.defName, obstacleReader.ok ? nameLength : 0);
    }
    if (!obstacleReader.Finish()) { if (error) *error = "obstacle section is corrupt"; return false; }

    CodecReader logReader(log->bytes.data(), log->bytes.size());
    if (logReader.U32() != kCodecVersion) { if (error) *error = "log section version mismatch"; return false; }
    const uint32_t logCount = logReader.U32();
    if (logCount > 100000) { if (error) *error = "log count exceeds 100000"; return false; }
    out->logLines.resize(logCount);
    for (uint32_t i = 0; i < logCount; ++i) {
        const uint16_t length = logReader.U16();
        out->logLines[i].assign(length, '\0');
        if (length) logReader.Bytes(&out->logLines[i][0], length);
    }
    if (!logReader.Finish()) { if (error) *error = "log section is corrupt"; return false; }

    CodecReader heroReader(heroes->bytes.data(), heroes->bytes.size());
    if (heroReader.U32() != kCodecVersion) { if (error) *error = "hero section version mismatch"; return false; }
    for (int side = 0; side < 2; ++side) {
        const int16_t points = (int16_t)heroReader.U16();
        if (points != out->spellPoints[side]) heroReader.ok = false;
    }
    if (!heroReader.Finish()) { if (error) *error = "hero section is corrupt or mana differs from battle section"; return false; }

    CodecReader relationReader(relations->bytes.data(), relations->bytes.size());
    if (relationReader.U32() != kCodecVersion) { if (error) *error = "relation section version mismatch"; return false; }
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
    if (!relationReader.Finish()) { if (error) *error = "relation section is corrupt"; return false; }

    CodecReader spellReader(spells->bytes.data(), spells->bytes.size());
    if (spellReader.U32() != kCodecVersion) { if (error) *error = "spell section version mismatch"; return false; }
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
