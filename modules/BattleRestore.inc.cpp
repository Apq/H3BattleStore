// ========== BattleRestore.inc.cpp ==========
// 同场战斗恢复。只有完整校验通过后才写战斗内存；任一步失败都停止并记录。


using RefreshCreatureFn = void(__thiscall*)(H3CombatManager*);
// 0x493FC0 实为六显式参（H3API Refresh(a,b,c) 展开为 this,a,0,0,c,b,0；本机 0x494792 为 ret 0x18）。
using RefreshFieldFn = void(__thiscall*)(H3CombatManager*, int, int, int, int, int, int);

// 0x61841F 为 cdecl：本机 0x61842B 普通 ret，参数由调用方清栈。
using RngSetFn = void(__cdecl*)(unsigned);


static RefreshCreatureFn const kRefreshCreature = (RefreshCreatureFn)0x495770;
static RefreshFieldFn const kRefreshField = (RefreshFieldFn)0x493FC0;

static RngSetFn kRngSet = (RngSetFn)0x61841F;

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
            if (stack.type < 0 || stack.type > 0x95 || !CodecStackPositionValid_(stack)) {
                if (error) *error = "stack identity";
                return false;
            }
            for (int vector = 0; vector < 4; ++vector) {
                for (size_t i = 0; i < stack.relations[vector].size(); ++i) {
                    const CodecIdentity& id = stack.relations[vector][i];
                    if (id.side < 0 || id.side > 1 || id.slot < 0 || id.slot >= 20
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
    // +0xEB is padding; do not overwrite it.
    target->cloneId = source.cloneId;
    target->cloneDuration = source.cloneDuration;
    target->spellToApply = source.spellToApply;
    target->visibility = source.visibility;
    target->info.flags = source.infoFlags;
    memcpy((uint8_t*)&target->info + 0x4C, source.infoCombat, sizeof(source.infoCombat));
    *(int32_t*)(raw + 0x4DC) = source.defendingDelta;
    *(int32_t*)(raw + 0x158) = source.animationSpeed;
    *(int32_t*)(raw + 0x48) = source.movementDirection;
    *(int32_t*)(raw + 0x100) = source.renderOffsetY;
    *(int32_t*)(raw + 0x104) = source.renderOffsetX;
    size_t extraOffset = 0;
    for (const CodecScalarRange_& range : kStackExtraRanges_) {
        memcpy(raw + range.offset, source.extraScalars + extraOffset, range.size);
        extraOffset += range.size;
    }
}

// H3API omits auto-retreat at +0x1402F, shifting its two tail fields left.
static void RestoreDisplayCaches_(H3CombatManager* mgr, const CodecCapture& capture)
{
    CodecRestoreDisplayCaches_(mgr->accessibleSquares, mgr->accessibleSquares2,
        (uint8_t*)mgr + 0x14031, capture);
}

// 从打包的 manager extraScalars 中取一段白名单字节写回 mgr（offset 必须完整
// 落在某个已登记 range 内）。用于渲染链结束后重建"渲染会推进的显示态"。
static void RestoreManagerExtraBytes_(H3CombatManager* mgr, const CodecCapture& capture,
    uint32_t offset, uint32_t size)
{
    size_t packed = 0;
    for (const auto& range : kManagerExtraRanges_) {
        if (offset >= range.offset && offset + size <= range.offset + range.size) {
            memcpy((uint8_t*)mgr + offset, capture.extraScalars + packed + (offset - range.offset), size);
            return;
        }
        packed += range.size;
    }
}

static bool g_restoreBusy = false;
static bool g_restoreFatal = false;static bool g_battleInitialized = false;
static unsigned g_battleGeneration = 0;
static DWORD g_battleThread = 0;
static int g_messageDepth = 0;
static int g_executorDepth = 0;
static bool g_battleListDirty = true;

static bool BattleMainDialog_(const H3CombatManager* mgr)
{
    H3WindowManager* wnd = H3WindowManager::Get();
    return CombatIsReadable_(mgr) && mgr->dlg && wnd && wnd->lastDlg == mgr->dlg;
}

static bool RestoreWindow_(const H3CombatManager* mgr)
{
    return g_battleInitialized && !g_restoreFatal && GetCurrentThreadId() == g_battleThread
        && g_messageDepth == 1 && !g_executorDepth && BattleMainDialog_(mgr) && !mgr->finished
        && !mgr->autoCombat && !mgr->tacticsPhase && (int)mgr->action == 0
        && !*(const int*)0x698A3C
        && mgr->currentMonSide >= 0 && mgr->currentMonSide < 2
        && mgr->currentMonIndex >= 0 && mgr->currentMonIndex < 20
        && mgr->currentActiveSide >= 0 && mgr->currentActiveSide < 2
        && mgr->isHuman[mgr->currentActiveSide]
        && mgr->activeStack == &mgr->stacks[mgr->currentMonSide][mgr->currentMonIndex]
        && mgr->activeStack->numberAlive > 0;
}

// ---------- v4 obstacle rebuild ----------
// The obstacle vector owns DEF references and keeps destroyed entries as zombies
// (def == 0; the count never shrinks and existing indices never move). Rebuilding
// is a diff against the saved payload:
//   * live entries missing from the save die through the game's own FUN_00466710
//     (clears the squares, Dereferences the DEF, leaves a zombie);
//   * saved entries missing live are rebuilt the way the game builds them: the DEF
//     comes from a surviving same-kind entry (reference count ++ at +0x18, the
//     H3ResourceItem::IncreaseReferences semantic) or from H3LoadedDef::Load
//     0x55C9C0 (cached lookup, ref++ inside), the entry is appended through the
//     vector insert FUN_0046AA60, and the squares are marked by FUN_00466590;
//   * entries present on both sides only get their five scalars overwritten.
// These are transaction-owned game primitives, not a hooked original path. Each
// is called exactly once inside its small SEH boundary. A fault inside
// a primitive marks the restore fatal: the write path may be half applied, so the
// session stops instead of continuing with unknown state (see H3Note 10.16).

typedef void (__thiscall* ObstacleRemoveFn_)(H3CombatManager*, unsigned int);
typedef int (__thiscall* ObstacleVectorInsertFn_)(void*, H3Obstacle*, unsigned int, const H3Obstacle*);
typedef void (__thiscall* ObstaclePlaceFn_)(H3CombatManager*, H3Obstacle*, int, int, unsigned int);
typedef H3LoadedDef* (__thiscall* ObstacleDefLoadFn_)(const char*);

#ifdef H3BATTLE_OBSTACLE_TEST_BACKEND
static ObstacleRemoveFn_ kObstacleRemove = (ObstacleRemoveFn_)0x466710;
static ObstacleVectorInsertFn_ kObstacleInsert = (ObstacleVectorInsertFn_)0x46AA60;
static ObstaclePlaceFn_ kObstaclePlace = (ObstaclePlaceFn_)0x466590;
static ObstacleDefLoadFn_ kObstacleDefLoad = (ObstacleDefLoadFn_)0x55C9C0;
#else
static ObstacleRemoveFn_ const kObstacleRemove = (ObstacleRemoveFn_)0x466710;
static ObstacleVectorInsertFn_ const kObstacleInsert = (ObstacleVectorInsertFn_)0x46AA60;
static ObstaclePlaceFn_ const kObstaclePlace = (ObstaclePlaceFn_)0x466590;
static ObstacleDefLoadFn_ const kObstacleDefLoad = (ObstacleDefLoadFn_)0x55C9C0;
#endif

static const int kObstacleGuard_ = GuardRegisterHook_("BattleStore.ObstacleTransaction");

// One primitive per function, POD locals only: SEH and C++ unwinding cannot mix.
static bool ObstacleRemoveSeh_(H3CombatManager* mgr, unsigned int index)
{
    __try {
        kObstacleRemove(mgr, index);
        H3Obstacle& entry = mgr->obstacleInfo.begin()[index];
        if (entry.def) return false;
        // Native expiry does not check DEF before removing a duration-zero entry.
        entry.featureDuration = 0;
        entry.animationIndex = 0xFFFFFFFFu;
    }
    __except (GuardCrashFilter_(kObstacleGuard_, GetExceptionInformation())) { return false; }
    return true;
}

static bool ObstacleCleanZombiesSeh_(H3CombatManager* mgr)
{
    __try {
        for (UINT i = 0; i < mgr->obstacleInfo.Count(); ++i) {
            H3Obstacle& entry = mgr->obstacleInfo.begin()[i];
            if (!entry.def) {
                entry.featureDuration = 0;
                entry.animationIndex = 0xFFFFFFFFu;
            }
        }
    }
    __except (GuardCrashFilter_(kObstacleGuard_, GetExceptionInformation())) { return false; }
    return true;
}

static bool ObstacleRefSeh_(H3LoadedDef* def)
{
    __try {
        INT32* refs = (INT32*)((uint8_t*)def + 0x18);
        if (!def || IsBadWritePtr(refs, sizeof(*refs)) || *refs <= 0 || *refs == 0x7FFFFFFF) return false;
        ++*refs;
    }
    __except (GuardCrashFilter_(kObstacleGuard_, GetExceptionInformation())) { return false; }
    return true;
}

static bool ObstacleInsertSeh_(H3CombatManager* mgr, const H3Obstacle* item, int* newIndex)
{
    H3Vector<H3Obstacle>* vector = &mgr->obstacleInfo;
    UINT before = 0, count = 0;
    __try {
        if (!ObstacleVectorReady_(*vector, true, &before)) return false;
        kObstacleInsert(vector, vector->end(), 1, item);
        if (!ObstacleVectorReady_(*vector, true, &count) || count != before + 1) return false;
        *newIndex = (int)(count - 1);
    }
    __except (GuardCrashFilter_(kObstacleGuard_, GetExceptionInformation())) { return false; }
    return true;
}

static bool ObstaclePlaceSeh_(H3CombatManager* mgr, H3Obstacle* entry,
    int index, int anchorHex, unsigned int bits)
{
    __try { kObstaclePlace(mgr, entry, index, anchorHex, bits); }
    __except (GuardCrashFilter_(kObstacleGuard_, GetExceptionInformation())) { return false; }
    return true;
}

static H3LoadedDef* ObstacleLoadSeh_(const char* defName, bool* faulted)
{
    H3LoadedDef* def = nullptr;
    *faulted = false;
    __try { def = kObstacleDefLoad(defName); }
    __except (GuardCrashFilter_(kObstacleGuard_, GetExceptionInformation())) { *faulted = true; return nullptr; }
    return def;
}

// H3ResourceItem::Dereference is the vtable +4 slot: removes the item from the
// resource manager and drops the reference the load/borrow created.
static bool ObstacleDerefSeh_(H3LoadedDef* def)
{
    __try {
        if (!def || !Readable_(def, 2 * sizeof(void*))) return false;
        void* const* vtable = *(void* const**)def;
        if (!Readable_(vtable, 2 * sizeof(void*))) return false;
        typedef void (__thiscall* DefEraseFn_)(H3LoadedDef*);
        DefEraseFn_ erase = (DefEraseFn_)vtable[1];
        erase(def);
    }
    __except (GuardCrashFilter_(kObstacleGuard_, GetExceptionInformation())) { return false; }
    return true;
}

static bool RestoreObstacleDefSane_(const H3LoadedDef* def)
{
    return def && Readable_(def, sizeof(H3LoadedDef))
        && def->groupsCount >= 1 && def->groupsCount <= 1024
        && Readable_(def->groups, def->groupsCount * sizeof(*def->groups))
        && Readable_(def->groups[0], sizeof(H3LoadedDef::DefGroup))
        && def->groups[0]->count >= 1 && def->groups[0]->count <= 100000
        && Readable_(def->groups[0]->frames, def->groups[0]->count * sizeof(H3DefFrame*))
        && Readable_(def->groups[0]->frames[0], sizeof(H3DefFrame))
        && !IsBadWritePtr(reinterpret_cast<uint8_t*>(const_cast<H3LoadedDef*>(def)) + 0x18, sizeof(INT32))
        && *(const INT32*)(reinterpret_cast<const uint8_t*>(def) + 0x18) > 0
        && *(const INT32*)(reinterpret_cast<const uint8_t*>(def) + 0x18) < 0x7FFFFFFF;
}

struct RestoreObstacleKey_
{
    uint16_t kindId;
    uint8_t anchorHex;
    int index;
};

static const RestoreObstacleKey_* RestoreObstacleFind_(
    const std::vector<RestoreObstacleKey_>& keys, uint16_t kindId, uint8_t anchorHex)
{
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i].kindId == kindId && keys[i].anchorHex == anchorHex) return &keys[i];
    return nullptr;
}

static const CodecObstacle* RestoreObstacleSaved_(
    const CodecCapture& capture, uint16_t kindId, uint8_t anchorHex)
{
    for (size_t i = 0; i < capture.obstacles.size(); ++i)
        if (capture.obstacles[i].kindId == kindId && capture.obstacles[i].anchorHex == anchorHex)
            return &capture.obstacles[i];
    return nullptr;
}

// All pointer work happens before any write: every saved kind must resolve to the
// live static info, the def name and cell layout must match the capture exactly
// (this is what catches game-version table drift), and the vector must be readable
// and writable with room for the rebuild.
static bool RestoreObstaclePreflight_(H3CombatManager* mgr, const CodecCapture& capture, std::string* error)
{
    auto reject = [&](const char* why) { if (error) *error = why; return false; };
    if (capture.obstacles.size() > 4096) return reject("saved obstacle count exceeds 4096");
    const H3Vector<H3Obstacle>& vector = mgr->obstacleInfo;
    UINT count = 0;
    if (!ObstacleVectorReady_(vector, true, &count))
        return reject("obstacle vector is not accessible");
    size_t additions = 0, rollbackAdds = 0;
    for (UINT i = 0; i < count; ++i) {
        const H3Obstacle& entry = vector.CFirst()[i];
        if (!entry.def) continue;
        uint16_t kind = 0;
        if (!ObstacleKindOf_(entry.info, &kind) || !RestoreObstacleDefSane_(entry.def))
            return reject("live obstacle resource is unavailable");
        void* const* vtable = *(void* const**)entry.def;
        if (!Readable_(vtable, 2 * sizeof(void*)) || !Readable_(vtable[1], 1))
            return reject("live obstacle release slot is unavailable");
        if (!RestoreObstacleSaved_(capture, kind, entry.anchorHex)) ++rollbackAdds;
    }
    for (size_t i = 0; i < capture.obstacles.size(); ++i) {
        const CodecObstacle& item = capture.obstacles[i];
        if (item.anchorHex >= 187 || item.cellCount > 8 || item.ownerSide < -1 || item.ownerSide > 1
            || !item.defName[0] || !memchr(item.defName, 0, sizeof(item.defName)))
            return reject("saved obstacle payload is invalid");
        const H3ObstacleInfo* info = ObstacleInfoFor_(item.kindId);
        if (!info || !Readable_(info, sizeof(*info)))
            return reject("saved obstacle kind is not present in this game build");
        char name[16];
        if (!ObstacleName_(info->defName, name) || strcmp(name, item.defName) != 0)
            return reject("saved obstacle def name does not match the live table");
        bool found = false;
        for (UINT k = 0; k < count; ++k) {
            const H3Obstacle& entry = vector.CFirst()[k];
            uint16_t kind = 0;
            if (entry.def && ObstacleKindOf_(entry.info, &kind)
                && kind == item.kindId && entry.anchorHex == item.anchorHex) found = true;
        }
        if (!found) ++additions;
        if (info->blockedCount < 0 || info->blockedCount > 8
            || (uint8_t)info->blockedCount != item.cellCount)
            return reject("saved obstacle cell count does not match the live table");
        for (int c = 0; c < item.cellCount; ++c)
            if (ObstacleCellHex_(item.anchorHex, info->relativeCells[c]) != (int)item.cells[c])
                return reject("saved obstacle cell layout does not match the live table");
    }
    if (count + additions + rollbackAdds > 4096)
        return reject("obstacle vector would exceed the supported size");
    return true;
}

static bool RestoreObstacleFault_(const char* why, std::string* error)
{
    g_restoreFatal = true;
    LogError("[Restore op=%ld] obstacle fault stage=%s: %s", g_diag.id,
        g_diag.stage ? g_diag.stage : "none", why);
    if (error) *error = why;
    return false;
}

// Pin every distinct live DEF plus missing saved kinds for both directions,
// including the last live reference deleted by forward remove. Apply never loads.
struct RestoreObstacleResources_
{
    struct Resource { uint16_t kind; H3LoadedDef* def; };
    std::vector<Resource> owned;
    std::vector<RestoreObstacleKey_> liveScratch;
    RestoreObstacleResources_() = default;
    RestoreObstacleResources_(const RestoreObstacleResources_&) = delete;
    RestoreObstacleResources_& operator=(const RestoreObstacleResources_&) = delete;
    ~RestoreObstacleResources_() { if (!g_restoreFatal && !owned.empty()) Release(nullptr); }
    H3LoadedDef* Find(uint16_t kind) const {
        for (size_t i = 0; i < owned.size(); ++i)
            if (owned[i].kind == kind) return owned[i].def;
        return nullptr;
    }
    bool Release(std::string* error) {
        if (g_restoreFatal) return false;
        DiagStage_("restore.obstacles-release");
        while (!owned.empty()) {
            H3LoadedDef* def = owned.back().def;
            owned.pop_back(); // Never retry a faulting dereference.
            if (!ObstacleDerefSeh_(def)) return RestoreObstacleFault_("resource pool release faulted", error);
        }
        return true;
    }
    bool Prepare(H3CombatManager* mgr, const CodecCapture& before,
        const CodecCapture& capture, std::string* error) {
        if (g_restoreFatal) return false;
        DiagStage_("restore.obstacles-pin");
        // Allocate all STL storage before acquiring any resource ownership.
        owned.reserve(mgr->obstacleInfo.Count() + before.obstacles.size() + capture.obstacles.size());
        liveScratch.reserve(4096);
        for (UINT k = 0; k < mgr->obstacleInfo.Count(); ++k) {
            const H3Obstacle& entry = mgr->obstacleInfo.CFirst()[k];
            if (!entry.def) continue;
            bool pinned = false;
            for (size_t i = 0; i < owned.size(); ++i) if (owned[i].def == entry.def) pinned = true;
            if (pinned) continue;
            uint16_t kind = 0;
            if (!ObstacleKindOf_(entry.info, &kind) || !RestoreObstacleDefSane_(entry.def)) {
                if (error) *error = "resource donor failed validation";
                return false;
            }
            if (!ObstacleRefSeh_(entry.def)) return RestoreObstacleFault_("resource pool pin faulted", error);
            Resource resource = { kind, entry.def };
            owned.push_back(resource);
        }
        const CodecCapture* directions[] = { &before, &capture };
        for (size_t direction = 0; direction < 2; ++direction) {
            for (size_t i = 0; i < directions[direction]->obstacles.size(); ++i) {
                const CodecObstacle& item = directions[direction]->obstacles[i];
                if (Find(item.kindId)) continue;
                H3LoadedDef* def = nullptr;
                for (UINT k = 0; k < mgr->obstacleInfo.Count(); ++k) {
                    const H3Obstacle& entry = mgr->obstacleInfo.CFirst()[k];
                    uint16_t kind = 0;
                    if (entry.def && ObstacleKindOf_(entry.info, &kind) && kind == item.kindId) {
                        def = entry.def; break;
                    }
                }
                if (def) {
                    if (!RestoreObstacleDefSane_(def)) {
                        if (error) *error = "resource donor failed validation";
                        return false;
                    }
                    if (!ObstacleRefSeh_(def)) return RestoreObstacleFault_("resource pool pin faulted", error);
                }
                else {
                    bool faulted = false;
                    DiagStage_("restore.obstacles-preload");
                    def = ObstacleLoadSeh_(item.defName, &faulted);
                    if (faulted) return RestoreObstacleFault_("resource pool load faulted", error);
                    if (!def) { if (error) *error = "obstacle def could not be loaded"; return false; }
                }
                Resource resource = { item.kindId, def };
                owned.push_back(resource); // reserve above makes ownership recording non-allocating.
                void* const* vtable = Readable_(def, sizeof(void*)) ? *(void* const**)def : nullptr;
                if (!RestoreObstacleDefSane_(def) || !Readable_(vtable, 2 * sizeof(void*))
                    || !Readable_(vtable[1], 1)) {
                    if (error) *error = "obstacle def failed validation";
                    return false;
                }
            }
        }
        LogDebug("[Restore op=%ld] obstacle resources pinned=%u for forward/rollback", g_diag.id, (unsigned)owned.size());
        return true;
    }
};

// Executes only after both directions have owning resources; faults are fail-stop.
static bool RestoreObstacles_(H3CombatManager* mgr, const CodecCapture& capture,
    RestoreObstacleResources_& resources, std::string* error)
{
    auto reject = [&](const char* why) { if (error) *error = why; return false; };
    auto fault = [&](const char* why) { return RestoreObstacleFault_(why, error); };
    if (g_restoreFatal) return false;
    H3Vector<H3Obstacle>& vector = mgr->obstacleInfo;
    std::vector<RestoreObstacleKey_>& live = resources.liveScratch;
    const UINT count = vector.Count();
    if (count > live.capacity()) return fault("obstacle scratch capacity was not prepared");
    live.clear();
    for (UINT i = 0; i < count; ++i) {
        const H3Obstacle& entry = vector.CFirst()[i];
        if (!entry.def) continue; // zombie: def cleared by FUN_00466710, never rebuilt
        uint16_t kindId = 0;
        if (!ObstacleKindOf_(entry.info, &kindId))
            return reject("live obstacle kind degraded since capture");
        RestoreObstacleKey_ key = { kindId, entry.anchorHex, (int)i };
        live.push_back(key);
    }
    for (size_t i = 0; i < capture.obstacles.size(); ++i)
        if (!resources.Find(capture.obstacles[i].kindId))
            return fault("prepared obstacle resource is missing");
    DiagStage_("restore.obstacles-clean-zombies");
    if (!ObstacleCleanZombiesSeh_(mgr)) return fault("obstacle zombie cleanup faulted");
    // Structural phase. Removes first (indices never shift), then appends through
    // the game's insert, then the five scalars on surviving entries.
    DiagStage_("restore.obstacles-remove");
    for (size_t i = 0; i < live.size(); ++i) {
        if (RestoreObstacleSaved_(capture, live[i].kindId, live[i].anchorHex)) continue;
        if (!ObstacleRemoveSeh_(mgr, (unsigned)live[i].index))
            return fault("obstacle removal faulted");
    }
    DiagStage_("restore.obstacles-insert");
    for (size_t i = 0; i < capture.obstacles.size(); ++i) {
        const CodecObstacle& saved = capture.obstacles[i];
        if (RestoreObstacleFind_(live, saved.kindId, saved.anchorHex)) continue;
        H3LoadedDef* def = resources.Find(saved.kindId);
        if (!ObstacleRefSeh_(def)) return fault("obstacle entry reference faulted");
        H3Obstacle item;
        memset(&item, 0, sizeof(item));
        item.def = def;
        item.info = const_cast<H3ObstacleInfo*>(ObstacleInfoFor_(saved.kindId));
        item.anchorHex = saved.anchorHex;
        item.ownerSide = saved.ownerSide;
        item.featureTriggered = saved.featureTriggered;
        item.featureDamage = saved.featureDamage;
        item.featureDuration = saved.featureDuration;
        item.animationIndex = saved.animationIndex;
        int newIndex = -1;
        if (!ObstacleInsertSeh_(mgr, &item, &newIndex))
            return fault("obstacle vector insert faulted");
        if (!ObstaclePlaceSeh_(mgr, vector.begin() + newIndex, newIndex,
            saved.anchorHex, ObstacleKindBits_(saved.kindId)))
            return fault("obstacle square placement faulted");
    }
    DiagStage_("restore.obstacles-update");
    for (size_t i = 0; i < live.size(); ++i) {
        const CodecObstacle* saved = RestoreObstacleSaved_(capture, live[i].kindId, live[i].anchorHex);
        if (!saved) continue;
        H3Obstacle& entry = vector.begin()[live[i].index];
        entry.ownerSide = saved->ownerSide;
        entry.featureTriggered = saved->featureTriggered;
        entry.featureDamage = saved->featureDamage;
        entry.featureDuration = saved->featureDuration;
        entry.animationIndex = saved->animationIndex;
    }
    return true;
}

// v4: obstacles are rebuilt through the game's own create/destroy paths before any
// scalar write; a false return means clean reject (nothing written) or fatal
// (g_restoreFatal set, partial write).
static bool RestoreCreatureDefReady_(H3LoadedDef* def, const CodecStack& stack);
#include "BattleObjects.inc.cpp"
#include "BattleSiege.inc.cpp"

static bool RestoreApply_(H3CombatManager* mgr, const CodecCapture& capture,
    RestoreObstacleResources_& resources, RestoreObjects_* objects, std::string* error, RestoreSiege_* siege = nullptr)
{
    if (!RestoreObstacles_(mgr, capture, resources, error)) return false;
    if (siege && !siege->Switch(mgr)) {
        if (error) *error = "交换攻城资源发生异常或状态漂移，已停止战斗";
        return false;
    }
    if (objects) objects->Switch(mgr);
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 21; ++slot) {
            const CodecStack& saved = capture.stacks[side][slot];
            if (slot == 20) {
                // Reserved containers have detached ownership; resource/type state
                // is not reconstructed as a combatant. Restore its marker explicitly.
                mgr->stacks[side][20].type = saved.occupied ? saved.type : -1;
            }
            if (!saved.occupied) continue;
            RestoreStackScalars_(&mgr->stacks[side][slot], saved);
            *(H3CombatCreature**)((uint8_t*)&mgr->stacks[side][slot] + 0x538) = saved.aiTarget.side < 0
                ? nullptr : &mgr->stacks[saved.aiTarget.side][saved.aiTarget.slot];
        }
    // Grid bits are independent saved state; obstacleIndex is recomputed from the
    // rebuilt vector
    // layout because it is a product of entry order, not a saved value. Zombies
    // (def == 0) claim no square.
    int squareObstacleIndex[187];
    for (int i = 0; i < 187; ++i) squareObstacleIndex[i] = -1;
    {
        const H3Vector<H3Obstacle>& vector = mgr->obstacleInfo;
        const UINT count = vector.Count();
        for (UINT i = 0; i < count; ++i) {
            const H3Obstacle& entry = vector.CFirst()[i];
            if (!entry.def) continue;
            uint16_t kind = 0;
            const CodecObstacle* saved = ObstacleKindOf_(entry.info, &kind)
                ? RestoreObstacleSaved_(capture, kind, entry.anchorHex) : nullptr;
            if (!saved) {
                g_restoreFatal = true;
                if (error) *error = "rebuilt obstacle set lost saved identity";
                return false;
            }
            squareObstacleIndex[saved->anchorHex] = (int)i;
            for (int c = 0; c < saved->cellCount; ++c)
                squareObstacleIndex[saved->cells[c]] = (int)i;
        }
    }
    for (int i = 0; i < 187; ++i) {
        H3CombatSquare& target = mgr->squares[i];
        const CodecSquare& source = capture.squares[i];
        size_t packed = 0;
        for (const auto& range : kSquareExtraRanges_) {
            memcpy((uint8_t*)&target + range.offset, source.extraScalars + packed, range.size);
            packed += range.size;
        }
        *((uint8_t*)&target + 0x4C) = 0;
        target.obstacleBits = source.obstacleBits;
        target.obstacleIndex = squareObstacleIndex[i];
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
    size_t managerPacked = 0;
    // Clear every old display cache before writing recorded manager ranges.
    RestoreDisplayCaches_(mgr, capture);
    for (const auto& range : kManagerExtraRanges_) {
        memcpy((uint8_t*)mgr + range.offset, capture.extraScalars + managerPacked, range.size);
        managerPacked += range.size;
    }
    for (int tower = 0; tower < 3; ++tower) {
        uint8_t* raw = (uint8_t*)mgr + 0x13D78 + tower * 0x24;
        memcpy(raw, &capture.towers[tower].scalars[0], 4);
        memcpy(raw + 0x0C, &capture.towers[tower].scalars[1], 24);
    }
    mgr->landType = capture.landType;
    mgr->absoluteObstacleId = capture.absoluteObstacleId;
    mgr->siegeKind = capture.siegeKind;
    *((uint8_t*)mgr + 0x53A8) = (uint8_t)capture.hasMoat;
    mgr->specialTerrain = capture.specialTerrain;
    mgr->antiMagicGarrison = capture.antiMagicGarrison;
    mgr->creatureBank = capture.creatureBank;
    mgr->boatCombat = capture.boatCombat;
    mgr->siegeKind2 = capture.siegeKind2;
    mgr->finished = capture.finished;
    mgr->autoCombat = capture.autoCombat;
    mgr->tacticsPhase = capture.tacticsPhase;
    mgr->tacticsDifference = capture.tacticsDifference;
    memcpy(mgr->isNotAI, capture.isNotAI, sizeof(capture.isNotAI));
    memcpy(mgr->isHuman, capture.isHuman, sizeof(capture.isHuman));
    memcpy(mgr->heroOwner, capture.heroOwner, sizeof(capture.heroOwner));
    memcpy(mgr->heroMonCount, capture.heroMonCount, sizeof(capture.heroMonCount));
    mgr->action = (eCombatAction)capture.action;
    mgr->actionParameter = capture.actionParameter;
    mgr->actionTarget = capture.actionTarget;
    mgr->actionParameter2 = capture.actionParameter2;
    mgr->currentMonSide = capture.currentMonSide;
    mgr->currentMonIndex = capture.currentMonIndex;
    mgr->currentActiveSide = capture.currentActiveSide;
    mgr->activeStack = &mgr->stacks[capture.currentMonSide][capture.currentMonIndex];
    mgr->turn = capture.turn;
    *((uint8_t*)mgr + 0x13DE4) = (uint8_t)capture.waitPhase;
    mgr->blueHighlight = 0;
    mgr->creatureAtMousePos = -1;
    mgr->mouseCoord = -1;
    mgr->attackerCoord = -1;
    mgr->moveType = -99;
    *(int32_t*)((uint8_t*)mgr + 0x132E0) = 0; // Native no-action hover mode.
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 20; ++slot)
            mgr->stacks[side][slot].highlightContour = 0;
    mgr->necromancyRaisedAmount = capture.necromancyRaisedAmount;
    mgr->necromancyRaisedMonsters = (eCreature)capture.necromancyRaisedMonsters;
    memcpy(mgr->artifactAutoCast, capture.artifactAutoCast, sizeof(capture.artifactAutoCast));
    memcpy(mgr->heroCasted, capture.heroCasted, sizeof(capture.heroCasted));
    memcpy(mgr->heroSpellPower, capture.heroSpellPower, sizeof(capture.heroSpellPower));
    memcpy(mgr->turnsSinceLastEnchanterCast, capture.turnsSinceLastEnchanterCast, sizeof(capture.turnsSinceLastEnchanterCast));
    memcpy(mgr->summonedMonster, capture.summonedMonster, sizeof(capture.summonedMonster));
    memcpy(mgr->fortWallsHp, capture.fortWallsHp, sizeof(capture.fortWallsHp));
    memcpy(mgr->fortWallsAlive, capture.fortWallsAlive, sizeof(capture.fortWallsAlive));
    memcpy(mgr->massSpellTarget, capture.massSpellTarget, sizeof(capture.massSpellTarget));
    RestoreDisplayCaches_(mgr, capture);
    for (int side = 0; side < 2; ++side)
        if (mgr->hero[side]) mgr->hero[side]->spellPoints = capture.spellPoints[side];
    return true;
}

static int RestoreMarkCreatureFrames_(H3CombatManager* mgr, const CodecCapture& capture)
{
    int marked = 0;
    // 0x495770 trusts every set bit: empty slots have NULL DEF, and stride is 21.
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 20; ++slot) {
            const CodecStack& stack = capture.stacks[side][slot];
            const bool drawable = stack.occupied && stack.type >= 0 && stack.type < 149
                && stack.position >= 0 && stack.position < 187;
            mgr->redrawCreatureFrame[side][slot] = drawable ? 1 : 0;
            if (drawable) ++marked;
        }
    return marked;
}

static bool RestoreCreatureDefReady_(H3LoadedDef* def, const CodecStack& stack)
{
    if (!Readable_(def, sizeof(*def)) || def->groupsCount < 1 || def->groupsCount > 1024
        || stack.animation < 0 || stack.animation >= def->groupsCount
        || !Readable_(def->groups, def->groupsCount * sizeof(*def->groups))
        || !Readable_(def->activeGroups, def->groupsCount * sizeof(*def->activeGroups))
        || !def->activeGroups[0] || !def->activeGroups[stack.animation]
        || !Readable_(def->groups[0], sizeof(H3LoadedDef::DefGroup))
        || def->groups[0]->count < 1)
        return false;
    const H3LoadedDef::DefGroup* group = def->groups[stack.animation];
    return Readable_(group, sizeof(*group)) && group->count > 0 && group->count <= 100000
        && stack.animationFrame >= 0 && stack.animationFrame < group->count
        && Readable_(group->frames, group->count * sizeof(*group->frames))
        && Readable_(group->frames[stack.animationFrame], sizeof(H3DefFrame));
}

// Rebuild bottom input gates only; full refresh also enters turn/auto-cast logic.
static void RestoreBottomControls_(H3CombatManager* mgr)
{
    if (!mgr || !mgr->dlg) return;
    const bool waitEnabled = CodecWaitControlEnabled_(*((const uint8_t*)mgr + 0x13DE4), mgr->tacticsPhase != 0);
    const bool defendEnabled = CodecDefendControlEnabled_(mgr->tacticsPhase != 0);
    const int side = mgr->currentActiveSide;
    H3Hero* hero = side >= 0 && side < 2 ? mgr->hero[side] : nullptr;
    const uint32_t casted = hero ? (uint32_t)mgr->heroCasted[side] : 0;
    const bool castOverride = *((const uint8_t*)mgr + 0x13D74) != 0;
    // WearsArtifact(0) is the native read-only spellbook check, not a cast.
    const bool hasSpellbook = hero && hero->WearsArtifact(0) != 0;
    const bool spellEnabled = CodecSpellControlEnabled_(mgr->tacticsPhase != 0,
        hero != nullptr, casted, castOverride, hasSpellbook);
    mgr->dlg->SendCommandToItem(spellEnabled ? 6 : 5, 0x7D8, 0x1000);
    mgr->dlg->SendCommandToItem(waitEnabled ? 6 : 5, 0x7D9, 0x1000);
    mgr->dlg->SendCommandToItem(defendEnabled ? 6 : 5, 0x7DA, 0x1000);
    H3DlgItem* spell = mgr->dlg->GetH3DlgItem(0x7D8);
    H3DlgItem* wait = mgr->dlg->GetH3DlgItem(0x7D9);
    H3DlgItem* defend = mgr->dlg->GetH3DlgItem(0x7DA);
    LogDebug("[SpellControl op=%ld] side=%d hero=%p casted=%u override=%d spellbook=%d expected=%d enabled=%d shaded=%d",
        g_diag.id, side, hero, casted, castOverride ? 1 : 0, hasSpellbook ? 1 : 0,
        spellEnabled ? 1 : 0, spell ? (spell->IsEnabled() ? 1 : 0) : -1,
        spell ? (spell->IsSet(h3::NH3DlgControls::NState::SHADED) ? 1 : 0) : -1);
    LogDebug("[Controls op=%ld] restored wait=%d defend=%d control=%d wait_enabled=%d wait_shaded=%d defend_enabled=%d defend_shaded=%d",
        g_diag.id, waitEnabled ? 1 : 0, defendEnabled ? 1 : 0,
        *((const int32_t*)((const uint8_t*)mgr + 0x132B4)),
        wait ? (wait->IsEnabled() ? 1 : 0) : -1,
        wait ? (wait->IsSet(h3::NH3DlgControls::NState::SHADED) ? 1 : 0) : -1,
        defend ? (defend->IsEnabled() ? 1 : 0) : -1,
        defend ? (defend->IsSet(h3::NH3DlgControls::NState::SHADED) ? 1 : 0) : -1);
}

static void RestoreRenderSeed_(H3CombatManager* mgr, const CodecCapture& capture)
{
    const int marked = RestoreMarkCreatureFrames_(mgr, capture);
    for (int tower = 0; tower < 3; ++tower)
        *((uint8_t*)mgr + 0x1402C + tower) = capture.towers[tower].defName[0] ? 1 : 0;
    LogDebug("[Render op=%ld] creature_frames=%d empty_or_reserved_skipped=%d", g_diag.id, marked, 42 - marked);
    DiagStage_("restore.render-creatures");
    kRefreshCreature(mgr);
    DiagStage_("restore.render-field");
    kRefreshField(mgr, 1, 0, 0, 0, 1, 0);
    // Background initialization copies the secondary accessibility cache. Keep
    // both recorded arrays after drawing, before strict recapture verification.
    RestoreDisplayCaches_(mgr, capture);
    DiagStage_("restore.render-log");
    // Refresh the two-line log display without append/wrapping side effects.
    const UINT count = mgr->dlg->GetCombatText().Count();
    *(UINT*)((uint8_t*)mgr->dlg + 0x64) = count < 2 ? count : 2;
    if (count) THISCALL_2(void, 0x472770, mgr->dlg, count > 2 ? count - 2 : 0);
    else {
        mgr->dlg->ShowHint("", false);
        *(UINT*)((uint8_t*)mgr->dlg + 0x68) = 0;
    }
    // Reapply recorded caches at the end of rendering; strict recapture checks
    // the actual bytes. This does not attribute cache writes to the log routine.
    RestoreDisplayCaches_(mgr, capture);
    RestoreManagerExtraBytes_(mgr, capture, 0x1402F, 2);
    RestoreBottomControls_(mgr);
    DiagStage_("restore.rng");
    kRngSet(capture.rngTlsSeed);
    *(uint32_t*)0x67FBE4 = capture.rngMirrorSeed;
}

static bool RestoreRngSeh_(uint32_t tls, uint32_t mirror)
{
    __try {
        kRngSet(tls);
        *(uint32_t*)0x67FBE4 = mirror;
    }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) {
        g_restoreFatal = true;
        return false;
    }
    return true;
}

// Construct before transaction ownership: cleanup finishes before this final write.
struct RestoreRngGuard_
{
    uint32_t tls, mirror;
    bool active = true;
    explicit RestoreRngGuard_(const CodecCapture& before)
        : tls(before.rngTlsSeed), mirror(before.rngMirrorSeed) {}
    bool Finish(const CodecCapture& state) {
        if (g_restoreFatal) return false;
        active = false;
        return RestoreRngSeh_(state.rngTlsSeed, state.rngMirrorSeed);
    }
    ~RestoreRngGuard_() { if (active && !g_restoreFatal) RestoreRngSeh_(tls, mirror); }
};

static bool RestoreRenderSeh_(H3CombatManager* mgr, const CodecCapture& capture)
{
    __try { RestoreRenderSeed_(mgr, capture); }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) {
        g_restoreFatal = true;
        return false;
    }
    return true;
}

struct RestoreLogBuffer
{
    H3Vector<H3String*> lines;
    ~RestoreLogBuffer() { for (H3String* line : lines) delete line; }
    bool Prepare(const std::vector<std::string>& source) {
        for (const std::string& text : source) {
            void* memory = H3Malloc(sizeof(H3String));
            if (!memory) return false;
            H3String* line = ::new(memory) H3String(text.data(), (UINT)text.size());
            if (!line || line->Length() != text.size()) { delete line; return false; }
            if (!lines.Add(line)) { delete line; return false; }
        }
        return true;
    }
};

// A primitive fault means the process must stop without touching game-owned
// allocators again. Keep the prepared buffer abandoned in that path.
static bool RestoreLogPrepareSeh_(RestoreLogBuffer* buffer, const std::vector<std::string>& lines)
{
    __try { return buffer->Prepare(lines); }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { g_restoreFatal = true; }
    return false;
}
struct RestoreLogBufferDeleter_
{
    void operator()(RestoreLogBuffer* buffer) const
    {
        if (g_restoreFatal) return;
        __try { delete buffer; }
        __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { g_restoreFatal = true; }
    }
};

static bool RestoreSameBattle_(H3CombatManager* mgr, const CodecCapture& capture,
    const std::string& expectedKey, std::string* error)
{
    DiagStage_("restore.preflight");
    if (!RestoreWindow_(mgr) || !RestoreCaptureValid_(capture, error)) {
        if (error && error->empty()) *error = "not at outer player message boundary";
        return false;
    }
    std::string key;
    if (!BattleFingerprint_(mgr, &key, error) || key != expectedKey) {
        if (error) *error = "battle fingerprint changed";
        return false;
    }
    std::unique_ptr<CodecCapture> beforeStorage(new CodecCapture{});
    CodecCapture& before = *beforeStorage;
    if (!CaptureBattle_(mgr, &before, error) || !RestorePolicy_(capture, error)
        || !RestorePolicy_(before, error)) return false;
    for (int side = 0; side < 2; ++side) {
        if (mgr->hero[side] && IsBadWritePtr(&mgr->hero[side]->spellPoints, sizeof(INT16))) {
            if (error) *error = "hero mana not writable"; return false;
        }
    }
    if (IsBadWritePtr(mgr, sizeof(*mgr))) {
        if (error) *error = "battle memory not writable"; return false;
    }
    DiagStage_("restore.obstacles-preflight");
    if (!RestoreObstaclePreflight_(mgr, capture, error)
        || !RestoreObstaclePreflight_(mgr, before, error)) return false;
    if (IsBadWritePtr(mgr->dlg, sizeof(*mgr->dlg))) {
        if (error) *error = "log dialog not writable"; return false;
    }
    H3Vector<H3String*>* log = &mgr->dlg->GetCombatText();
    RestoreRngGuard_ rngGuard(before);
    std::unique_ptr<RestoreLogBuffer, RestoreLogBufferDeleter_> prepared(new RestoreLogBuffer{});
    if (!RestoreLogPrepareSeh_(prepared.get(), capture.logLines)) {
        if (error) *error = "log preallocation failed"; return false;
    }
    RestoreObstacleResources_ resources;
    if (!resources.Prepare(mgr, before, capture, error)) {
        if (!g_restoreFatal && !resources.Release(error)) return false;
        return false;
    }
    std::unique_ptr<RestoreObjects_> objects(new RestoreObjects_{});
    DiagStage_("restore.objects-prepare");
    if (!objects->Prepare(mgr, before, capture, error)) return false;
    std::unique_ptr<RestoreSiege_> siege(new RestoreSiege_{});
    if (!siege->Prepare(mgr, before, capture, error)) return false;
    // Preparation may call game initialization; preserve the original random stream.
    if (!RestoreRngSeh_(before.rngTlsSeed, before.rngMirrorSeed)) {
        if (error) *error = "恢复随机数状态时发生异常，已停止战斗";
        return false;
    }
    g_diag.writing = true;
    DiagStage_("restore.commit-objects");
    if (!RestoreApply_(mgr, capture, resources, objects.get(), error, siege.get())) {
        // v4: a clean obstacle reject happens before the first structural write
        // (refcounts balanced, nothing to roll back); a fault sets g_restoreFatal
        // and stops the session without attempting further writes.
        if (error && error->empty())
            *error = g_restoreFatal ? "obstacle rebuild fault; stopping with partial write" : "obstacle rebuild failed";
        g_diag.writing = g_restoreFatal;
        return false;
    }
    DiagStage_("restore.log-swap");
    log->swap(prepared->lines);
    if (!RestoreRenderSeh_(mgr, capture)) {
        if (error) *error = "恢复后刷新战场发生异常，已停止战斗";
        return false;
    }
    DiagStage_("restore.verify");
    if (!DiagVerifyRestore_(mgr, capture)) {
        DiagStage_("restore.rollback");
        if (!RestoreApply_(mgr, before, resources, objects.get(), error, siege.get())) {
            // Rollback uses only pinned resources. A primitive fault still leaves
            // an unverified written state, so the session must stop.
            g_restoreFatal = true;
            if (error) *error = "rollback fault; stopping with unverified state";
            g_diag.writing = true;
            return false;
        }
        log->swap(prepared->lines);
        if (!RestoreRenderSeh_(mgr, before)) {
            if (error) *error = "回滚后刷新战场发生异常，已停止战斗";
            return false;
        }
        if (!DiagVerifyRestore_(mgr, before)) g_restoreFatal = true;
        if (error) *error = g_restoreFatal ? "rollback verification failed" : "restore mismatch; rolled back";
        if (!g_restoreFatal) {
            objects->Release(error);
            if (!g_restoreFatal) siege->Release(error);
            if (!g_restoreFatal) resources.Release(error);
            if (!g_restoreFatal) {
                prepared.reset();
                if (!g_restoreFatal && !rngGuard.Finish(before) && error)
                    *error = "回滚随机数状态时发生异常，已停止战斗";
            }
        }
        g_diag.writing = g_restoreFatal;
        return false;
    }
    if (!objects->Release(error) || !siege->Release(error) || !resources.Release(error)) { g_diag.writing = true; return false; }
    // Destroy old log ownership too, before the final game operation (RNG commit).
    prepared.reset();
    if (g_restoreFatal || !rngGuard.Finish(capture)) {
        if (error) *error = "提交随机数状态时发生异常，已停止战斗";
        g_diag.writing = true;
        return false;
    }
    g_diag.writing = false;
    DiagStage_("restore.verified");
    return true;
}
