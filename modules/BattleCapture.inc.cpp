// ========== BattleCapture.inc.cpp ==========
// 从当前战斗只读采集时刻状态。
// 原则（2026-10-11 08:0x 用户裁定）：存档时必然处于正常游戏过程，一切该进入存档的
// 数据必然合法——采集端不再做任何值域/关联类业务校验；内存可读性探测仅作为防崩
// 铠甲保留，探测失败一律降级（跳过该项/截断采集），绝不因此拒绝存档。

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
    const uintptr_t mapBase = *(const uintptr_t*)(object + 0x24);
    const uint32_t mapSize = *(const uint32_t*)(object + 0x28);
    if (!mapSize || mapSize > 100000 || !Readable_((const void*)mapBase, mapSize * 4)
        || (uintptr_t)map < mapBase || (uintptr_t)endMap < (uintptr_t)map
        || ((uintptr_t)map - mapBase) % 4 || ((uintptr_t)endMap - mapBase) % 4
        || (uintptr_t)endMap - mapBase >= mapSize * 4
        || !Readable_(current, 4) || !Readable_(map, sizeof(void*))
        || *(const uintptr_t*)(object + 0x04) != (uintptr_t)*map
        || (uintptr_t)blockEnd != (uintptr_t)*map + 4096
        || (uintptr_t)current < (uintptr_t)*map || (uintptr_t)current >= (uintptr_t)blockEnd
        || ((uintptr_t)current - (uintptr_t)*map) % 4) return false;
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
        if (current == blockEnd) {
            ++map;
            if (map > endMap || !Readable_(map, sizeof(void*)) || !Readable_(*map, 4)) return false;
            current = *map;
            blockEnd = current + 0x1000;
        }
        if (read + 1 == size && (current != endCurrent || map != endMap)) return false;
    }
    return true;
}

// 原则（2026-10-07 用户裁定）：游戏正常产生的状态不做语义校验。关系向量按可读性
// 采集任意条数；无法映射到管理器槽位的悬挂条目跳过并计数——原生瞬态，丢弃优于
// 拒绝（重放悬挂指针是 0x43E720 一类陈旧链接崩溃源）。仍返回 false 的只剩内存
// 不可读一类硬故障（无法为回滚建立快照）。
static bool ReadPointerRelations_(const H3CombatManager* mgr, const H3Vector<H3CombatCreature*>& vector,
    std::vector<CodecIdentity>* out, uint32_t* dropped)
{
    out->clear();
    const UINT count = vector.Count();
    if (!count) return true;
    if (count > 100000 || !Readable_(vector.CFirst(), count * sizeof(H3CombatCreature*))) return false;
    out->reserve(count);
    for (UINT i = 0; i < count; ++i) {
        CodecIdentity identity = { -1, -1 };
        if (!StackIdentity_(mgr, vector[i], &identity.side, &identity.slot)) {
            ++*dropped;
            continue;
        }
        out->push_back(identity);
    }
    return true;
}

// Native VC6 set<int>: header owns a head node, shared nil lives at 0x694FA0.
static bool ReadSpellSet_(const uint8_t* object, std::vector<int32_t>* out)
{
    out->clear();
    if (!Readable_(object, 16)) return false;
    const uint32_t count = *(const uint32_t*)(object + 12);
    const uint32_t* head = *(const uint32_t* const*)(object + 4);
    if (count > 81 || !Readable_(head, 20)) return false;
    if (!count) return true;
    const uint32_t* nil = *(const uint32_t* const*)0x694FA0;
    const uint32_t* node = (const uint32_t*)head[1];
    std::vector<const uint32_t*> path, visited;
    while (node != nil || !path.empty()) {
        while (node != nil) {
            if (node == head || !Readable_(node, 20) || visited.size() >= count
                || std::find(visited.begin(), visited.end(), node) != visited.end()) return false;
            visited.push_back(node);
            path.push_back(node);
            node = (const uint32_t*)node[0];
        }
        node = path.back(); path.pop_back();
        const int32_t spell = (int32_t)node[3];
        if (spell < 0 || spell >= 81 || (!out->empty() && spell <= out->back())) return false;
        out->push_back(spell);
        node = (const uint32_t*)node[2];
    }
    return out->size() == count;
}

// v4 obstacle kind identity: the terrain table is a 91-entry array of
// H3ObstacleInfo at 0x63C7C8 (stride 0x14); the five spell obstacles are
// standalone statics placed beside it and are NOT on the table grid, which is
// exactly why the old pointer-difference infoIndex truncated for them.
static const uintptr_t kObstacleTableAddress_ = 0x63C7C8;
static const size_t kObstacleTableEntries_ = 91;
static const uintptr_t kObstacleSpellInfos_[] = {
    0x63CEE8, // 100 quicksand  C17SPE1.DEF
    0x63CF00, // 101 land mine  C09spF1.def
    0x63CF18, // 102 force field 2 cells C15spE1.def
    0x63CF2C, // 103 force field 3 cells C15spE10.def
    0x63CF68, // 104 fire wall  C07spF61.def
};

#ifdef H3BATTLE_OBSTACLE_TEST_BACKEND
static const H3ObstacleInfo* const* g_obstacleInfoMapTest_ = nullptr;
#endif

static bool ObstacleKindOf_(const H3ObstacleInfo* info, uint16_t* kindId)
{
    if (!info) return false;
#ifdef H3BATTLE_OBSTACLE_TEST_BACKEND
    if (g_obstacleInfoMapTest_) {
        for (uint16_t i = 0; i <= 104; ++i)
            if (g_obstacleInfoMapTest_[i] == info) { *kindId = i; return true; }
        return false;
    }
#endif
    const uintptr_t address = (uintptr_t)info;
    const uintptr_t offset = address - kObstacleTableAddress_;
    if (offset < kObstacleTableEntries_ * 0x14 && offset % 0x14 == 0) {
        *kindId = (uint16_t)(offset / 0x14);
        return true;
    }
    for (size_t i = 0; i < sizeof(kObstacleSpellInfos_) / sizeof(kObstacleSpellInfos_[0]); ++i)
        if (kObstacleSpellInfos_[i] == address) {
            *kindId = (uint16_t)(100 + i);
            return true;
        }
    return false;
}

// Address of the static info for a known kind id; null when out of range.
static const H3ObstacleInfo* ObstacleInfoFor_(uint16_t kindId)
{
#ifdef H3BATTLE_OBSTACLE_TEST_BACKEND
    if (g_obstacleInfoMapTest_) return kindId <= 104 ? g_obstacleInfoMapTest_[kindId] : nullptr;
#endif
    if (kindId < kObstacleTableEntries_)
        return (const H3ObstacleInfo*)(kObstacleTableAddress_ + (size_t)kindId * 0x14);
    if (kindId >= 100 && kindId < 100 + sizeof(kObstacleSpellInfos_) / sizeof(kObstacleSpellInfos_[0]))
        return (const H3ObstacleInfo*)kObstacleSpellInfos_[kindId - 100];
    return nullptr;
}

// Exact replication of the row-parity adjustment in FUN_00466590/FUN_00466710:
// when the anchor row is odd and the target row is even, the delta lands one hex left.
static int ObstacleCellHex_(int anchorHex, int delta)
{
    int target = delta + anchorHex;
    if (((anchorHex / 17) & 1) != 0 && ((target / 17) & 1) == 0) target -= 1;
    return target;
}

static bool ObstacleName_(const char* source, char* out)
{
    if (!source || !out) return false;
    memset(out, 0, 16);
    for (size_t i = 0; i < 16; ++i) {
        if (!Readable_(source + i, 1)) return false;
        const char ch = source[i];
        if (!ch) return i != 0;
        if (i == 15) return false;
        out[i] = ch;
    }
    return false;
}

// Validate raw vector pointers before Count() can subtract malformed pointers.
static bool ObstacleVectorReady_(const H3Vector<H3Obstacle>& vector, bool writable, UINT* count)
{
    if (!Readable_(&vector, sizeof(vector))) return false;
    const uintptr_t* header = reinterpret_cast<const uintptr_t*>(&vector);
    const uintptr_t first = header[1], end = header[2], capacity = header[3];
    if ((!first && (end || capacity)) || end < first || capacity < end
        || (end - first) % sizeof(H3Obstacle) || (capacity - first) % sizeof(H3Obstacle)) return false;
    const uintptr_t bytes = end - first;
    if (bytes / sizeof(H3Obstacle) > 4096) return false;
    if (bytes && !Readable_(reinterpret_cast<const void*>(first), bytes)) return false;
    if (writable && (IsBadWritePtr(const_cast<H3Vector<H3Obstacle>*>(&vector), sizeof(vector))
        || (capacity != first && IsBadWritePtr(reinterpret_cast<void*>(first), capacity - first)))) return false;
    *count = static_cast<UINT>(bytes / sizeof(H3Obstacle));
    return true;
}

static bool CaptureResourceName_(const void* resource, char (&name)[13])
{
    memset(name, 0, sizeof(name));
    if (!resource) return true;
    if (!Readable_(resource, 0x1C)) return false;
    const char* raw = (const char*)resource + 4;
    const size_t length = strnlen(raw, 12);
    if (!length || *(const int32_t*)((const uint8_t*)resource + 0x18) <= 0) return false;
    memcpy(name, raw, length);
    return true;
}

static bool CaptureBattle_(const H3CombatManager* mgr, CodecCapture* out, std::string* error)
{
    if (!Readable_(mgr, sizeof(H3CombatManager)) || !out) {
        if (error) *error = "combat manager is not readable";
        return false;
    }
    // 快照含 STL 容器，不能 memset 其对象头；重采集时也必须释放旧内容。
    CodecReset_(out);
    out->version = kCodecVersion;
    out->action = mgr->action;
    out->actionParameter = mgr->actionParameter;
    out->actionTarget = mgr->actionTarget;
    out->actionParameter2 = mgr->actionParameter2;
    out->landType = mgr->landType;
    out->absoluteObstacleId = mgr->absoluteObstacleId;
    out->siegeKind = mgr->siegeKind;
    out->hasMoat = *((const uint8_t*)mgr + 0x53A8);
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
    out->waitPhase = *((const uint8_t*)mgr + 0x13DE4);
    memcpy(out->heroSpellPower, mgr->heroSpellPower, sizeof(out->heroSpellPower));
    memcpy(out->isNotAI, mgr->isNotAI, sizeof(out->isNotAI));
    memcpy(out->isHuman, mgr->isHuman, sizeof(out->isHuman));
    memcpy(out->heroOwner, mgr->heroOwner, sizeof(out->heroOwner));
    memcpy(out->artifactAutoCast, mgr->artifactAutoCast, sizeof(out->artifactAutoCast));
    memcpy(out->heroCasted, mgr->heroCasted, sizeof(out->heroCasted));
    memcpy(out->heroMonCount, mgr->heroMonCount, sizeof(out->heroMonCount));
    memcpy(out->turnsSinceLastEnchanterCast, mgr->turnsSinceLastEnchanterCast, sizeof(out->turnsSinceLastEnchanterCast));
    memcpy(out->summonedMonster, mgr->summonedMonster, sizeof(out->summonedMonster));
    // RNG：当前种子在游戏线程 TLS+0x14（rand 只推进它），0x67FBE4 是播种镜像，二者分别采集。
    // Get() 即原版 getter 0x61D8C3；本函数只在游戏线程钩子内调用，战斗中单例必然已存在。
    out->rngTlsSeed = (uint32_t)H3Random::ThreadLocalSingleton::Get().CurrentSeed();
    out->rngMirrorSeed = *(const uint32_t*)0x67FBE4;
    memcpy(out->fortWallsHp, mgr->fortWallsHp, sizeof(out->fortWallsHp));
    memcpy(out->fortWallsAlive, mgr->fortWallsAlive, sizeof(out->fortWallsAlive));
    memcpy(out->massSpellTarget, mgr->massSpellTarget, sizeof(out->massSpellTarget));
    memcpy(out->accessibleSquares, mgr->accessibleSquares, sizeof(out->accessibleSquares));
    memcpy(out->accessibleSquares2, mgr->accessibleSquares2, sizeof(out->accessibleSquares2));

    size_t managerPacked = 0;
    for (const auto& range : kManagerExtraRanges_) {
        memcpy(out->extraScalars + managerPacked, (const uint8_t*)mgr + range.offset, range.size);
        managerPacked += range.size;
    }
    DiagStage_("capture.siege");
    for (int tower = 0; tower < 3; ++tower) {
        const uint8_t* raw = (const uint8_t*)mgr + 0x13D78 + tower * 0x24;
        CodecTower_& saved = out->towers[tower];
        memcpy(&saved.scalars[0], raw, 4);
        memcpy(&saved.scalars[1], raw + 0x0C, 24);
        // 08:0x 裁定：资源名读不到只降级为空名（CaptureResourceName_ 失败时已清零），
        // 不拒绝存档。
        CaptureResourceName_(*(const void* const*)(raw + 4), saved.defName);
        CaptureResourceName_(*(const void* const*)(raw + 8), saved.missileName);
    }
    for (int wall = 0; wall < 90; ++wall)
        CaptureResourceName_(((const void* const*)((const uint8_t*)mgr + 0x13DF8))[wall], out->wallPcxNames[wall]);

    static_assert(offsetof(H3CombatCreature, info) + offsetof(H3CreatureInformation, numberShots) == 0xD8,
        "native remaining ammunition offset");
    static_assert(offsetof(H3Hero, bodyArtifacts) == 0x12D && sizeof(H3Artifact) == 8,
        "native hero equipment layout");
    static_assert(kMachineBodySlot_ == 13 && kMachineSlotsBytes_ == 4 * sizeof(H3Artifact),
        "native war-machine slots 13..16");
    DiagStage_("capture.heroes");
    for (int side = 0; side < 2; ++side) {
        const H3Hero* hero = mgr->hero[side];
        out->heroPresent[side] = hero != nullptr;
        if (hero) {
            const H3Artifact* machines = hero->bodyArtifacts + kMachineBodySlot_;
            // 08:0x 裁定：英雄结构读不到按"该侧无英雄"降级，不拒绝存档。
            if (Readable_(hero, 0x1A) && Readable_(machines, kMachineSlotsBytes_)) {
                out->spellPoints[side] = hero->spellPoints;
                CodecCaptureWarMachines_(machines, out->warMachines[side]);
            } else {
                out->heroPresent[side] = false;
            }
        }
        // 鹰眼集合读不到降级为空集，不拒绝存档。
        if (!ReadSpellSet_((const uint8_t*)mgr + 0x545C + side * 0x10, &out->eagleEye[side]))
            out->eagleEye[side].clear();
    }

    DiagStage_("capture.squares");
    for (int squareIndex = 0; squareIndex < 187; ++squareIndex) {
        const H3CombatSquare& source = mgr->squares[squareIndex];
        CodecSquare& square = out->squares[squareIndex];
        size_t packed = 0;
        for (const auto& range : kSquareExtraRanges_) {
            memcpy(square.extraScalars + packed, (const uint8_t*)&source + range.offset, range.size);
            packed += range.size;
        }
        square.obstacleBits = source.obstacleBits;
        square.stackSide = source.stackSide;
        square.stackIndex = source.stackIndex;
        square.twoHexMonsterSquare = source.twoHexMonsterSquare;
        // 08:0x 裁定：不做业务校验；deadStackSide/Index[14] 是定长附表（结构容量
        // 事实），计数只钳到附表容量，超出部分附表本就不存在。
        square.deadStacksNumber = source.deadStacksNumber >= 0 && source.deadStacksNumber <= 14
            ? source.deadStacksNumber : 0;
        memcpy(square.deadStackSide, source.deadStackSide, sizeof(square.deadStackSide));
        memcpy(square.deadStackIndex, source.deadStackIndex, sizeof(square.deadStackIndex));
        memcpy(square.belongsToAttacker, source.belongsToAttacker, sizeof(square.belongsToAttacker));
        square.availableForLeftSquare = source.availableForLeftSquare;
        square.availableForRightSquare = source.availableForRightSquare;
    }

    DiagStage_("capture.obstacles");
    UINT obstacleCount = 0;
    // 08:0x 裁定：容器读不到按无障碍降级；单条读不到/类型不识别跳过该条，均不拒绝。
    if (ObstacleVectorReady_(mgr->obstacleInfo, false, &obstacleCount)) {
        out->obstacles.reserve(obstacleCount);
        for (UINT i = 0; i < obstacleCount; ++i) {
            const H3Obstacle& source = mgr->obstacleInfo[i];
            // v4: destroyed entries stay in the vector as zombies (def == 0, count never
            // shrinks). Only live entries are saved, keyed by kind + anchor and sorted so
            // the encoded section is independent of the vector's append order.
            if (!source.def) continue;
            DiagCursor_(-1, (int)i);
            CodecObstacle item;
            memset(&item, 0, sizeof(item));
            if (!Readable_(source.info, sizeof(H3ObstacleInfo)) || !ObstacleKindOf_(source.info, &item.kindId))
                continue;
            const H3ObstacleInfo* info = source.info;
            // cells[8] 定长附表（结构容量事实）：计数只钳容量。
            const int blocked = info->blockedCount >= 0 && info->blockedCount <= 8 ? info->blockedCount : 0;
            if (!ObstacleName_(info->defName, item.defName))
                continue;
            item.anchorHex = source.anchorHex;
            item.ownerSide = source.ownerSide;
            item.featureTriggered = source.featureTriggered;
            item.featureDamage = source.featureDamage;
            item.featureDuration = source.featureDuration;
            item.animationIndex = source.animationIndex;
            item.cellCount = (uint8_t)blocked;
            for (int c = 0; c < blocked; ++c)
                item.cells[c] = (uint8_t)ObstacleCellHex_(item.anchorHex, info->relativeCells[c]);
            out->obstacles.push_back(item);
        }
    }
    std::sort(out->obstacles.begin(), out->obstacles.end(), CodecObstacleKeyLess_);

    DiagStage_("capture.combat-log");
    if (mgr->dlg && Readable_(mgr->dlg, sizeof(H3CombatDlg))) {
        // 08:0x 裁定：日志有多少存多少，全量采集。内存探测只防崩：数组整体读不到
        // → 存 0 条；中途坏行 → 截断到此为止；单行超 0xFFFF → 同样截断（编码 U16
        // 长度上限）。任何情况都不拒绝存档。
        const H3Vector<H3String*>& log = *(const H3Vector<H3String*>*)((const uint8_t*)mgr->dlg + 0x54);
        const UINT logCount = log.Count();
        const H3String* const* lines = log.CFirst();
        if (logCount && logCount <= 10000000 && lines
            && Readable_(lines, logCount * sizeof(H3String*))) {
            out->logLines.reserve(logCount);
            for (UINT i = 0; i < logCount; ++i) {
                const H3String* line = lines[i];
                if (!line || !Readable_(line, sizeof(H3String))) break;
                const UINT len = line->Length();
                if (len > 0xFFFF) break;
                if (len && !Readable_(line->String(), len)) break;
                out->logLines.push_back(len ? std::string(line->String(), len) : std::string());
            }
        }
    }

    DiagStage_("capture.stacks");
    uint32_t unresolvable = 0; // 悬挂在管理器之外的 AI 目标/关系指针（丢弃计数）
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            DiagCursor_(side, slot);
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
            // +0xEB is padding, not a death flag; manager's vanish mask is captured separately.
            stack.isDead = 0;
            stack.hasLosses = raw[0xE9];
            stack.hasLosses2 = raw[0xEA];
            stack.cloneId = source.cloneId;
            stack.cloneDuration = source.cloneDuration;
            stack.spellToApply = source.spellToApply;
            stack.visibility = source.visibility;
            // v2：info 只取 flags（+0x10），不整块复制含 LPCSTR 的 0x74 字节。
            stack.infoFlags = source.info.flags;
            memcpy(stack.infoCombat, (const uint8_t*)&source.info + 0x4C, sizeof(stack.infoCombat));
            stack.defendingDelta = *(const int32_t*)(raw + 0x4DC);
            stack.animationSpeed = *(const int32_t*)(raw + 0x158);
            stack.movementDirection = *(const int32_t*)(raw + 0x48);
            stack.renderOffsetY = *(const int32_t*)(raw + 0x100);
            stack.renderOffsetX = *(const int32_t*)(raw + 0x104);
            size_t extraOffset = 0;
            for (const CodecScalarRange_& range : kStackExtraRanges_) {
                memcpy(stack.extraScalars + extraOffset, raw + range.offset, range.size);
                extraOffset += range.size;
            }
            const H3CombatCreature* aiTarget = *(H3CombatCreature* const*)(raw + 0x538);
            stack.aiTarget = {-1, -1};
            // 悬挂 AI 目标（指向管理器之外）同样按丢弃处理，不再拒绝采集。
            if (aiTarget && !StackIdentity_(mgr, aiTarget, &stack.aiTarget.side, &stack.aiTarget.slot)) {
                stack.aiTarget = {-1, -1};
                ++unresolvable;
            }
            const uint8_t* rawRelations = (const uint8_t*)&source;
            const H3Vector<H3CombatCreature*>* relations[] = {
                (const H3Vector<H3CombatCreature*>*)(rawRelations + 0x4F4),
                (const H3Vector<H3CombatCreature*>*)(rawRelations + 0x504),
                (const H3Vector<H3CombatCreature*>*)(rawRelations + 0x514),
                (const H3Vector<H3CombatCreature*>*)(rawRelations + 0x524)
            };
            // 08:0x 裁定：关系向量/法术队列读不到只丢弃该项（计 unresolvable 留证），
            // 不拒绝存档。
            for (int relation = 0; relation < 4; ++relation) {
                if (!ReadPointerRelations_(mgr, *relations[relation], &stack.relations[relation], &unresolvable)) {
                    LogDebug("[Capture op=%ld] slot=%d:%d relation=%d unreadable, dropped", g_diag.id, side, slot, relation);
                    ++unresolvable;
                }
            }
            if (!ReadDequeInts_((const uint8_t*)&source + 0x420, &stack.spellIds)) {
                LogDebug("[Capture op=%ld] slot=%d:%d spell deque unreadable, dropped", g_diag.id, side, slot);
                ++unresolvable;
            }
        }
    }
    // 换阵重打的新战斗可能残留指向空置槽/管理器之外的瞬态链接（详见
    // CodecNormalizeStaleLinks_ 注释）；采集完成后统一丢弃，保证存档与恢复永不
    // 重放悬挂指针。丢弃量进 debug 日志留证。
    const CodecLinkDropReport_ droppedLinks = CodecNormalizeStaleLinks_(*out);
    if (droppedLinks.aiTargets || droppedLinks.relationEntries || unresolvable)
        LogDebug("[Capture op=%ld] stale links dropped ai_targets=%u relation_entries=%u unresolvable=%u",
            g_diag.id, droppedLinks.aiTargets, droppedLinks.relationEntries, unresolvable);
    DiagCursor_(-1, -1);
    return true;
}
