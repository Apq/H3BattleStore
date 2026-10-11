// ========== BattleCodec.inc.cpp ==========
// 把一次战斗时刻编码成长度明确的二进制段。
// 本文件不访问游戏对象；超限直接失败，不截断日志或容器。
// 2026-10-11 用户裁定：双格式可切换——二进制 codec 原样保留，另加 JSON 后端
// （CodecEncodeJson/CodecDecodeJson），段数据模型共用，字段一一对应二进制布局。

#include <memory>
#include <utility>

#include "BattleStoreJsonCore.inc.hpp"

static const uint32_t kSectionBattle = 1;
static const uint32_t kSectionStacks = 2;
static const uint32_t kSectionSquares = 3;
static const uint32_t kSectionObstacles = 4;
static const uint32_t kSectionLog = 5;
static const uint32_t kSectionHeroes = 6;
static const uint32_t kSectionRelations = 7;
static const uint32_t kSectionSpells = 8;
// v8（2026-10-10 用户裁定升版）：法力只在 heroes 段存一份（原 battle 段重复字段删除）。
// 旧 v7 档全部拒绝，玩家重新存档。
static const uint32_t kCodecVersion = 8;

// Hero body slots 13..16: ballista, ammo cart, tent, catapult. No pointers.
static constexpr int kMachineBodySlot_ = 13;
static constexpr int kMachineSlotCount_ = 4;
static constexpr size_t kMachineSlotsBytes_ = 32;
static constexpr int32_t kMachineArtifactIds_[] = {4, 5, 6, 3};
struct CodecWarMachine_ {
    int32_t id = -1;
    int32_t subtype = -1;
};
static void CodecCaptureWarMachines_(const void* nativeSlots, CodecWarMachine_ (&out)[4])
{
    const uint8_t* bytes = (const uint8_t*)nativeSlots;
    for (int i = 0; i < kMachineSlotCount_; ++i) {
        memcpy(&out[i].id, bytes + i * 8, 4);
        memcpy(&out[i].subtype, bytes + i * 8 + 4, 4);
    }
}
static void CodecRestoreWarMachines_(void* nativeSlots, const CodecWarMachine_ (&saved)[4])
{
    uint8_t* bytes = (uint8_t*)nativeSlots;
    for (int i = 0; i < kMachineSlotCount_; ++i) {
        memcpy(bytes + i * 8, &saved[i].id, 4);
        memcpy(bytes + i * 8 + 4, &saved[i].subtype, 4);
    }
}
static bool CodecWarMachinesValid_(bool heroPresent, const CodecWarMachine_ (&saved)[4])
{
    for (int i = 0; i < kMachineSlotCount_; ++i) {
        if (saved[i].id != -1 && (!heroPresent || saved[i].id != kMachineArtifactIds_[i])) return false;
        if (saved[i].id == -1 && saved[i].subtype != -1) return false;
    }
    return true;
}

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
    bool heroPresent[2] = {};
    CodecWarMachine_ warMachines[2][kMachineSlotCount_];
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
// native machine DEFs that never satisfy the creature-frame gate;
// same-battle restore renders them through the DEF the game already draws.
// 2026-10-10 17:43 玩家日志实证回归：攻城战 mgr->stacks[1][8].def==0（箭塔
// DEF 挂在攻城塔记录上，不在 stack->def），统一"指针非空"最小集误拒全部
// 攻城/带机器读档——豁免是 2026-10-07 弹药车前案的实证结论，不是臆测，恢复。
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
    for (int side = 0; side < 2; ++side)
        if (!CodecWarMachinesValid_(saved.heroPresent[side], saved.warMachines[side]))
            return reject("invalid hero war-machine equipment");
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
    // 墙阶段 0..4 是五阶段贴图的结构界；HP 上限为拍脑袋值，2026-10-10 用户裁定删除，
    // 仅保留非负（HP 由投石机/魔法累积伤害决定，无游戏上限语义可查）。
    for (int wall = 0; wall < 18; ++wall)
        if (saved.fortWallsHp[wall] < 0
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
        // 朝向 0..1（塔左右朝向）、关联槽 0..19、类型 149、塔位 254/251/255 是结构界。
        // 塔类型参数/DEF 组号/帧号（scalars[0]/[4]/[5]）原上限 150/1023/1023 为拍脑袋值，
        // 2026-10-10 用户裁定删除：存档值来自正在正常渲染的塔，恢复按同名资源加载
        // 同一对象，组号/帧号必然落在对象结构内。
        if (!saved.siegeKind2 || t.scalars[3] < 0 || t.scalars[3] > 1
            || slot < 0 || slot >= 20
            || !saved.stacks[1][slot].occupied || saved.stacks[1][slot].type != 149
            || saved.stacks[1][slot].position != towerHex[tower])
            return reject("箭塔状态或关联槽位无效");
    }
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            const CodecStack& s = saved.stacks[side][slot];
            if (s.occupied) {
                // 08:0x 用户裁定：法术编号白名单全线删除（编码/解码/恢复策略三层
                // 同口径——0..81 是未证实的表容量假设，热血扩表会误拒真实战斗）。
                for (int v = 0; v < 4; ++v) {
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
                    // 绘制偏移原上限 ±1000000 为拍脑袋值，2026-10-10 用户裁定删除。
                    if (s.type < 0 || s.type > 0x95 || !CodecStackPositionValid_(s)
                        || s.activeSpellNumber < 0 || s.activeSpellNumber > 81)
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
            // 类型 0..0x95（生物表）、位置结构界、双格朝向 -1..1、法术数 0..81 均为
            // 结构界保留；绘制偏移原上限 ±1000000 为拍脑袋值，2026-10-10 用户裁定删除。
            if (s.type < 0 || s.type > 0x95 || s.numberAlive < 0 || s.numberForeverDead < 0
                || s.numberAtStart < 0 || s.numberAlive > s.numberAtStart
                || s.healthLost < 0 || s.infoCombat[0] <= 0 || s.healthLost >= s.infoCombat[0]
                || !CodecStackPositionValid_(s)
                || s.secondHexOrientation < -1 || s.secondHexOrientation > 1
                || s.activeSpellNumber < 0 || s.activeSpellNumber > 81)
                return reject("saved stack scalar outside valid range");
            const float effects[] = {s.frenzyMultiplier, s.blindEffect, s.fireShieldEffect,
                s.protectionAirEffect, s.protectionFireEffect, s.protectionWaterEffect,
                s.protectionEarthEffect, s.shieldEffect, s.airShieldEffect, s.slowEffect,
                CodecStackFloat_(s, 0x450), CodecStackFloat_(s, 0x4A4)};
            // 2026-10-10 用户裁定：幅度界 ±1000000 无游戏语义出处，删除；仅查有限性
            // ——NaN 写回游戏内存会扩散进伤害计算，且逐字节 verify 对 NaN 自比较
            // 照样"相等"，必须事前拦截。
            for (float f : effects)
                if (!_finite(f)) return reject("non-finite spell effect");
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
    for (int side = 0; side < 2; ++side) {
        if (!CodecWarMachinesValid_(capture.heroPresent[side], capture.warMachines[side])) {
            if (error) *error = "invalid hero war-machine equipment";
            return false;
        }
    }
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
    // v8：法力不再写入 battle 段（heroes 段已存同一份，重复字段删除）。
    battle.U32(capture.rngTlsSeed);
    battle.U32(capture.rngMirrorSeed);
    for (int side = 0; side < 2; ++side) {
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
        // 尸体数 0..14 由 capture 侧（deadStackSide[15] 数组边界）保证；镜像闸删除。
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
    log.U32((uint32_t)capture.logLines.size());
    for (size_t i = 0; i < capture.logLines.size(); ++i)
        log.Text(capture.logLines[i].data(), capture.logLines[i].size());

    CodecWriter heroes;
    heroes.U32(kCodecVersion);
    for (int side = 0; side < 2; ++side) {
        heroes.U16((uint16_t)capture.spellPoints[side]);
        heroes.U8(capture.heroPresent[side] ? 1 : 0);
        for (const CodecWarMachine_& machine : capture.warMachines[side]) {
            heroes.I32(machine.id);
            heroes.I32(machine.subtype);
        }
    }

    CodecWriter relations;
    relations.U32(kCodecVersion);
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            for (int vector = 0; vector < 4; ++vector) {
                const std::vector<CodecIdentity>& items = capture.stacks[side][slot].relations[vector];
                // U16 计数字段的格式容量事实：超出截断写入（08:0x 裁定：不因容量
                // 拒绝存档；关系向量瞬态容器真实达不到该量级）。
                const size_t limit = items.size() > 0xFFFF ? 0xFFFF : items.size();
                relations.U16((uint16_t)limit);
                for (size_t i = 0; i < limit; ++i) {
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
            spells.U32((uint32_t)ids.size());
            for (size_t i = 0; i < ids.size(); ++i) {
                // 08:0x 裁定：法术编号原值写入（原 0..81 白名单是未证实的表容量
                // 假设，热血环境若扩表会误拒），解码侧同放开。
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
    // v8：法力从 heroes 段读取（battle 段重复字段已删除）。
    out->rngTlsSeed = battleReader.U32();
    out->rngMirrorSeed = battleReader.U32();
    for (int side = 0; side < 2; ++side) {
        const uint32_t count = battleReader.U32();
        // 法术表 81 项结构界（capture 侧 ReadSpellSet_ 同限）。
        if (count > 81) battleReader.ok = false;
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
    // 08:0x 裁定后存档端全量采集：固定上限改为数学界——每行至少 2 字节长度头，
    // 超过剩余容量的条数只可能是损坏文件（内存安全闸，非业务校验）。
    if (logCount > (logReader.size - logReader.pos) / 2) { if (error) *error = "log count exceeds section size"; return false; }
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
        // v8：法力唯一存于此处（battle 段重复字段已删，不再比较）。
        out->spellPoints[side] = (int16_t)heroReader.U16();
        out->heroPresent[side] = heroReader.U8() == 1;
        for (CodecWarMachine_& machine : out->warMachines[side]) {
            machine.id = heroReader.I32();
            machine.subtype = heroReader.I32();
        }
        // 写回白名单：机器装备值会直接 memcpy 进英雄身体槽 13..16，保留。
        if (!CodecWarMachinesValid_(out->heroPresent[side], out->warMachines[side])) heroReader.ok = false;
    }
    if (!heroReader.Finish()) { if (error) *error = "hero section is corrupt"; return false; }

    CodecReader relationReader(relations->bytes.data(), relations->bytes.size());
    if (relationReader.U32() != kCodecVersion) { if (error) *error = "relation section version mismatch"; return false; }
    for (int side = 0; side < 2 && relationReader.ok; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            for (int vector = 0; vector < 4; ++vector) {
                const uint16_t count = relationReader.U16();
                // resize 内存安全闸，与采集侧 ReadPointerRelations_ 可读性上限
                // 同口径 100000；原 42 为未证实推断（2026-10-10 用户裁定删除）。
                if (count > 100000) relationReader.ok = false;
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
                // 08:0x 裁定：与编码端同放开——原 0..81 白名单是未证实的表容量假设。
                ids[i] = spellReader.I32();
            }
        }
    }
    if (!spellReader.Finish()) { if (error) *error = "spell section is corrupt"; return false; }
    return true;
}

// ===================================================================
// JSON 后端（2026-10-11 用户裁定：存档双格式 binary|json）。
// 段数据模型与二进制共用（CodecCapture 不变），字段一一对应二进制布局；
// JSON 帧版本号独立于二进制 codec 版本，从 1 起，由帧层管理。
// 字段级原子展开硬约束：每个数据字段都是显式 JSON 键或数组元素——禁止
// 分隔符伪打包（"0,1,15,20"），禁止 base64/hex 包字节；数组只用于同构
// 条目列表与字节/整数数值列表。整数走 number，浮点走 number（非有限值
// dump 为 null，读回 NaN，交由 RestorePolicy_ 的有限性检查拒绝，与二进制
// 路径同口径）。读侧 schema 校验（存在/类型/字宽/数组尺寸）取代帧 CRC。
// ===================================================================

// stacks 条目：外层 side/index 是定位键，同时承载二进制的冗余身份字段
// stack.side/stack.sideIndex（解码写回同值，RestorePolicy_ 校验自然满足）。
static BattleJson JsonStackEntry_(int side, int slot, const CodecStack& s)
{
    BattleJson obj;
    obj["side"] = side;
    obj["index"] = slot;
    obj["occupied"] = s.occupied ? 1 : 0;
    if (!s.occupied) return obj;
    obj["type"] = s.type;
    obj["position"] = s.position;
    obj["animation"] = s.animation;
    obj["animationFrame"] = s.animationFrame;
    obj["secondHexOrientation"] = s.secondHexOrientation;
    obj["numberAlive"] = s.numberAlive;
    obj["previousNumber"] = s.previousNumber;
    obj["numberForeverDead"] = s.numberForeverDead;
    obj["healthLost"] = s.healthLost;
    obj["slotIndex"] = s.slotIndex;
    obj["numberAtStart"] = s.numberAtStart;
    obj["baseHP"] = s.baseHP;
    obj["isLucky"] = s.isLucky;
    obj["activeSpellNumber"] = s.activeSpellNumber;
    obj["activeSpellDuration"] = JsonInt32Array_(s.activeSpellDuration, 81);
    obj["activeSpellLevel"] = JsonInt32Array_(s.activeSpellLevel, 81);
    obj["retaliations"] = s.retaliations;
    obj["blessDamage"] = s.blessDamage;
    obj["curseDamage"] = s.curseDamage;
    obj["antiMagic"] = s.antiMagic;
    obj["bloodlustEffect"] = s.bloodlustEffect;
    obj["precisionEffect"] = s.precisionEffect;
    obj["weaknessEffect"] = s.weaknessEffect;
    obj["stoneSkinEffect"] = s.stoneSkinEffect;
    obj["prayerEffect"] = s.prayerEffect;
    obj["mirthEffect"] = s.mirthEffect;
    obj["sorrowEffect"] = s.sorrowEffect;
    obj["fortuneEffect"] = s.fortuneEffect;
    obj["misfortuneEffect"] = s.misfortuneEffect;
    obj["slayerType"] = s.slayerType;
    obj["hexesTraveled"] = s.hexesTraveled;
    obj["counterstrikeEffect"] = s.counterstrikeEffect;
    obj["frenzyMultiplier"] = s.frenzyMultiplier;
    obj["blindEffect"] = s.blindEffect;
    obj["fireShieldEffect"] = s.fireShieldEffect;
    obj["protectionAirEffect"] = s.protectionAirEffect;
    obj["protectionFireEffect"] = s.protectionFireEffect;
    obj["protectionWaterEffect"] = s.protectionWaterEffect;
    obj["protectionEarthEffect"] = s.protectionEarthEffect;
    obj["shieldEffect"] = s.shieldEffect;
    obj["airShieldEffect"] = s.airShieldEffect;
    obj["blinded"] = s.blinded;
    obj["paralyzed"] = s.paralyzed;
    obj["forgetfulnessLevel"] = s.forgetfulnessLevel;
    obj["slowEffect"] = s.slowEffect;
    obj["hasteEffect"] = s.hasteEffect;
    obj["diseaseAttackEffect"] = s.diseaseAttackEffect;
    obj["diseaseDefenseEffect"] = s.diseaseDefenseEffect;
    obj["faerieDragonSpell"] = s.faerieDragonSpell;
    obj["magicMirrorEffect"] = s.magicMirrorEffect;
    obj["morale"] = s.morale;
    obj["luck"] = s.luck;
    obj["isDone"] = s.isDone;
    obj["highlightContour"] = s.highlightContour;
    obj["attackedAlready"] = s.attackedAlready;
    obj["isDead"] = s.isDead;
    obj["hasLosses"] = s.hasLosses;
    obj["hasLosses2"] = s.hasLosses2;
    obj["cloneId"] = s.cloneId;
    obj["cloneDuration"] = s.cloneDuration;
    obj["spellToApply"] = s.spellToApply;
    obj["visibility"] = s.visibility;
    obj["infoFlags"] = s.infoFlags;
    obj["infoCombat"] = JsonInt32Array_(s.infoCombat, 8);
    obj["defendingDelta"] = s.defendingDelta;
    obj["animationSpeed"] = s.animationSpeed;
    obj["movementDirection"] = s.movementDirection;
    obj["renderOffsetY"] = s.renderOffsetY;
    obj["renderOffsetX"] = s.renderOffsetX;
    obj["stackExtra"] = JsonByteArray_(s.extraScalars, sizeof(s.extraScalars));
    obj["aiTargetSide"] = s.aiTarget.side;
    obj["aiTargetSlot"] = s.aiTarget.slot;
    return obj;
}

static bool JsonStackFromEntry_(const BattleJson& item, CodecCapture* out)
{
    int32_t side = 0, index = 0, occupied = 0;
    if (!JsonI32_(item, "side", &side) || !JsonI32_(item, "index", &index)
        || !JsonI32_(item, "occupied", &occupied)
        || side < 0 || side > 1 || index < 0 || index > 20 || occupied < 0 || occupied > 1)
        return false;
    CodecStack& s = out->stacks[side][index];
    s.occupied = occupied != 0;
    if (!s.occupied) return true; // 空槽条目仅三个定位键，字段保持解码前零值
    s.side = side;
    s.sideIndex = index;
    if (!JsonI32_(item, "type", &s.type) || !JsonI32_(item, "position", &s.position)
        || !JsonI32_(item, "animation", &s.animation)
        || !JsonI32_(item, "animationFrame", &s.animationFrame)
        || !JsonI32_(item, "secondHexOrientation", &s.secondHexOrientation)
        || !JsonI32_(item, "numberAlive", &s.numberAlive)
        || !JsonI32_(item, "previousNumber", &s.previousNumber)
        || !JsonI32_(item, "numberForeverDead", &s.numberForeverDead)
        || !JsonI32_(item, "healthLost", &s.healthLost)
        || !JsonI32_(item, "slotIndex", &s.slotIndex)
        || !JsonI32_(item, "numberAtStart", &s.numberAtStart)
        || !JsonI32_(item, "baseHP", &s.baseHP)
        || !JsonI32_(item, "isLucky", &s.isLucky)
        || !JsonI32_(item, "activeSpellNumber", &s.activeSpellNumber))
        return false;
    if (!JsonIntsToArray_(item, "activeSpellDuration", s.activeSpellDuration, 81)
        || !JsonIntsToArray_(item, "activeSpellLevel", s.activeSpellLevel, 81))
        return false;
    if (!JsonI32_(item, "retaliations", &s.retaliations)
        || !JsonI32_(item, "blessDamage", &s.blessDamage)
        || !JsonI32_(item, "curseDamage", &s.curseDamage)
        || !JsonI32_(item, "antiMagic", &s.antiMagic)
        || !JsonI32_(item, "bloodlustEffect", &s.bloodlustEffect)
        || !JsonI32_(item, "precisionEffect", &s.precisionEffect)
        || !JsonI32_(item, "weaknessEffect", &s.weaknessEffect)
        || !JsonI32_(item, "stoneSkinEffect", &s.stoneSkinEffect)
        || !JsonI32_(item, "prayerEffect", &s.prayerEffect)
        || !JsonI32_(item, "mirthEffect", &s.mirthEffect)
        || !JsonI32_(item, "sorrowEffect", &s.sorrowEffect)
        || !JsonI32_(item, "fortuneEffect", &s.fortuneEffect)
        || !JsonI32_(item, "misfortuneEffect", &s.misfortuneEffect)
        || !JsonI32_(item, "slayerType", &s.slayerType)
        || !JsonI32_(item, "hexesTraveled", &s.hexesTraveled)
        || !JsonI32_(item, "counterstrikeEffect", &s.counterstrikeEffect))
        return false;
    if (!JsonF32_(item, "frenzyMultiplier", &s.frenzyMultiplier)
        || !JsonF32_(item, "blindEffect", &s.blindEffect)
        || !JsonF32_(item, "fireShieldEffect", &s.fireShieldEffect)
        || !JsonF32_(item, "protectionAirEffect", &s.protectionAirEffect)
        || !JsonF32_(item, "protectionFireEffect", &s.protectionFireEffect)
        || !JsonF32_(item, "protectionWaterEffect", &s.protectionWaterEffect)
        || !JsonF32_(item, "protectionEarthEffect", &s.protectionEarthEffect)
        || !JsonF32_(item, "shieldEffect", &s.shieldEffect)
        || !JsonF32_(item, "airShieldEffect", &s.airShieldEffect))
        return false;
    if (!JsonU8_(item, "blinded", &s.blinded) || !JsonU8_(item, "paralyzed", &s.paralyzed))
        return false;
    if (!JsonI32_(item, "forgetfulnessLevel", &s.forgetfulnessLevel)
        || !JsonF32_(item, "slowEffect", &s.slowEffect)
        || !JsonI32_(item, "hasteEffect", &s.hasteEffect)
        || !JsonI32_(item, "diseaseAttackEffect", &s.diseaseAttackEffect)
        || !JsonI32_(item, "diseaseDefenseEffect", &s.diseaseDefenseEffect)
        || !JsonI32_(item, "faerieDragonSpell", &s.faerieDragonSpell)
        || !JsonI32_(item, "magicMirrorEffect", &s.magicMirrorEffect)
        || !JsonI32_(item, "morale", &s.morale)
        || !JsonI32_(item, "luck", &s.luck))
        return false;
    if (!JsonU8_(item, "isDone", &s.isDone)
        || !JsonU8_(item, "highlightContour", &s.highlightContour)
        || !JsonU8_(item, "attackedAlready", &s.attackedAlready)
        || !JsonU8_(item, "isDead", &s.isDead)
        || !JsonU8_(item, "hasLosses", &s.hasLosses)
        || !JsonU8_(item, "hasLosses2", &s.hasLosses2))
        return false;
    if (!JsonI32_(item, "cloneId", &s.cloneId)
        || !JsonI32_(item, "cloneDuration", &s.cloneDuration)
        || !JsonI32_(item, "spellToApply", &s.spellToApply)
        || !JsonI32_(item, "visibility", &s.visibility)
        || !JsonU32_(item, "infoFlags", &s.infoFlags))
        return false;
    if (!JsonIntsToArray_(item, "infoCombat", s.infoCombat, 8))
        return false;
    if (!JsonI32_(item, "defendingDelta", &s.defendingDelta)
        || !JsonI32_(item, "animationSpeed", &s.animationSpeed)
        || !JsonI32_(item, "movementDirection", &s.movementDirection)
        || !JsonI32_(item, "renderOffsetY", &s.renderOffsetY)
        || !JsonI32_(item, "renderOffsetX", &s.renderOffsetX))
        return false;
    if (!JsonBytesToArray_(item, "stackExtra", s.extraScalars, sizeof(s.extraScalars)))
        return false;
    return JsonI32_(item, "aiTargetSide", &s.aiTarget.side)
        && JsonI32_(item, "aiTargetSlot", &s.aiTarget.slot);
}

// 编码：输出 {"turn":..., "sections":{...8 段...}}；帧层（format/version/
// battleKey/timestampUtcMs）由 BattleArchive 组装。battle 段不重复存 turn
// （顶层已含，与文件名回合互为冗余校验）。
static bool CodecEncodeJson(const CodecCapture& capture, BattleJson* out, std::string* error)
{
    if (!out) return false;
    for (int side = 0; side < 2; ++side) {
        if (!CodecWarMachinesValid_(capture.heroPresent[side], capture.warMachines[side])) {
            if (error) *error = "invalid hero war-machine equipment";
            return false;
        }
    }

    BattleJson battle;
    battle["action"] = capture.action;
    battle["actionParameter"] = capture.actionParameter;
    battle["actionTarget"] = capture.actionTarget;
    battle["actionParameter2"] = capture.actionParameter2;
    battle["landType"] = capture.landType;
    battle["absoluteObstacleId"] = capture.absoluteObstacleId;
    battle["siegeKind"] = capture.siegeKind;
    battle["hasMoat"] = capture.hasMoat;
    battle["specialTerrain"] = capture.specialTerrain;
    battle["heroSpellPower"] = JsonInt32Array_(capture.heroSpellPower, 2);
    battle["heroOwner"] = JsonInt32Array_(capture.heroOwner, 2);
    battle["heroMonCount"] = JsonInt32Array_(capture.heroMonCount, 2);
    battle["turnsSinceLastEnchanterCast"] = JsonInt32Array_(capture.turnsSinceLastEnchanterCast, 2);
    battle["summonedMonster"] = JsonInt32Array_(capture.summonedMonster, 2);
    battle["currentMonSide"] = capture.currentMonSide;
    battle["currentMonIndex"] = capture.currentMonIndex;
    battle["currentActiveSide"] = capture.currentActiveSide;
    battle["autoCombat"] = capture.autoCombat;
    battle["creatureAtMousePos"] = capture.creatureAtMousePos;
    battle["mouseCoord"] = capture.mouseCoord;
    battle["attackerCoord"] = capture.attackerCoord;
    battle["moveType"] = capture.moveType;
    battle["siegeKind2"] = capture.siegeKind2;
    battle["finished"] = capture.finished;
    battle["necromancyRaisedAmount"] = capture.necromancyRaisedAmount;
    battle["necromancyRaisedMonsters"] = capture.necromancyRaisedMonsters;
    battle["tacticsDifference"] = capture.tacticsDifference;
    battle["waitPhase"] = capture.waitPhase;
    battle["antiMagicGarrison"] = capture.antiMagicGarrison;
    battle["creatureBank"] = capture.creatureBank;
    battle["boatCombat"] = capture.boatCombat;
    battle["isNotAI"] = JsonByteArray_(capture.isNotAI, 2);
    battle["isHuman"] = JsonByteArray_(capture.isHuman, 2);
    battle["artifactAutoCast"] = JsonByteArray_(capture.artifactAutoCast, 2);
    {
        BattleJson casted = BattleJson::array();
        for (int side = 0; side < 2; ++side) casted.push_back(capture.heroCasted[side]);
        battle["heroCasted"] = casted;
    }
    battle["tacticsPhase"] = capture.tacticsPhase;
    battle["fortWallsHp"] = JsonInt32Array_(capture.fortWallsHp, 18);
    battle["fortWallsAlive"] = JsonInt32Array_(capture.fortWallsAlive, 18);
    {
        BattleJson mass = BattleJson::array();
        for (int side = 0; side < 2; ++side)
            mass.push_back(JsonByteArray_(capture.massSpellTarget[side], 20));
        battle["massSpellTarget"] = mass;
    }
    battle["accessibleSquares"] = JsonByteArray_(capture.accessibleSquares, 187);
    battle["accessibleSquares2"] = JsonByteArray_(capture.accessibleSquares2, 187);
    battle["rngTlsSeed"] = capture.rngTlsSeed;
    battle["rngMirrorSeed"] = capture.rngMirrorSeed;
    {
        BattleJson eagle = BattleJson::array();
        for (int side = 0; side < 2; ++side)
            eagle.push_back(JsonInt32Array_(capture.eagleEye[side].data(),
                capture.eagleEye[side].size()));
        battle["eagleEye"] = eagle;
    }
    battle["managerExtra"] = JsonByteArray_(capture.extraScalars, sizeof(capture.extraScalars));
    {
        BattleJson towers = BattleJson::array();
        for (const auto& tower : capture.towers) {
            towers.push_back({
                {"type", tower.scalars[0]},
                {"x", tower.scalars[1]},
                {"y", tower.scalars[2]},
                {"facing", tower.scalars[3]},
                {"sequence", tower.scalars[4]},
                {"frameIndex", tower.scalars[5]},
                {"defendingSlot", tower.scalars[6]},
                {"defName", JsonFromRawBytes_(tower.defName, strnlen(tower.defName, sizeof(tower.defName)))},
                {"missileName", JsonFromRawBytes_(tower.missileName, strnlen(tower.missileName, sizeof(tower.missileName)))},
            });
        }
        battle["towers"] = towers;
    }
    {
        BattleJson walls = BattleJson::array();
        for (const auto& name : capture.wallPcxNames)
            walls.push_back(JsonFromRawBytes_(name, strnlen(name, sizeof(name))));
        battle["wallPcxNames"] = walls;
    }

    BattleJson stacks = BattleJson::array();
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 21; ++slot)
            stacks.push_back(JsonStackEntry_(side, slot, capture.stacks[side][slot]));

    BattleJson squares = BattleJson::array();
    for (int hex = 0; hex < 187; ++hex) {
        const CodecSquare& q = capture.squares[hex];
        BattleJson dead = BattleJson::array();
        for (int d = 0; d < q.deadStacksNumber; ++d)
            dead.push_back({{"side", q.deadStackSide[d]}, {"slot", q.deadStackIndex[d]},
                {"attacker", q.belongsToAttacker[d]}});
        squares.push_back({
            {"hex", hex},
            {"squareExtra", JsonByteArray_(q.extraScalars, sizeof(q.extraScalars))},
            {"obstacleBits", q.obstacleBits},
            {"stackSide", q.stackSide},
            {"stackIndex", q.stackIndex},
            {"twoHexMonsterSquare", q.twoHexMonsterSquare},
            {"deadStacksNumber", q.deadStacksNumber},
            {"deadStacks", dead},
            {"availableForLeftSquare", q.availableForLeftSquare},
            {"availableForRightSquare", q.availableForRightSquare},
        });
    }

    BattleJson obstacles = BattleJson::array();
    for (const CodecObstacle& item : capture.obstacles) {
        const size_t nameLength = strnlen(item.defName, sizeof(item.defName));
        if (!nameLength || nameLength >= sizeof(item.defName) || item.cellCount > 8) {
            if (error) *error = "capture exceeds an exact-save limit";
            return false;
        }
        obstacles.push_back({
            {"kindId", item.kindId},
            {"anchorHex", item.anchorHex},
            {"ownerSide", item.ownerSide},
            {"featureTriggered", item.featureTriggered},
            {"featureDamage", item.featureDamage},
            {"featureDuration", item.featureDuration},
            {"animationIndex", item.animationIndex},
            {"cells", JsonByteArray_(item.cells, item.cellCount)},
            {"defName", JsonFromRawBytes_(item.defName, nameLength)},
        });
    }

    BattleJson log;
    {
        std::vector<std::string> lines;
        lines.reserve(capture.logLines.size());
        bool allAcp = true;
        for (const auto& line : capture.logLines) {
            std::string utf8;
            if (JsonAcpToUtf8_(line, &utf8)) lines.push_back(std::move(utf8));
            else { allAcp = false; break; }
        }
        if (!allAcp) {
            lines.clear();
            for (const auto& line : capture.logLines)
                lines.push_back(JsonFromRawBytes_(line.data(), line.size()));
        }
        BattleJson arr = BattleJson::array();
        for (const auto& line : lines) arr.push_back(line);
        log["encoding"] = allAcp ? "acp" : "bytes";
        log["lines"] = arr;
    }

    BattleJson heroes = BattleJson::array();
    for (int side = 0; side < 2; ++side) {
        BattleJson machines = BattleJson::array();
        for (const CodecWarMachine_& machine : capture.warMachines[side])
            machines.push_back({{"id", machine.id}, {"subtype", machine.subtype}});
        heroes.push_back({
            {"side", side},
            {"spellPoints", capture.spellPoints[side]},
            {"heroPresent", capture.heroPresent[side] ? 1 : 0},
            {"warMachines", machines},
        });
    }

    BattleJson relations = BattleJson::array();
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 21; ++slot)
            for (int vector = 0; vector < 4; ++vector)
                // JSON 无 U16 计数容量，全量存（08:0x 裁定：不因容量拒绝存档）。
                for (const CodecIdentity& id : capture.stacks[side][slot].relations[vector])
                    relations.push_back({{"side", side}, {"index", slot}, {"vector", vector},
                        {"targetSide", id.side}, {"targetSlot", id.slot}});

    BattleJson spells = BattleJson::array();
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 21; ++slot)
            // 法术编号原值存取（白名单已全线删除，三层同口径）。
            for (int32_t id : capture.stacks[side][slot].spellIds)
                spells.push_back({{"side", side}, {"index", slot}, {"spellId", id}});

    BattleJson sections;
    sections["battle"] = battle;
    sections["stacks"] = stacks;
    sections["squares"] = squares;
    sections["obstacles"] = obstacles;
    sections["log"] = log;
    sections["heroes"] = heroes;
    sections["relations"] = relations;
    sections["spells"] = spells;
    BattleJson root;
    root["turn"] = capture.turn;
    root["sections"] = sections;
    *out = std::move(root);
    return true;
}

// 解码：schema 级字段校验（存在/类型/字宽/数组尺寸界）取代段 CRC；缺任一
// 段整档拒绝；段内值域业务校验仍由 RestorePolicy_ 执行（防线不变）。
static bool CodecDecodeJson(const BattleJson& root, CodecCapture* out, std::string* error)
{
    if (error) error->clear();
    if (!out) { if (error) *error = "null capture output"; return false; }
    if (!root.is_object() || !root.contains("turn") || !root.contains("sections")
        || !root["sections"].is_object()) {
        if (error) *error = "capture is missing a required section";
        return false;
    }
    CodecReset_(out);
    if (!JsonI32_(root, "turn", &out->turn)) {
        if (error) *error = "battle section is corrupt";
        return false;
    }
    const BattleJson& sections = root["sections"];
    const char* const names[] = {"battle", "stacks", "squares", "obstacles",
        "log", "heroes", "relations", "spells"};
    const bool expectArray[] = {false, true, true, true, false, true, true, true};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (!sections.contains(names[i]) || sections[names[i]].empty()) {
            if (error) *error = "capture is missing a required section";
            return false;
        }
        if (expectArray[i] && !sections[names[i]].is_array()) {
            if (error) *error = "capture is missing a required section";
            return false;
        }
        if (!expectArray[i] && !sections[names[i]].is_object()) {
            if (error) *error = "capture is missing a required section";
            return false;
        }
    }
    const BattleJson& battle = sections["battle"];
    if (!JsonI32_(battle, "action", &out->action)
        || !JsonI32_(battle, "actionParameter", &out->actionParameter)
        || !JsonI32_(battle, "actionTarget", &out->actionTarget)
        || !JsonI32_(battle, "actionParameter2", &out->actionParameter2)
        || !JsonI32_(battle, "landType", &out->landType)
        || !JsonI32_(battle, "absoluteObstacleId", &out->absoluteObstacleId)
        || !JsonI32_(battle, "siegeKind", &out->siegeKind)
        || !JsonI32_(battle, "hasMoat", &out->hasMoat)
        || !JsonI32_(battle, "specialTerrain", &out->specialTerrain)
        || !JsonIntsToArray_(battle, "heroSpellPower", out->heroSpellPower, 2)
        || !JsonIntsToArray_(battle, "heroOwner", out->heroOwner, 2)
        || !JsonIntsToArray_(battle, "heroMonCount", out->heroMonCount, 2)
        || !JsonIntsToArray_(battle, "turnsSinceLastEnchanterCast", out->turnsSinceLastEnchanterCast, 2)
        || !JsonIntsToArray_(battle, "summonedMonster", out->summonedMonster, 2)
        || !JsonI32_(battle, "currentMonSide", &out->currentMonSide)
        || !JsonI32_(battle, "currentMonIndex", &out->currentMonIndex)
        || !JsonI32_(battle, "currentActiveSide", &out->currentActiveSide)
        || !JsonI32_(battle, "autoCombat", &out->autoCombat)
        || !JsonI32_(battle, "creatureAtMousePos", &out->creatureAtMousePos)
        || !JsonI32_(battle, "mouseCoord", &out->mouseCoord)
        || !JsonI32_(battle, "attackerCoord", &out->attackerCoord)
        || !JsonI32_(battle, "moveType", &out->moveType)
        || !JsonI32_(battle, "siegeKind2", &out->siegeKind2)
        || !JsonI32_(battle, "finished", &out->finished)
        || !JsonI32_(battle, "necromancyRaisedAmount", &out->necromancyRaisedAmount)
        || !JsonI32_(battle, "necromancyRaisedMonsters", &out->necromancyRaisedMonsters)
        || !JsonI32_(battle, "tacticsDifference", &out->tacticsDifference)
        || !JsonI32_(battle, "waitPhase", &out->waitPhase)
        || !JsonU8_(battle, "antiMagicGarrison", &out->antiMagicGarrison)
        || !JsonU8_(battle, "creatureBank", &out->creatureBank)
        || !JsonU8_(battle, "boatCombat", &out->boatCombat)
        || !JsonBytesToArray_(battle, "isNotAI", out->isNotAI, 2)
        || !JsonBytesToArray_(battle, "isHuman", out->isHuman, 2)
        || !JsonBytesToArray_(battle, "artifactAutoCast", out->artifactAutoCast, 2)
        || !battle.contains("heroCasted") || !battle["heroCasted"].is_array()
        || battle["heroCasted"].size() != 2
        || !JsonU8_(battle, "tacticsPhase", &out->tacticsPhase)
        || !JsonIntsToArray_(battle, "fortWallsHp", out->fortWallsHp, 18)
        || !JsonIntsToArray_(battle, "fortWallsAlive", out->fortWallsAlive, 18)) {
        if (error) *error = "battle section is corrupt";
        return false;
    }
    if (!battle.contains("massSpellTarget") || !battle["massSpellTarget"].is_array()
        || battle["massSpellTarget"].size() != 2
        || !battle.contains("accessibleSquares") || !battle.contains("accessibleSquares2")) {
        if (error) *error = "battle section is corrupt";
        return false;
    }
    {
        // massSpellTarget 每行直接按数组元素取值。
        for (int side = 0; side < 2; ++side) {
            const BattleJson& row = battle["massSpellTarget"][side];
            if (!row.is_array() || row.size() != 20) {
                if (error) *error = "battle section is corrupt";
                return false;
            }
            for (int i = 0; i < 20; ++i) {
                long long v = 0;
                if (!JsonIntRange_(row[i], 0, kJsonU8Max_, &v)) {
                    if (error) *error = "battle section is corrupt";
                    return false;
                }
                out->massSpellTarget[side][i] = (uint8_t)v;
            }
        }
        if (!JsonBytesToArray_(battle, "accessibleSquares", out->accessibleSquares, 187)
            || !JsonBytesToArray_(battle, "accessibleSquares2", out->accessibleSquares2, 187)) {
            if (error) *error = "battle section is corrupt";
            return false;
        }
    }
    {
        for (int side = 0; side < 2; ++side) {
            long long v = 0;
            if (!JsonIntRange_(battle["heroCasted"][side], 0, kJsonU32Max_, &v)) {
                if (error) *error = "battle section is corrupt";
                return false;
            }
            out->heroCasted[side] = (uint32_t)v;
        }
    }
    if (!JsonU32_(battle, "rngTlsSeed", &out->rngTlsSeed)
        || !JsonU32_(battle, "rngMirrorSeed", &out->rngMirrorSeed)) {
        if (error) *error = "battle section is corrupt";
        return false;
    }
    {
        // 法术表 81 项结构界（capture 侧 ReadSpellSet_ 同限）。
        const BattleJson& eagle = battle["eagleEye"];
        if (!eagle.is_array() || eagle.size() != 2) {
            if (error) *error = "battle section is corrupt";
            return false;
        }
        for (int side = 0; side < 2; ++side) {
            if (!eagle[side].is_array() || eagle[side].size() > 81) {
                if (error) *error = "battle section is corrupt";
                return false;
            }
            out->eagleEye[side].resize(eagle[side].size());
            for (size_t i = 0; i < eagle[side].size(); ++i) {
                long long v = 0;
                if (!JsonIntRange_(eagle[side][i], kJsonI32Min_, kJsonI32Max_, &v)) {
                    if (error) *error = "battle section is corrupt";
                    return false;
                }
                out->eagleEye[side][i] = (int32_t)v;
            }
        }
    }
    if (!JsonBytesToArray_(battle, "managerExtra", out->extraScalars, sizeof(out->extraScalars))) {
        if (error) *error = "battle section is corrupt";
        return false;
    }
    {
        const BattleJson& towers = battle["towers"];
        if (!towers.is_array() || towers.size() != 3) {
            if (error) *error = "battle section is corrupt";
            return false;
        }
        for (int t = 0; t < 3; ++t) {
            const BattleJson& item = towers[t];
            CodecTower_& tower = out->towers[t];
            if (!JsonI32_(item, "type", &tower.scalars[0])
                || !JsonI32_(item, "x", &tower.scalars[1])
                || !JsonI32_(item, "y", &tower.scalars[2])
                || !JsonI32_(item, "facing", &tower.scalars[3])
                || !JsonI32_(item, "sequence", &tower.scalars[4])
                || !JsonI32_(item, "frameIndex", &tower.scalars[5])
                || !JsonI32_(item, "defendingSlot", &tower.scalars[6])) {
                if (error) *error = "battle section is corrupt";
                return false;
            }
            if (!item.contains("defName") || !item["defName"].is_string()
                || !item.contains("missileName") || !item["missileName"].is_string()) {
                if (error) *error = "battle section is corrupt";
                return false;
            }
            size_t written = 0;
            if (!JsonLatin1ToCharArray_(item["defName"].get<std::string>(),
                    tower.defName, sizeof(tower.defName), &written)
                || !JsonLatin1ToCharArray_(item["missileName"].get<std::string>(),
                    tower.missileName, sizeof(tower.missileName), &written)) {
                if (error) *error = "battle section is corrupt";
                return false;
            }
        }
        const BattleJson& walls = battle["wallPcxNames"];
        if (!walls.is_array() || walls.size() != 90) {
            if (error) *error = "battle wall arrays are truncated";
            return false;
        }
        for (int i = 0; i < 90; ++i) {
            if (!walls[i].is_string()) {
                if (error) *error = "battle wall arrays are truncated";
                return false;
            }
            size_t written = 0;
            if (!JsonLatin1ToCharArray_(walls[i].get<std::string>(),
                    out->wallPcxNames[i], sizeof(out->wallPcxNames[i]), &written)) {
                if (error) *error = "battle wall arrays are truncated";
                return false;
            }
        }
    }

    {
        const BattleJson& arr = sections["stacks"];
        if (arr.size() != 42) {
            if (error) *error = "stack section is corrupt";
            return false;
        }
        bool seen[2][21] = {};
        for (const BattleJson& item : arr) {
            int32_t side = -1, index = -1;
            if (!JsonI32_(item, "side", &side) || !JsonI32_(item, "index", &index)
                || side < 0 || side > 1 || index < 0 || index > 20 || seen[side][index]) {
                if (error) *error = "stack section is corrupt";
                return false;
            }
            seen[side][index] = true;
            if (!JsonStackFromEntry_(item, out)) {
                if (error) *error = "stack section is corrupt";
                return false;
            }
        }
    }

    {
        const BattleJson& arr = sections["squares"];
        if (arr.size() != 187) {
            if (error) *error = "square section header mismatch";
            return false;
        }
        for (int hex = 0; hex < 187; ++hex) {
            const BattleJson& item = arr[hex];
            CodecSquare& q = out->squares[hex];
            int32_t hexKey = -1, deadNumber = 0;
            if (!JsonI32_(item, "hex", &hexKey) || hexKey != hex
                || !JsonBytesToArray_(item, "squareExtra", q.extraScalars, sizeof(q.extraScalars))
                || !JsonU8_(item, "obstacleBits", &q.obstacleBits)
                || !JsonI8_(item, "stackSide", &q.stackSide)
                || !JsonI8_(item, "stackIndex", &q.stackIndex)
                || !JsonU8_(item, "twoHexMonsterSquare", &q.twoHexMonsterSquare)
                || !JsonI32_(item, "deadStacksNumber", &deadNumber)) {
                if (error) *error = "square section is corrupt";
                return false;
            }
            if (deadNumber < 0 || deadNumber > 14
                || !item.contains("deadStacks") || !item["deadStacks"].is_array()
                || item["deadStacks"].size() != (size_t)deadNumber) {
                if (error) *error = "square section is corrupt";
                return false;
            }
            q.deadStacksNumber = deadNumber;
            for (int d = 0; d < deadNumber; ++d) {
                const BattleJson& deadItem = item["deadStacks"][d];
                long long side = 0, slot = 0, attacker = 0;
                if (!JsonReadInt_(deadItem, "side", kJsonI8Min_, kJsonI8Max_, &side)
                    || !JsonReadInt_(deadItem, "slot", kJsonI8Min_, kJsonI8Max_, &slot)
                    || !JsonReadInt_(deadItem, "attacker", 0, kJsonU8Max_, &attacker)) {
                    if (error) *error = "square section is corrupt";
                    return false;
                }
                q.deadStackSide[d] = (int8_t)side;
                q.deadStackIndex[d] = (int8_t)slot;
                q.belongsToAttacker[d] = (uint8_t)attacker;
            }
            if (!JsonU8_(item, "availableForLeftSquare", &q.availableForLeftSquare)
                || !JsonU8_(item, "availableForRightSquare", &q.availableForRightSquare)) {
                if (error) *error = "square section is corrupt";
                return false;
            }
        }
    }

    {
        const BattleJson& arr = sections["obstacles"];
        if (arr.size() > 4096) {
            if (error) *error = "obstacle count exceeds 4096";
            return false;
        }
        out->obstacles.resize(arr.size());
        for (size_t i = 0; i < arr.size(); ++i) {
            const BattleJson& item = arr[i];
            CodecObstacle& o = out->obstacles[i];
            if (!JsonU16_(item, "kindId", &o.kindId)
                || !JsonU8_(item, "anchorHex", &o.anchorHex)
                || !JsonI8_(item, "ownerSide", &o.ownerSide)
                || !JsonU8_(item, "featureTriggered", &o.featureTriggered)
                || !JsonU32_(item, "featureDamage", &o.featureDamage)
                || !JsonU32_(item, "featureDuration", &o.featureDuration)
                || !JsonU32_(item, "animationIndex", &o.animationIndex)) {
                if (error) *error = "obstacle section is corrupt";
                return false;
            }
            if (!item.contains("cells") || !item["cells"].is_array()
                || item["cells"].size() > 8) {
                if (error) *error = "obstacle section is corrupt";
                return false;
            }
            memset(o.cells, 0, sizeof(o.cells));
            o.cellCount = (uint8_t)item["cells"].size();
            for (size_t c = 0; c < item["cells"].size(); ++c) {
                long long v = 0;
                if (!JsonIntRange_(item["cells"][c], 0, kJsonU8Max_, &v)) {
                    if (error) *error = "obstacle section is corrupt";
                    return false;
                }
                o.cells[c] = (uint8_t)v;
            }
            if (!item.contains("defName") || !item["defName"].is_string()) {
                if (error) *error = "obstacle section is corrupt";
                return false;
            }
            size_t written = 0;
            memset(o.defName, 0, sizeof(o.defName));
            if (!JsonLatin1ToCharArray_(item["defName"].get<std::string>(),
                    o.defName, sizeof(o.defName) - 1, &written)
                || written == 0) {
                if (error) *error = "obstacle section is corrupt";
                return false;
            }
        }
    }

    {
        const BattleJson& logSec = sections["log"];
        if (!logSec.contains("encoding") || !logSec["encoding"].is_string()
            || !logSec.contains("lines") || !logSec["lines"].is_array()) {
            if (error) *error = "log section version mismatch";
            return false;
        }
        const std::string encoding = logSec["encoding"].get<std::string>();
        const bool isAcp = encoding == "acp";
        const bool isBytes = encoding == "bytes";
        if (!isAcp && !isBytes) {
            if (error) *error = "log section version mismatch";
            return false;
        }
        out->logLines.resize(logSec["lines"].size());
        for (size_t i = 0; i < logSec["lines"].size(); ++i) {
            const BattleJson& lineNode = logSec["lines"][i];
            if (!lineNode.is_string()) {
                if (error) *error = "log section is corrupt";
                return false;
            }
            const std::string text = lineNode.get<std::string>();
            if (isAcp) JsonUtf8ToAcp_(text, &out->logLines[i]);
            else if (!JsonLatin1ToBytes_(text, &out->logLines[i])) {
                if (error) *error = "log section is corrupt";
                return false;
            }
        }
    }

    {
        const BattleJson& arr = sections["heroes"];
        if (arr.size() != 2) {
            if (error) *error = "hero section is corrupt";
            return false;
        }
        for (int side = 0; side < 2; ++side) {
            const BattleJson& item = arr[side];
            int32_t sideKey = -1, present = 0;
            if (!JsonI32_(item, "side", &sideKey) || sideKey != side
                || !JsonI16_(item, "spellPoints", &out->spellPoints[side])
                || !JsonI32_(item, "heroPresent", &present) || present < 0 || present > 1
                || !item.contains("warMachines") || !item["warMachines"].is_array()
                || item["warMachines"].size() != 4) {
                if (error) *error = "hero section is corrupt";
                return false;
            }
            out->heroPresent[side] = present == 1;
            for (int m = 0; m < 4; ++m) {
                const BattleJson& machine = item["warMachines"][m];
                if (!JsonI32_(machine, "id", &out->warMachines[side][m].id)
                    || !JsonI32_(machine, "subtype", &out->warMachines[side][m].subtype)) {
                    if (error) *error = "hero section is corrupt";
                    return false;
                }
            }
            // 写回白名单与二进制同口径：机器装备值会直接 memcpy 进英雄身体槽 13..16。
            if (!CodecWarMachinesValid_(out->heroPresent[side], out->warMachines[side])) {
                if (error) *error = "hero section is corrupt";
                return false;
            }
        }
    }

    {
        const BattleJson& arr = sections["relations"];
        for (const BattleJson& item : arr) {
            long long side = 0, index = 0, vector = 0, targetSide = 0, targetSlot = 0;
            if (!JsonReadInt_(item, "side", 0, 1, &side)
                || !JsonReadInt_(item, "index", 0, 20, &index)
                || !JsonReadInt_(item, "vector", 0, 3, &vector)
                || !JsonReadInt_(item, "targetSide", 0, 1, &targetSide)
                || !JsonReadInt_(item, "targetSlot", 0, 20, &targetSlot)) {
                if (error) *error = "relation section is corrupt";
                return false;
            }
            CodecIdentity id = {(int32_t)targetSide, (int32_t)targetSlot};
            out->stacks[side][index].relations[vector].push_back(id);
        }
    }

    {
        const BattleJson& arr = sections["spells"];
        for (const BattleJson& item : arr) {
            long long side = 0, index = 0, spellId = 0;
            if (!JsonReadInt_(item, "side", 0, 1, &side)
                || !JsonReadInt_(item, "index", 0, 20, &index)
                || !JsonReadInt_(item, "spellId", kJsonI32Min_, kJsonI32Max_, &spellId)) {
                if (error) *error = "spell section is corrupt";
                return false;
            }
            out->stacks[side][index].spellIds.push_back((int32_t)spellId);
        }
    }
    return true;
}
