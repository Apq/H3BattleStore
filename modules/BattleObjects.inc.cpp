// Prepared native ownership. No resource loading or allocation during Switch().
using StackCtorFn_ = H3CombatCreature* (__thiscall*)(H3CombatCreature*);
using StackDtorFn_ = void (__thiscall*)(H3CombatCreature*);
using StackInitFn_ = void (__thiscall*)(H3CombatCreature*, int, int, H3Hero*, int, int, int);
using StackResourcesFn_ = void (__thiscall*)(H3CombatCreature*);
using GameAllocFn_ = void* (__cdecl*)(unsigned);
using GameFreeFn_ = void (__cdecl*)(void*);
using SpellSetCtorFn_ = void* (__thiscall*)(void*, const char*, const char*);
using SpellSetInsertFn_ = void (__thiscall*)(void*, void*, const int*);
using SpellSetDtorFn_ = void (__thiscall*)(void*);
static StackCtorFn_ kStackCtor_ = (StackCtorFn_)0x43CF70;
static StackDtorFn_ kStackDtor_ = (StackDtorFn_)0x43D120;
static StackInitFn_ kStackInit_ = (StackInitFn_)0x43D450;
static StackResourcesFn_ kStackResources_ = (StackResourcesFn_)0x43D710;
static GameAllocFn_ kGameAlloc_ = (GameAllocFn_)0x617492;
static GameFreeFn_ kGameFree_ = (GameFreeFn_)0x60B0F0;
static SpellSetCtorFn_ kSpellSetCtor_ = (SpellSetCtorFn_)0x46A230;
static SpellSetInsertFn_ kSpellSetInsert_ = (SpellSetInsertFn_)0x5A9760;
static SpellSetDtorFn_ kSpellSetDtor_ = (SpellSetDtorFn_)0x462490;
static const int kObjectGuard_ = GuardRegisterHook_("BattleStore.Objects");

static bool ObjectStackConstructSeh_(H3CombatCreature* stack, const CodecStack* source, H3Hero* hero)
{
    __try {
        kStackCtor_(stack);
        stack->type = -1;
        if (source && source->occupied) {
            kStackInit_(stack, source->type, source->numberAtStart, hero,
                source->side, source->sideIndex, source->position);
            if (source->type != 149) kStackResources_(stack);
        }
    }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { return false; }
    return true;
}
static StackDtorFn_ kDequeTrimTail_ = (StackDtorFn_)0x448E00;

static bool ObjectStackDestroySeh_(H3CombatCreature* stack)
{
    __try {
        uint32_t* deque = (uint32_t*)((uint8_t*)stack + 0x420);
        // Only on detached ownership, immediately before destruction. The native
        // append may leave an empty tail which pop_front otherwise never frees.
        if (deque[11] && deque[7] == deque[5] && deque[8] > deque[4])
            kDequeTrimTail_((H3CombatCreature*)deque);
        kStackDtor_(stack);
    }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { return false; }
    return true;
}
static void* ObjectAllocateSeh_(unsigned bytes, bool* fault)
{
    *fault = false;
    __try { return kGameAlloc_(bytes); }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { *fault = true; }
    return nullptr;
}
static bool ObjectFreeSeh_(void* object)
{
    __try { kGameFree_(object); }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { return false; }
    return true;
}
static bool ObjectSetConstructSeh_(void* object)
{
    const char zero = 0;
    __try { kSpellSetCtor_(object, &zero, &zero); }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { return false; }
    return true;
}
static bool ObjectSetInsertSeh_(void* object, int spell)
{
    uint32_t result[2] = {};
    __try { kSpellSetInsert_(object, result, &spell); }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { return false; }
    return true;
}
static bool ObjectSetDestroySeh_(void* object)
{
    __try { kSpellSetDtor_(object); }
    __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { return false; }
    return true;
}
static void ObjectSwapBytes_(void* left, void* right, size_t count)
{
    uint8_t* a = (uint8_t*)left;
    uint8_t* b = (uint8_t*)right;
    for (size_t i = 0; i < count; ++i) std::swap(a[i], b[i]);
}

static uint8_t* kObjectResourceMap_ = (uint8_t*)0x69E560;
static const uint8_t* const* kObjectResourceNilSlot_ = (const uint8_t* const*)0x69E604;
static uint8_t* kObjectTextBuffer_ = (uint8_t*)0x697428;

// Resource initialization overwrites cached WAV parameters even when only an
// outside owner uses that sound. Existing cache refs stay alive during Prepare.
struct ObjectSoundGuard_ {
    struct Entry { uint8_t* resource; uint32_t parameters[3]; };
    std::vector<Entry> entries;
    uint8_t text[512] = {};
    bool textCaptured = false;
    bool Add(uint8_t* resource) {
        if (!resource) return true;
        for (const auto& entry : entries) if (entry.resource == resource) return true;
        if (!Readable_(resource, 0x34) || IsBadWritePtr(resource + 0x28, 12)
            || *(const int32_t*)(resource + 0x18) < 1) return false;
        Entry entry = {}; entry.resource = resource;
        memcpy(entry.parameters, resource + 0x28, 12);
        entries.push_back(entry);
        return true;
    }
    bool CaptureImpl(H3CombatManager* mgr) {
        if (kObjectTextBuffer_) {
            if (!Readable_(kObjectTextBuffer_, sizeof(text)) || IsBadWritePtr(kObjectTextBuffer_, sizeof(text))) return false;
            memcpy(text, kObjectTextBuffer_, sizeof(text));
            textCaptured = true;
        }
        // VC6 resource map: header +4=head,+C=count; node +1C=item,+20=color.
        if (kObjectResourceMap_) {
            if (!Readable_(kObjectResourceMap_, 16)) return false;
            const uint8_t* head = *(const uint8_t* const*)(kObjectResourceMap_ + 4);
            const uint32_t count = *(const uint32_t*)(kObjectResourceMap_ + 12);
            if (count > 65536 || !Readable_(head, 0x24)
                || !Readable_(kObjectResourceNilSlot_, sizeof(void*))) return false;
            const uint8_t* nil = *kObjectResourceNilSlot_;
            if (!Readable_(nil, 0x24)) return false;
            if (count) {
                std::vector<const uint8_t*> pending, visited;
                pending.push_back(*(const uint8_t* const*)(head + 4));
                while (!pending.empty()) {
                    const uint8_t* node = pending.back(); pending.pop_back();
                    if (node == head || node == nil || !Readable_(node, 0x24)
                        || std::find(visited.begin(), visited.end(), node) != visited.end()
                        || visited.size() >= count) return false;
                    visited.push_back(node);
                    uint8_t* item = *(uint8_t* const*)(node + 0x1C);
                    if (!Readable_(item, 0x1C)) return false;
                    if (*(const int32_t*)(item + 0x14) == 32 && !Add(item)) return false;
                    for (int offset : {0, 8}) {
                        const uint8_t* child = *(const uint8_t* const*)(node + offset);
                        if (child == nil) continue;
                        if (child == head || !Readable_(child, 0x24)
                            || *(const uint8_t* const*)(child + 4) != node) return false;
                        pending.push_back(child);
                    }
                }
                if (visited.size() != count) return false;
            }
        }
        for (int side = 0; side < 2; ++side)
            for (int slot = 0; slot < 21; ++slot)
                for (int sound = 0; sound < 8; ++sound)
                    if (!Add(*(uint8_t**)((uint8_t*)&mgr->stacks[side][slot] + 0x170 + sound * 4))) return false;
        return true;
    }
    bool Capture(H3CombatManager* mgr) {
        __try { return CaptureImpl(mgr); }
        __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { return false; }
    }
    static bool RestoreSeh_(const Entry* entry) {
        __try { memcpy(entry->resource + 0x28, entry->parameters, 12); }
        __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { return false; }
        return true;
    }
    bool RestoreTextSeh_() {
        __try { memcpy(kObjectTextBuffer_, text, sizeof(text)); }
        __except (GuardCrashFilter_(kObjectGuard_, GetExceptionInformation())) { return false; }
        return true;
    }
    ~ObjectSoundGuard_() {
        if (g_restoreFatal) return;
        for (const auto& entry : entries)
            if (!RestoreSeh_(&entry)) { g_restoreFatal = true; return; }
        if (textCaptured && !RestoreTextSeh_()) g_restoreFatal = true;
    }
};

struct RestoreObjects_
{
    struct Slot {
        std::unique_ptr<uint8_t[]> bytes;
        bool constructed = false;
        bool replace = false;
    } slots[2][21];
    uint32_t sets[2][4] = {};
    bool setConstructed[2] = {};
    bool switched = false;
    ~RestoreObjects_() {
        if (!g_restoreFatal) Release(nullptr);
        if (g_restoreFatal)
            for (int side = 0; side < 2; ++side)
                for (int slot = 0; slot < 21; ++slot) slots[side][slot].bytes.release();
    }
    bool Fault(const char* why, std::string* error) {
        g_restoreFatal = true;
        if (error) *error = why;
        return false;
    }
    void* Allocate(unsigned bytes, std::string* error) {
        bool fault = false;
        void* memory = ObjectAllocateSeh_(bytes, &fault);
        if (fault) Fault("native object allocator fault", error);
        else if (!memory && error) *error = "object preallocation failed";
        if (memory) memset(memory, 0, bytes);
        return memory;
    }
    bool PrepareDeque(uint8_t* stack, const std::vector<int32_t>& values, std::string* error) {
        if (values.empty()) return true;
        // Shift begin for exact multiples: every block has values, end stays inside
        // the final block, and native pop_front frees every allocation.
        const unsigned beginOffset = values.size() % 1024 == 0 ? 1 : 0;
        const unsigned blocks = ((unsigned)values.size() + beginOffset + 1023) / 1024;
        uint32_t** map = (uint32_t**)Allocate((blocks + 2) * sizeof(void*), error);
        if (!map) return false;
        for (unsigned block = 0; block < blocks; ++block) {
            map[block + 1] = (uint32_t*)Allocate(4096, error);
            if (!map[block + 1]) {
                if (!g_restoreFatal) {
                    for (unsigned i = 0; i < block && !g_restoreFatal; ++i)
                        if (!ObjectFreeSeh_(map[i + 1])) Fault("deque allocation cleanup fault", error);
                    if (!g_restoreFatal && !ObjectFreeSeh_(map)) Fault("deque map cleanup fault", error);
                }
                return false;
            }
        }
        for (unsigned i = 0; i < values.size(); ++i)
            map[(i + beginOffset) / 1024 + 1][(i + beginOffset) % 1024] = (uint32_t)values[i];
        uint32_t* header = (uint32_t*)(stack + 0x420);
        header[1] = (uint32_t)map[1]; header[2] = header[1] + 4096;
        header[3] = header[1] + beginOffset * 4; header[4] = (uint32_t)(map + 1);
        header[5] = (uint32_t)map[blocks]; header[6] = header[5] + 4096;
        header[7] = header[5] + ((values.size() + beginOffset) % 1024) * 4;
        header[8] = (uint32_t)(map + blocks);
        header[9] = (uint32_t)map; header[10] = blocks + 2;
        header[11] = (uint32_t)values.size();
        return true;
    }
    bool PrepareRelations(uint8_t* stack, H3CombatManager* mgr, const CodecStack& source, std::string* error) {
        for (int v = 0; v < 4; ++v) {
            if (source.relations[v].empty()) continue;
            const unsigned n = (unsigned)source.relations[v].size();
            H3CombatCreature** refs = (H3CombatCreature**)Allocate(n * sizeof(void*), error);
            if (!refs) return false;
            uint32_t* header = (uint32_t*)(stack + 0x4F4 + v * 16);
            header[1] = (uint32_t)refs;
            header[2] = header[3] = header[1] + n * sizeof(void*);
            for (unsigned i = 0; i < n; ++i) {
                const CodecIdentity& id = source.relations[v][i];
                refs[i] = &mgr->stacks[id.side][id.slot];
            }
        }
        return true;
    }
    bool Prepare(H3CombatManager* mgr, const CodecCapture& before, const CodecCapture& target, std::string* error) {
        ObjectSoundGuard_ soundGuard;
        if (!soundGuard.Capture(mgr)) {
            if (error) *error = "无法备份当前共享声音播放参数";
            return false;
        }
        static_assert(sizeof(H3CombatCreature) == 0x548, "native slot layout");
        for (int side = 0; side < 2; ++side) {
            if (!ObjectSetConstructSeh_(sets[side])) return Fault("eagle-eye set constructor fault", error);
            setConstructed[side] = true;
            for (int spell : target.eagleEye[side])
                if (!ObjectSetInsertSeh_(sets[side], spell)) return Fault("eagle-eye set insertion fault", error);
            std::vector<int32_t> check;
            if (!ReadSpellSet_((const uint8_t*)sets[side], &check) || check != target.eagleEye[side]) {
                if (error) *error = "prepared eagle-eye set mismatch";
                return false;
            }
            for (int slot = 0; slot < 21; ++slot) {
                Slot& prepared = slots[side][slot];
                const CodecStack& saved = target.stacks[side][slot];
                const CodecStack& old = before.stacks[side][slot];
                // The reserved slot owns containers, but is not a renderable combatant.
                prepared.replace = slot < 20 && (saved.occupied != old.occupied
                    || (saved.occupied && saved.type != old.type));
                prepared.bytes.reset(new uint8_t[sizeof(H3CombatCreature)]{});
                H3CombatCreature* stack = (H3CombatCreature*)prepared.bytes.get();
                if (!ObjectStackConstructSeh_(stack, prepared.replace ? &saved : nullptr, mgr->hero[side]))
                    return Fault("native stack preparation fault", error);
                prepared.constructed = true;
                if (!PrepareDeque(prepared.bytes.get(), saved.spellIds, error)
                    || !PrepareRelations(prepared.bytes.get(), mgr, saved, error)) return false;
                // 2026-10-10 用户裁定：DEF 检查精简为最小集（指针非空+对象头可读，
                // 2026-10-06 43DEDA 空指针直送渲染前案防线），组号/帧号信任游戏自洽。
                // 145~149 机器/箭塔豁免恢复（2026-10-10 17:43 玩家日志实证：攻城战
                // 箭塔槽 stack->def==0 是正常状态，其 DEF 挂在攻城塔记录上）。
                if (slot < 20 && saved.occupied && CodecDefFrameGateApplies_(saved.type)) {
                    H3LoadedDef* def = prepared.replace ? stack->def : mgr->stacks[side][slot].def;
                    if (!RestoreCreatureDefReady_(def, saved)) {
                        // 2026-10-07: an ammo-cart slot was rejected with no slot
                        // evidence; name the failing stack for future diagnosis.
                        LogDebug("[Objects op=%ld] saved def gate side=%d slot=%d type=%d anim=%d/%d def=%p groups=%d replace=%d",
                            g_diag.id, side, slot, saved.type, saved.animation,
                            saved.animationFrame, (const void*)def,
                            Readable_(def, sizeof(*def)) ? (int)def->groupsCount : -1,
                            prepared.replace ? 1 : 0);
                        if (error) *error = "saved creature DEF unavailable";
                        return false;
                    }
                }
                if (slot < 20 && old.occupied && CodecDefFrameGateApplies_(old.type)
                    && !RestoreCreatureDefReady_(mgr->stacks[side][slot].def, old)) {
                    LogDebug("[Objects op=%ld] rollback def gate side=%d slot=%d type=%d anim=%d/%d def=%p groups=%d",
                        g_diag.id, side, slot, old.type, old.animation,
                        old.animationFrame, (const void*)mgr->stacks[side][slot].def,
                        Readable_(mgr->stacks[side][slot].def, sizeof(H3LoadedDef))
                            ? (int)mgr->stacks[side][slot].def->groupsCount : -1);
                    if (error) *error = "rollback creature DEF unavailable";
                    return false;
                }
            }
        }
        return true;
    }
    void Switch(H3CombatManager* mgr) {
        for (int side = 0; side < 2; ++side) {
            ObjectSwapBytes_((uint8_t*)mgr + 0x545C + side * 16, sets[side], 16);
            for (int slot = 0; slot < 21; ++slot) {
                Slot& item = slots[side][slot];
                uint8_t* live = (uint8_t*)&mgr->stacks[side][slot];
                if (item.replace) ObjectSwapBytes_(live, item.bytes.get(), sizeof(H3CombatCreature));
                else {
                    ObjectSwapBytes_(live + 0x420, item.bytes.get() + 0x420, 48);
                    ObjectSwapBytes_(live + 0x4F4, item.bytes.get() + 0x4F4, 64);
                }
            }
        }
        switched = !switched;
    }
    bool Release(std::string* error) {
        if (g_restoreFatal) return false;
        for (int side = 0; side < 2; ++side) {
            for (int slot = 0; slot < 21; ++slot) {
                Slot& item = slots[side][slot];
                if (item.constructed) {
                    if (!ObjectStackDestroySeh_((H3CombatCreature*)item.bytes.get()))
                        return Fault("native stack release fault", error);
                    item.constructed = false;
                }
            }
            if (setConstructed[side]) {
                if (!ObjectSetDestroySeh_(sets[side])) return Fault("eagle-eye set release fault", error);
                setConstructed[side] = false;
            }
        }
        return true;
    }
};
