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
    // 2026-10-10 v10 玩家日志：名字匹配且尺寸正常的 PCX/DEF，其 +0x10
    // 分别为 0x9a625c00 / 0x785c6600 / 0x00641000，不能要求整个 DWORD 为零。
    // SgFrDrw3.pcx 长12字节，NUL在+0x10；高位字节来源尚未验证。
    // SiegeNameValid_ 验证预期名在13字节内终止；_strnicmp 比较实际资源名。
    // 保留引用、虚表及后续几何/帧检查，不按 nameEnd 高位值拒绝。
    return refs > 0 && refs < 0x7FFFFFFF
        && !IsBadWritePtr((void*)(raw + 0x18), sizeof(int32_t))
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

static bool SiegeCaptureReadyDiag_(const CodecCapture& capture, int* aux, int* index)
{
    *aux = 0; *index = -1;
    for (int i = 0; i < 3; ++i)
        if (!SiegeNameValid_(capture.towers[i].defName)
            || !SiegeNameValid_(capture.towers[i].missileName)) { *aux = 1; *index = i; return false; }
    for (int i = 0; i < 90; ++i)
        if (!SiegeNameValid_(capture.wallPcxNames[i])) { *aux = 1; *index = 6 + i; return false; }
    if (capture.siegeKind2 > 0) {
        if (!capture.wallPcxNames[20][0]) { *aux = 2; *index = 20; return false; } // Renderer unconditionally draws wall[4][0].
        for (int i = 0; i < 18; ++i)
            if (capture.fortWallsAlive[i] < 0 || capture.fortWallsAlive[i] > 4) { *aux = 3; *index = i; return false; }
        const int32_t door = capture.siegeKind; // H3API +0x53A4, native door status.
        if (door < 0 || door > 3) { *aux = 4; *index = -1; return false; }
        if (door != 3 && !capture.wallPcxNames[door][0]) { *aux = 4; *index = door; return false; }
        // Other walls have a native null check, including wall[14].
    }
    return true;
}

// 2026-10-10 攻城读档细分诊断（玩家日志：攻城战读档全部拒绝于 preflight，
// 总原因无法区分 ready/塔/资源/引用哪个子条件）。POD 摘要在 SEH 内填写，
// 字符串与日志统一在 SEH 外构造。
struct SiegePreflightDiag_ {
    int reason = 0;   // 1 protect 2 state 3 kind2 4 before-ready 5 saved-ready 6 tower 7 resource 8 references
    int index = -1;   // ready: 槽号; tower: 0..2; resource: 0..95
    int aux = 0;      // ready 子项: 1 name-slot 2 wall20 3 fortwalls 4 door
    char name[13] = {};
};

static const char* SiegeDiagReason_(int reason)
{
    switch (reason) {
    case 1: return "protect";
    case 2: return "state";
    case 3: return "kind2";
    case 4: return "before-ready";
    case 5: return "saved-ready";
    case 6: return "tower-match";
    case 7: return "resource-match";
    case 8: return "references";
    default: return "unknown";
    }
}

struct SiegeDiagSlotData_ {
    int readable; int refs; unsigned field10; int nameCmp;
    int w, h, scanline, bufSize, needed;
    int groups, defW, defH;
};
static void SiegeDiagSlotSeh_(const H3ResourceItem* item, const char* name, SiegeDiagSlotData_* d)
{
    memset(d, 0, sizeof(*d));
    d->nameCmp = -1;
    __try {
        if (!Readable_(item, sizeof(*item))) return;
        d->readable = 1;
        d->refs = *(const int32_t*)((const uint8_t*)item + 0x18);
        d->field10 = *(const uint32_t*)((const uint8_t*)item + 0x10);
        if (name[0]) d->nameCmp = _strnicmp((const char*)((const uint8_t*)item + 4), name, 13);
        // PCX 与 DEF 字段重叠读取，日志侧按槽号解释。
        const H3LoadedPcx* pcx = (const H3LoadedPcx*)item;
        d->w = pcx->width; d->h = pcx->height; d->scanline = pcx->scanlineSize; d->bufSize = pcx->bufSize;
        d->needed = (int)((uint64_t)(unsigned)pcx->scanlineSize * (uint64_t)(unsigned)pcx->height);
        const H3LoadedDef* def = (const H3LoadedDef*)item;
        d->groups = def->groupsCount; d->defW = def->widthDEF; d->defH = def->heightDEF;
    }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { }
}

static void SiegeDiagTowerSeh_(const H3CombatManager::TownTowerLoaded* live, int out[7])
{
    __try {
        out[0] = live->crType2Shot; out[1] = live->creatureX; out[2] = live->creatureY;
        out[3] = live->orientation; out[4] = live->defGroup;
        memcpy(&out[5], (const uint8_t*)live + 0x1C, sizeof(int32_t));
        out[6] = live->stackNumber;
    }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { }
}

static void CopySlotName_(const CodecCapture& capture, int index, char out[13])
{
    const char* name = index < 6
        ? (index % 2 ? capture.towers[index / 2].missileName : capture.towers[index / 2].defName)
        : capture.wallPcxNames[index - 6];
    memcpy(out, name, 12);
    out[12] = 0;
}

static void SiegePreflightDiagnose_(H3CombatManager* mgr, const CodecCapture* before,
    const CodecCapture* saved, const SiegePreflightDiag_& d);

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
        const CodecCapture* saved, bool* fault, SiegePreflightDiag_* diag) {
        *fault = false;
        __try {
            if (!Readable_(mgr, sizeof(*mgr)) || IsBadWritePtr(mgr->towers, sizeof(mgr->towers))
                || IsBadWritePtr(mgr->townSiegePcx, sizeof(mgr->townSiegePcx))) { diag->reason = 1; return false; }
            if (mgr->finished || !g_battleInitialized) { diag->reason = 2; return false; }
            if (mgr->siegeKind2 != before->siegeKind2) { diag->reason = 3; return false; }
            if (!SiegeCaptureReadyDiag_(*before, &diag->aux, &diag->index)) { diag->reason = 4; return false; }
            if (!SiegeCaptureReadyDiag_(*saved, &diag->aux, &diag->index)) { diag->reason = 5; return false; }
            for (int i = 0; i < 3; ++i)
                if (!TowerMatches(mgr->towers[i], before->towers[i])) { diag->reason = 6; diag->index = i; return false; }
            for (int i = 0; i < 96; ++i)
                if (!ResourceMatches(LiveOwned(mgr, i), *before, i)) {
                    diag->reason = 7; diag->index = i;
                    CopySlotName_(*before, i, diag->name);
                    return false;
                }
            if (!ReferencesCoverSlots(mgr)) { diag->reason = 8; return false; }
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
        SiegePreflightDiag_ diag = {};
        DiagStage_("restore.siege-preflight");
        if (!PreflightSeh(mgr, &before, &saved, &fault, &diag)) {
            SiegePreflightDiagnose_(mgr, &before, &saved, diag);
            return fault ? Fault("攻城资源预检发生异常，已停止战斗", error)
                : Reject("当前攻城资源、引用、名称或塔状态已漂移，或保存的渲染资源不足", error);
        }
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

// 引用覆盖诊断采集：SEH 内只写 POD（static 缓冲由调用方提供，单游戏线程、一次性诊断）。
static int SiegeDiagReferencesSeh_(H3CombatManager* mgr, int* issueSlot, int* issueRefs, int* issueAlias)
{
    int issues = 0;
    __try {
        for (int i = 0; i < 96 && issues < 96; ++i) {
            const H3ResourceItem* item = RestoreSiege_::LiveOwned(mgr, i);
            if (!item || !Readable_(item, sizeof(*item))) continue;
            int aliases = 0;
            for (int j = 0; j < 96; ++j)
                if (RestoreSiege_::LiveOwned(mgr, j) == item) ++aliases;
            const int32_t refs = *(const int32_t*)((const uint8_t*)item + 0x18);
            if (refs < aliases || refs <= 0 || refs >= 0x7FFFFFFF) {
                issueSlot[issues] = i; issueRefs[issues] = refs; issueAlias[issues] = aliases;
                ++issues;
            }
        }
    }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { }
    return issues;
}

// SEH 外统一落盘：攻城预检拒绝时输出能区分子条件的字段，一次复现即可定位
// （2026-10-10 玩家日志：保存成功后立即读档也在 preflight 被确定性拒绝）。
static void SiegePreflightDiagnose_(H3CombatManager* mgr, const CodecCapture* before,
    const CodecCapture* saved, const SiegePreflightDiag_& d)
{
    if (d.reason == 4 || d.reason == 5) {
        const CodecCapture& c = d.reason == 4 ? *before : *saved;
        std::string emptyWalls;
        for (int i = 0; i < 90; ++i)
            if (!c.wallPcxNames[i][0]) {
                if (!emptyWalls.empty()) emptyWalls += ',';
                emptyWalls += std::to_string(i);
            }
        LogInfo("[Restore] siege-diag reason=%s aux=%d index=%d kind2=%d kind(door)=%d wall20=%s emptyWallSlots=%s",
            SiegeDiagReason_(d.reason), d.aux, d.index, c.siegeKind2, c.siegeKind,
            c.wallPcxNames[20][0] ? c.wallPcxNames[20] : "(empty)",
            emptyWalls.empty() ? "(none)" : emptyWalls.c_str());
        return;
    }
    if (d.reason == 6) {
        const int i = d.index >= 0 && d.index < 3 ? d.index : 0;
        int live[7] = {};
        SiegeDiagTowerSeh_(&mgr->towers[i], live);
        const CodecTower_& b = before->towers[i];
        LogInfo("[Restore] siege-diag reason=tower-match index=%d live=%d,%d,%d,%d,%d,%d,%d before=%d,%d,%d,%d,%d,%d,%d",
            i, live[0], live[1], live[2], live[3], live[4], live[5], live[6],
            b.scalars[0], b.scalars[1], b.scalars[2], b.scalars[3], b.scalars[4], b.scalars[5], b.scalars[6]);
        return;
    }
    if (d.reason == 7) {
        const int i = d.index >= 0 && d.index < 96 ? d.index : 0;
        const H3ResourceItem* item = RestoreSiege_::LiveOwned(mgr, i);
        char name[13] = {};
        CopySlotName_(*before, i, name);
        SiegeDiagSlotData_ s = {};
        SiegeDiagSlotSeh_(item, name, &s);
        LogInfo("[Restore] siege-diag reason=resource-match index=%d name=%s item=%p readable=%d refs=%d field10=%x nameCmp=%d pcx(w=%d h=%d scanline=%d buf=%d needed=%d) def(groups=%d w=%d h=%d)",
            i, name, (const void*)item, s.readable, s.refs, s.field10, s.nameCmp,
            s.w, s.h, s.scanline, s.bufSize, s.needed, s.groups, s.defW, s.defH);
        return;
    }
    if (d.reason == 8) {
        // 引用覆盖失败：列出 refs<aliases 或 refs 非法的槽。static POD 缓冲，
        // SEH 内只写 POD；单游戏线程且诊断一次性，无重入。
        static int issueSlot[96];
        static int issueRefs[96];
        static int issueAlias[96];
        const int issues = SiegeDiagReferencesSeh_(mgr, issueSlot, issueRefs, issueAlias);
        std::string detail;
        for (int k = 0; k < issues; ++k) {
            if (!detail.empty()) detail += ' ';
            detail += "slot" + std::to_string(issueSlot[k]) + ":refs" + std::to_string(issueRefs[k])
                + "/alias" + std::to_string(issueAlias[k]);
        }
        LogInfo("[Restore] siege-diag reason=references issues=%d detail=%s",
            issues, detail.empty() ? "(none)" : detail.c_str());
        return;
    }
    LogInfo("[Restore] siege-diag reason=%s index=%d", SiegeDiagReason_(d.reason), d.index);
}
