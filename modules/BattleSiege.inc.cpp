// Prepared siege ownership; include after BattleObjects and before RestoreApply.
// Each non-null slot owns one native reference, even when pointers are equal.
using SiegeDefLoadFn_ = H3LoadedDef* (__fastcall*)(const char*);
using SiegePcxLoadFn_ = H3LoadedPcx* (__fastcall*)(const char*);
static SiegeDefLoadFn_ kSiegeDefLoad_ = (SiegeDefLoadFn_)0x55C9C0;
static SiegePcxLoadFn_ kSiegePcxLoad_ = (SiegePcxLoadFn_)0x55AA10;

static H3LoadedDef* SiegeDefLoadSeh_(const char* name, bool* fault)
{
    *fault = false;
    __try { return kSiegeDefLoad_(name); }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { *fault = true; }
    return nullptr;
}

static H3LoadedPcx* SiegePcxLoadSeh_(const char* name, bool* fault)
{
    *fault = false;
    __try { return kSiegePcxLoad_(name); }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { *fault = true; }
    return nullptr;
}

static bool SiegeNameValid_(const char* name)
{
    return memchr(name, 0, 13) != nullptr;
}

static bool SiegeResourceReady_(const H3ResourceItem* item, const char* name)
{
    if (!Readable_(item, sizeof(*item))) return false;
    const uint8_t* raw = (const uint8_t*)item;
    const int32_t refs = *(const int32_t*)(raw + 0x18);
    void* const* vtable = *(void* const* const*)raw;
    // The protected H3API name is 12 bytes followed by a zero DWORD.
    return refs > 0 && refs < 0x7FFFFFFF
        && !IsBadWritePtr((void*)(raw + 0x18), sizeof(int32_t))
        && *(const uint32_t*)(raw + 0x10) == 0
        && Readable_(vtable, 2 * sizeof(void*)) && Readable_(vtable[1], 1)
        && SiegeNameValid_(name) && name[0]
        && _strnicmp((const char*)raw + 4, name, 13) == 0;
}

static bool SiegeFrameReady_(const H3DefFrame* frame)
{
    return Readable_(frame, sizeof(*frame)) && frame->width > 0 && frame->width <= 8192
        && frame->height > 0 && frame->height <= 8192
        && frame->frameWidth >= 0 && frame->frameWidth <= frame->width
        && frame->frameHeight >= 0 && frame->frameHeight <= frame->height
        && frame->marginLeft >= 0 && frame->marginLeft <= frame->width - frame->frameWidth
        && frame->marginTop >= 0 && frame->marginTop <= frame->height - frame->frameHeight
        && frame->rawDataSize > 0 && frame->rawDataSize <= 64 * 1024 * 1024
        && frame->dataSize >= 0 && frame->dataSize <= 64 * 1024 * 1024
        && frame->compressionType >= 0 && frame->compressionType <= 3
        && Readable_(frame->rawData, (size_t)frame->rawDataSize);
}

static bool SiegeDefReady_(const H3LoadedDef* def, const char* name, int sequence, int frameIndex)
{
    if (!SiegeResourceReady_(def, name) || !Readable_(def, sizeof(*def))
        || def->groupsCount < 1 || def->groupsCount > 1024
        || sequence < 0 || sequence >= def->groupsCount
        || !Readable_(def->groups, def->groupsCount * sizeof(*def->groups))
        || !Readable_(def->activeGroups, def->groupsCount * sizeof(*def->activeGroups))
        || !def->activeGroups[0] || !def->activeGroups[sequence]
        || def->widthDEF <= 0 || def->widthDEF > 8192
        || def->heightDEF <= 0 || def->heightDEF > 8192
        || !Readable_(def->palette565, sizeof(H3Palette565))) return false;
    const H3LoadedDef::DefGroup* base = def->groups[0];
    const H3LoadedDef::DefGroup* group = def->groups[sequence];
    if (!Readable_(base, sizeof(*base)) || base->count < 1 || base->count > 100000
        || !Readable_(group, sizeof(*group)) || group->count < 1 || group->count > 100000
        || frameIndex < 0 || frameIndex >= group->count
        || !Readable_(group->frames, group->count * sizeof(*group->frames))) return false;
    return SiegeFrameReady_(group->frames[frameIndex]);
}

static bool SiegePcxReady_(const H3LoadedPcx* pcx, const char* name)
{
    if (!SiegeResourceReady_(pcx, name) || !Readable_(pcx, sizeof(*pcx))
        || pcx->width < 1 || pcx->width > 8192 || pcx->height < 1 || pcx->height > 8192
        || pcx->scanlineSize < pcx->width || pcx->scanlineSize > 32768
        || pcx->bufSize < 1 || pcx->bufSize > 64 * 1024 * 1024) return false;
    const uint64_t needed = (uint64_t)pcx->scanlineSize * (uint64_t)pcx->height;
    return needed <= (uint64_t)pcx->bufSize && Readable_(pcx->buffer, (size_t)needed);
}

static bool SiegeCaptureReady_(const CodecCapture& capture)
{
    for (int i = 0; i < 3; ++i)
        if (!SiegeNameValid_(capture.towers[i].defName)
            || !SiegeNameValid_(capture.towers[i].missileName)) return false;
    for (int i = 0; i < 90; ++i)
        if (!SiegeNameValid_(capture.wallPcxNames[i])) return false;
    if (capture.siegeKind2 > 0) {
        if (!capture.wallPcxNames[20][0]) return false; // Renderer unconditionally draws wall[4][0].
        for (int i = 0; i < 18; ++i)
            if (capture.fortWallsAlive[i] < 0 || capture.fortWallsAlive[i] > 4) return false;
        const int32_t door = capture.siegeKind; // H3API +0x53A4, native door status.
        if (door < 0 || door > 3 || (door != 3 && !capture.wallPcxNames[door][0])) return false;
        // Other walls have a native null check, including wall[14].
    }
    return true;
}

struct RestoreSiege_
{
    using Tower = H3CombatManager::TownTowerLoaded;
    Tower towers[3] = {};
    H3LoadedPcx* walls[18][5] = {};
    Tower expectedTowers[3] = {};
    H3LoadedPcx* expectedWalls[18][5] = {};
    H3CombatManager* manager = nullptr;
    bool ready = false;
    bool switched = false;

    RestoreSiege_() = default;
    RestoreSiege_(const RestoreSiege_&) = delete;
    RestoreSiege_& operator=(const RestoreSiege_&) = delete;
    ~RestoreSiege_() { if (!g_restoreFatal) Release(nullptr); }

    static bool Reject(const char* why, std::string* error) {
        if (error) *error = why;
        return false;
    }
    static bool Fault(const char* why, std::string* error) {
        g_restoreFatal = true;
        return Reject(why, error);
    }
    H3ResourceItem* Owned(int index) const {
        if (index < 6) return index % 2 ? towers[index / 2].shotDefLoaded : towers[index / 2].monDefLoaded;
        return walls[(index - 6) / 5][(index - 6) % 5];
    }
    static H3ResourceItem* LiveOwned(const H3CombatManager* mgr, int index) {
        if (index < 6) return index % 2 ? mgr->towers[index / 2].shotDefLoaded : mgr->towers[index / 2].monDefLoaded;
        return mgr->townSiegePcx[(index - 6) / 5][(index - 6) % 5];
    }
    static const char* Name(const CodecCapture& capture, int index) {
        if (index < 6) return index % 2 ? capture.towers[index / 2].missileName : capture.towers[index / 2].defName;
        return capture.wallPcxNames[index - 6];
    }
    static bool TowerMatches(const Tower& live, const CodecTower_& saved) {
        int32_t frameIndex = 0;
        // H3API mislabels +0x1C as H3DefFrame*: native initialization/drawing uses an index.
        memcpy(&frameIndex, (const uint8_t*)&live + 0x1C, sizeof(frameIndex));
        return live.crType2Shot == saved.scalars[0] && live.creatureX == saved.scalars[1]
            && live.creatureY == saved.scalars[2] && live.orientation == saved.scalars[3]
            && live.defGroup == saved.scalars[4] && frameIndex == saved.scalars[5]
            && live.stackNumber == saved.scalars[6];
    }
    static bool ResourceMatches(const H3ResourceItem* item, const CodecCapture& capture, int index) {
        const char* name = Name(capture, index);
        if (!item) return !name[0];
        if (index >= 6) return SiegePcxReady_((const H3LoadedPcx*)item, name);
        const CodecTower_& tower = capture.towers[index / 2];
        const H3LoadedDef* def = (const H3LoadedDef*)item;
        if (!SiegeDefReady_(def, name, index % 2 ? 0 : tower.scalars[4],
            index % 2 ? 0 : tower.scalars[5])) return false;
        if (index % 2) {
            const H3LoadedDef::DefGroup* group = def->groups[0];
            for (int frame = 0; frame < group->count; ++frame)
                if (!SiegeFrameReady_(group->frames[frame])) return false;
        }
        return true;
    }
    bool ReferencesCoverSlots(const H3CombatManager* mgr) const {
        for (int i = 0; i < (mgr ? 192 : 96); ++i) {
            const H3ResourceItem* item = i < 96 ? Owned(i) : LiveOwned(mgr, i - 96);
            if (!item) continue;
            if (!Readable_(item, sizeof(*item))) return false;
            int aliases = 0;
            for (int j = 0; j < 96; ++j) {
                if (Owned(j) == item) ++aliases;
                if (mgr && LiveOwned(mgr, j) == item) ++aliases;
            }
            const int32_t refs = *(const int32_t*)((const uint8_t*)item + 0x18);
            if (refs < aliases || refs >= 0x7FFFFFFF) return false;
        }
        return true;
    }
    bool PreflightSeh(H3CombatManager* mgr, const CodecCapture* before,
        const CodecCapture* saved, bool* fault) {
        *fault = false;
        __try {
            if (!Readable_(mgr, sizeof(*mgr)) || IsBadWritePtr(mgr->towers, sizeof(mgr->towers))
                || IsBadWritePtr(mgr->townSiegePcx, sizeof(mgr->townSiegePcx))
                || mgr->finished || !g_battleInitialized || mgr->siegeKind2 != before->siegeKind2
                || !SiegeCaptureReady_(*before) || !SiegeCaptureReady_(*saved)) return false;
            for (int i = 0; i < 3; ++i)
                if (!TowerMatches(mgr->towers[i], before->towers[i])) return false;
            for (int i = 0; i < 96; ++i)
                if (!ResourceMatches(LiveOwned(mgr, i), *before, i)) return false;
            if (!ReferencesCoverSlots(mgr)) return false;
            memcpy(expectedTowers, mgr->towers, sizeof(expectedTowers));
            memcpy(expectedWalls, mgr->townSiegePcx, sizeof(expectedWalls));
        }
        __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { *fault = true; return false; }
        return true;
    }
    bool ValidateOwnedSeh(const CodecCapture* saved, int index, bool* fault, bool* releasable) {
        *fault = false;
        *releasable = false;
        __try {
            *releasable = SiegeResourceReady_(Owned(index), Name(*saved, index))
                && ReferencesCoverSlots(manager);
            return *releasable && ResourceMatches(Owned(index), *saved, index);
        }
        __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { *fault = true; return false; }
    }
    bool Prepare(H3CombatManager* mgr, const CodecCapture& before,
        const CodecCapture& saved, std::string* error) {
        static_assert(sizeof(Tower) == 0x24, "native siege tower stride");
        static_assert(sizeof(H3LoadedPcx*) == 4, "native siege pointer width");
        if (g_restoreFatal) return Reject("攻城资源事务已进入异常停止状态", error);
        if (manager || ready) return Reject("攻城资源事务不能重复准备", error);
        bool fault = false;
        DiagStage_("restore.siege-preflight");
        if (!PreflightSeh(mgr, &before, &saved, &fault))
            return fault ? Fault("攻城资源预检发生异常，已停止战斗", error)
                : Reject("当前攻城资源、引用、名称或塔状态已漂移，或保存的渲染资源不足", error);
        manager = mgr;
        for (int i = 0; i < 3; ++i) {
            const CodecTower_& source = saved.towers[i];
            towers[i].crType2Shot = source.scalars[0];
            towers[i].creatureX = source.scalars[1];
            towers[i].creatureY = source.scalars[2];
            towers[i].orientation = source.scalars[3];
            towers[i].defGroup = source.scalars[4];
            memcpy((uint8_t*)&towers[i] + 0x1C, &source.scalars[5], sizeof(int32_t));
            towers[i].stackNumber = source.scalars[6];
        }
        DiagStage_("restore.siege-preload");
        for (int i = 0; i < 96; ++i) {
            const char* name = Name(saved, i);
            if (!name[0]) continue;
            // Load is called for every occupied slot: native cache hits acquire ref++.
            if (i < 6) {
                H3LoadedDef*& slot = i % 2 ? towers[i / 2].shotDefLoaded : towers[i / 2].monDefLoaded;
                slot = SiegeDefLoadSeh_(name, &fault);
            }
            else walls[(i - 6) / 5][(i - 6) % 5] = SiegePcxLoadSeh_(name, &fault);
            if (fault) return Fault("预加载攻城资源发生异常，已停止战斗且不再清理原生资源", error);
            if (!Owned(i)) {
                if (!Release(error)) return false;
                return Reject("保存的攻城资源无法加载，已撤销新资源引用", error);
            }
            bool releasable = false;
            if (!ValidateOwnedSeh(&saved, i, &fault, &releasable)) {
                if (fault || !releasable)
                    return Fault("预加载攻城资源的原生对象或引用无效，已停止战斗", error);
                if (!Release(error)) return false;
                return Reject("保存的攻城 DEF 帧、弹道组零帧或墙 PCX 尺寸及缓冲无效", error);
            }
        }
        ready = true;
        return true;
    }
    bool SwitchSeh(H3CombatManager* mgr) {
        __try {
            if (!Readable_(mgr, sizeof(*mgr)) || mgr->finished
                || IsBadWritePtr(mgr->towers, sizeof(mgr->towers))
                || IsBadWritePtr(mgr->townSiegePcx, sizeof(mgr->townSiegePcx))
                || (!switched && memcmp(mgr->towers, expectedTowers, sizeof(expectedTowers)))
                || memcmp(mgr->townSiegePcx, expectedWalls, sizeof(expectedWalls))) return false;
            for (int i = 0; i < 3; ++i)
                if (mgr->towers[i].monDefLoaded != expectedTowers[i].monDefLoaded
                    || mgr->towers[i].shotDefLoaded != expectedTowers[i].shotDefLoaded) return false;
            if (!ReferencesCoverSlots(mgr)) return false;
            ObjectSwapBytes_(mgr->towers, towers, sizeof(towers));
            ObjectSwapBytes_(mgr->townSiegePcx, walls, sizeof(walls));
            memcpy(expectedTowers, mgr->towers, sizeof(expectedTowers));
            memcpy(expectedWalls, mgr->townSiegePcx, sizeof(expectedWalls));
            switched = !switched;
        }
        __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { return false; }
        return true;
    }
    bool Switch(H3CombatManager* mgr) {
        if (g_restoreFatal) return false;
        if (!ready || mgr != manager || !SwitchSeh(mgr))
            return Fault("交换攻城资源发生异常或状态漂移，已停止战斗", nullptr);
        return true;
    }
    bool ReleaseSlotSeh(int index) {
        H3ResourceItem* item = nullptr;
        void* const* vtable = nullptr;
        int32_t refs = 0;
        __try {
            item = Owned(index);
            if (!Readable_(item, sizeof(*item))) return false;
            refs = *(const int32_t*)((const uint8_t*)item + 0x18);
            vtable = *(void* const* const*)item;
            if (refs < 1 || refs >= 0x7FFFFFFF || !Readable_(vtable, 2 * sizeof(void*))
                || !Readable_(vtable[1], 1) || !ReferencesCoverSlots(nullptr)) return false;
            // Forget the slot before calling the one native primitive; never retry a fault.
            if (index < 6) {
                if (index % 2) towers[index / 2].shotDefLoaded = nullptr;
                else towers[index / 2].monDefLoaded = nullptr;
            }
            else walls[(index - 6) / 5][(index - 6) % 5] = nullptr;
        }
        __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { return false; }
        // The common resource vtable +4 Dereference takes only this for DEF and PCX.
        return ObstacleDerefSeh_((H3LoadedDef*)item);
    }
    bool Release(std::string* error) {
        if (g_restoreFatal) return Reject("攻城资源事务已异常停止，跳过原生资源释放", error);
        ready = false;
        DiagStage_("restore.siege-release");
        for (int i = 0; i < 96; ++i)
            if (Owned(i) && !ReleaseSlotSeh(i))
                return Fault("释放攻城资源发生异常或引用无效，已停止战斗且不再释放其他槽", error);
        manager = nullptr;
        switched = false;
        return true;
    }
};
