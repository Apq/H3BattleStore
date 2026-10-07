// Exercise production callbacks with the deployed patcher, never Heroes3.
// Match the production /W3 include level; keep test code at the runner's /W4.
#pragma warning(push, 3)
#define H3BATTLE_OBSTACLE_TEST_BACKEND
#define DllMain UnusedBattleStoreDllMainForAbiTest
#include "../H3BattleStore.cpp"
#undef DllMain
#pragma warning(pop)
#pragma comment(lib, "user32.lib")

#include <climits>
#include <cstdio>
#include <cstdlib>

static_assert(sizeof(void*) == 4, "Hook ABI tests require x86");
static void CheckAbi_(bool ok, const char* message)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL Hook ABI: %s\n", message);
        std::exit(1);
    }
}

// Production callbacks above keep /O2; local originals and callers must not
// acquire register-preservation assumptions from their compiler-known bodies.
#pragma optimize("", off)
static int test_calls_ = 0, test_parameter_ = 0, test_depth_ = 0;
static H3CombatManager* test_self_ = nullptr;
static H3Msg* test_message_ = nullptr;
static bool test_throw_ = false;
static bool test_startFreeze_ = false, test_startSawFrozen_ = false;
static std::string test_startKey_;
static constexpr DWORD kOriginalException_ = 0xE0424242;
static int ExpectedResult_(H3CombatManager* self, int parameter)
{
    return static_cast<int>((reinterpret_cast<UINT32>(self)
        ^ static_cast<UINT32>(parameter) ^ 0x13579BDFu) & 0x7FFFFFFFu);
}
static __declspec(noinline) int __fastcall MockExecute_(H3CombatManager* self, int, int parameter)
{
    ++test_calls_;
    test_self_ = self;
    test_parameter_ = parameter;
    test_depth_ = g_executorDepth;
    if (test_throw_) RaiseException(kOriginalException_, 0, 0, nullptr);
    return ExpectedResult_(self, parameter);
}
static __declspec(noinline) int __fastcall MockStart_(H3CombatManager* self, int, int parameter)
{
    ++test_calls_;
    test_self_ = self;
    test_parameter_ = parameter;
    if (test_startFreeze_) {
        std::string key;
        test_startSawFrozen_ = BattleFingerprint_(self, &key, nullptr) && key == test_startKey_;
        self->army[0]->type[0] = 99; self->army[0]->count[0] = 999;
        self->stacks[0][0].type = 99; self->stacks[0][0].numberAtStart = 999;
        self->heroOwner[0] = 7; self->landType = 8; self->absoluteObstacleId = 42;
    }
    return ExpectedResult_(self, parameter);
}
static __declspec(noinline) void __fastcall MockStop_(H3CombatManager* self)
{
    ++test_calls_;
    test_self_ = self;
}
static __declspec(noinline) void __fastcall MockCycle_(H3CombatManager* self)
{
    ++test_calls_;
    test_self_ = self;
}
static __declspec(noinline) int __fastcall MockMessage_(H3CombatManager* self, int, H3Msg* message)
{
    ++test_calls_;
    test_self_ = self;
    test_message_ = message;
    test_depth_ = g_messageDepth;
    if (test_throw_) RaiseException(kOriginalException_, 0, 0, nullptr);
    return ExpectedResult_(self, -17);
}
static __declspec(noinline) int CallExecute_(H3CombatManager* self, int parameter)
{
    unsigned before = 0, after = 0;
    __asm { mov before, esp }
    const int result = THISCALL_2(int, &MockExecute_, self, parameter);
    __asm { mov after, esp }
    CheckAbi_(before == after, "executor ESP");
    return result;
}
static bool CatchOriginalException_(H3CombatManager* self, int parameter)
{
    bool caught = false;
    unsigned before = 0, after = 0;
    __asm { mov before, esp }
    __try { CallExecute_(self, parameter); }
    __except (GetExceptionCode() == kOriginalException_
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { caught = true; }
    __asm { mov after, esp }
    CheckAbi_(before == after, "executor exception ESP");
    return caught;
}
static __declspec(noinline) int CallMessage_(H3CombatManager* self, H3Msg* message)
{
    unsigned before = 0, after = 0;
    __asm { mov before, esp }
    const int result = THISCALL_2(int, &MockMessage_, self, message);
    __asm { mov after, esp }
    CheckAbi_(before == after, "message ESP");
    return result;
}
static bool CatchMessageException_(H3CombatManager* self, H3Msg* message)
{
    bool caught = false;
    unsigned before = 0, after = 0;
    __asm { mov before, esp }
    __try { CallMessage_(self, message); }
    __except (GetExceptionCode() == kOriginalException_
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { caught = true; }
    __asm { mov after, esp }
    CheckAbi_(before == after, "message exception ESP");
    return caught;
}
static void TestExecutor_(HiHook* hook)
{
    int storage = 0;
    H3CombatManager* self = reinterpret_cast<H3CombatManager*>(&storage);
    test_calls_ = 0;
    g_executorDepth = 0;
    const int bridge = THISCALL_2(int, hook->GetDefaultFunc(), self, -1234567);
    CheckAbi_(bridge == ExpectedResult_(self, -1234567), "executor original bridge return");
    CheckAbi_(test_calls_ == 1 && test_self_ == self && test_parameter_ == -1234567,
        "executor original bridge arguments/once");
    CheckAbi_(g_executorDepth == 0 && test_depth_ == 0, "original bridge bypasses callback");
    test_calls_ = 0;
    const int parameters[] = { 0, 1, -1, 0x12345678, INT_MIN, INT_MAX };
    for (int i = 0; i < 1000; ++i) {
        const int parameter = parameters[i % _countof(parameters)];
        H3CombatManager* argument = (i & 1) ? self : nullptr;
        CheckAbi_(CallExecute_(argument, parameter) == ExpectedResult_(argument, parameter),
            "executor entry return");
        CheckAbi_(test_self_ == argument && test_parameter_ == parameter,
            "executor preserves ECX and single explicit int");
        CheckAbi_(test_calls_ == i + 1, "executor default exactly once per entry");
        CheckAbi_(test_depth_ == 1 && g_executorDepth == 0, "executor normal finally depth");
    }
    test_calls_ = 0;
    test_throw_ = true;
    CheckAbi_(CatchOriginalException_(self, INT_MIN), "original SEH propagates unchanged");
    CheckAbi_(test_calls_ == 1 && test_self_ == self && test_parameter_ == INT_MIN,
        "throwing executor default once with original arguments");
    CheckAbi_(test_depth_ == 1 && g_executorDepth == 0, "executor exception finally depth zero");
    test_throw_ = false;
    CheckAbi_(CallExecute_(self, INT_MAX) == ExpectedResult_(self, INT_MAX)
        && test_calls_ == 2 && g_executorDepth == 0, "executor usable after original exception");
    std::printf("PASS executor: bridge, 1000 args/results/ESP/once, original SEH, finally depth zero\n");
}
static void TestLifecycle_()
{
    test_calls_ = 0;
    for (int i = 0; i < 1000; ++i) {
        unsigned before = 0, after = 0;
        const unsigned generation = g_battleGeneration;
        __asm { mov before, esp }
        const int result = THISCALL_2(int, &MockStart_, nullptr, -i);
        __asm { mov after, esp }
        CheckAbi_(before == after && result == ExpectedResult_(nullptr, -i), "start result/ESP");
        CheckAbi_(test_self_ == nullptr && test_parameter_ == -i && test_calls_ == i + 1,
            "start original arguments/once");
        CheckAbi_(g_battleGeneration == generation + 1 && !g_battleInitialized
            && g_battleThread == GetCurrentThreadId(), "start production reset");
    }
    {
        std::unique_ptr<uint8_t[]> manager(new uint8_t[sizeof(H3CombatManager)]{});
        std::unique_ptr<uint8_t[]> armies[2];
        H3CombatManager* mgr = reinterpret_cast<H3CombatManager*>(manager.get());
        for (int side = 0; side < 2; ++side) {
            armies[side].reset(new uint8_t[sizeof(H3Army)]{});
            mgr->army[side] = reinterpret_cast<H3Army*>(armies[side].get());
            for (int slot = 0; slot < 7; ++slot) mgr->army[side]->type[slot] = -1;
            mgr->army[side]->type[0] = 45 + side; mgr->army[side]->count[0] = 10;
        }
        CheckAbi_(BattleInitialFingerprint_(mgr, &test_startKey_, nullptr), "start fixture initial fingerprint");
        test_startFreeze_ = true; test_startSawFrozen_ = false; test_calls_ = 0;
        unsigned before = 0, after = 0;
        __asm { mov before, esp }
        const int result = THISCALL_2(int, &MockStart_, mgr, -77);
        __asm { mov after, esp }
        test_startFreeze_ = false;
        std::string frozen;
        CheckAbi_(test_startSawFrozen_ && test_calls_ == 1 && before == after
            && result == ExpectedResult_(mgr, -77), "production start freezes key before original call with ABI intact");
        CheckAbi_(BattleFingerprint_(mgr, &frozen, nullptr) && frozen == test_startKey_,
            "original initialization mutations preserve frozen fingerprint");
        BattleReset_();
        CheckAbi_(!BattleFingerprint_(mgr, &frozen, nullptr), "lifecycle reset clears frozen fingerprint");
    }
    test_calls_ = 0;
    int storage = 0;
    H3CombatManager* self = reinterpret_cast<H3CombatManager*>(&storage);
    for (int i = 0; i < 1000; ++i) {
        unsigned before = 0, after = 0;
        const unsigned generation = g_battleGeneration;
        __asm { mov before, esp }
        THISCALL_1(void, &MockStop_, self);
        __asm { mov after, esp }
        CheckAbi_(before == after && test_self_ == self && test_calls_ == i + 1,
            "stop ECX/ESP/default once");
        CheckAbi_(g_battleGeneration == generation + 1 && !g_battleInitialized,
            "stop production reset");
    }
    std::printf("PASS lifecycle: 1000 start args/results/ESP, 1000 stop ECX/ESP, defaults once/reset\n");
}
static void TestCycleAndMessage_()
{
    g_uiWaitSaveUntil = 0;
    test_calls_ = 0;
    for (int i = 0; i < 1000; ++i) {
        unsigned before = 0, after = 0;
        __asm { mov before, esp }
        THISCALL_1(void, &MockCycle_, nullptr);
        __asm { mov after, esp }
        CheckAbi_(before == after && test_self_ == nullptr && test_calls_ == i + 1,
            "cycle null ECX/ESP/default once");
    }
    // BattleMainDialog_ reads the absolute game window-manager slot even for
    // null mgr. Busy short-circuits that path; fatal must stay false on return.
    g_restoreBusy = true;
    g_restoreFatal = false;
    g_messageDepth = 0;
    test_calls_ = 0;
    int storage = 0;
    H3CombatManager* self = reinterpret_cast<H3CombatManager*>(&storage);
    for (int i = 0; i < 1000; ++i) {
        H3CombatManager* argument = (i & 1) ? self : nullptr;
        const int result = CallMessage_(argument, nullptr);
        CheckAbi_(result == ExpectedResult_(argument, -17), "message result");
        CheckAbi_(test_self_ == argument && test_message_ == nullptr && test_calls_ == i + 1,
            "message original arguments/default once");
        CheckAbi_(test_depth_ == 1 && g_messageDepth == 0, "message normal depth balance");
    }
    H3Msg message = {};
    test_calls_ = 0;
    test_throw_ = true;
    for (int i = 0; i < 2; ++i) {
        H3CombatManager* argument = i ? self : nullptr;
        H3Msg* messageArgument = i ? &message : nullptr;
        CheckAbi_(CatchMessageException_(argument, messageArgument),
            "message original SEH propagates unchanged");
        CheckAbi_(test_calls_ == i + 1 && test_self_ == argument && test_message_ == messageArgument,
            "throwing message default once with original arguments");
        CheckAbi_(test_depth_ == 1 && g_messageDepth == 0,
            "message exception finally depth zero");
    }
    test_throw_ = false;
    CheckAbi_(CallMessage_(self, &message) == ExpectedResult_(self, -17)
        && test_calls_ == 3 && test_self_ == self && test_message_ == &message
        && test_depth_ == 1 && g_messageDepth == 0,
        "message usable after original exception");
    g_restoreBusy = false;
    std::printf("PASS cycle/message: 1000 null cycles and 1000 busy/null-message args/results/ESP/once\n");
    std::printf("PASS message: null/non-null original SEH/ESP/once, finally depth zero, usable after SEH\n");
    std::printf("LIMIT: no game UI, non-null cycle manager, full restore transaction or HD hook chain tested\n");
}
static void TestRestoreFrames_()
{
    std::unique_ptr<uint8_t[]> storage(new uint8_t[sizeof(H3CombatManager)]{});
    H3CombatManager* mgr = reinterpret_cast<H3CombatManager*>(storage.get());
    std::unique_ptr<CodecCapture> capture(new CodecCapture{});
    memset(mgr->redrawCreatureFrame, 1, sizeof(mgr->redrawCreatureFrame));
    mgr->heroAnimation[0] = 7;
    mgr->heroAnimation[1] = 8;
    CheckAbi_(RestoreMarkCreatureFrames_(mgr, *capture) == 0, "empty slots clear stale redraw bits");
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 20; ++slot)
            CheckAbi_(!mgr->redrawCreatureFrame[side][slot], "empty slot never reaches NULL DEF draw");
    for (int side = 0; side < 2; ++side) {
        CodecStack& live = capture->stacks[side][0];
        live.occupied = true; live.type = 45; live.position = 22; live.numberAlive = 10;
        CodecStack& corpse = capture->stacks[side][19];
        corpse.occupied = true; corpse.type = 45; corpse.position = 186; corpse.numberAlive = 0;
        capture->stacks[side][20] = live;
    }
    CheckAbi_(RestoreMarkCreatureFrames_(mgr, *capture) == 4, "both sides include live and corpse slot19, not slot20");
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 20; ++slot)
            CheckAbi_(!!mgr->redrawCreatureFrame[side][slot] == (slot == 0 || slot == 19),
                "mask uses 20-byte side stride, capture uses 21-object stride");
    CheckAbi_(mgr->heroAnimation[0] == 7 && mgr->heroAnimation[1] == 8,
        "mask never overwrites adjacent hero animation flags");
    capture->stacks[0][0].position = -1;
    capture->stacks[0][19].position = 187;
    capture->stacks[1][0].type = -1;
    capture->stacks[1][19].type = 149;
    CheckAbi_(RestoreMarkCreatureFrames_(mgr, *capture) == 0, "offboard/empty type/tower never force redraw");

    // Game resources dereference their native vtable on destruction.
    std::unique_ptr<uint8_t[]> defStorage(new uint8_t[sizeof(H3LoadedDef)]{});
    H3LoadedDef* def = reinterpret_cast<H3LoadedDef*>(defStorage.get());
    H3LoadedDef::DefGroup group = {};
    H3LoadedDef::DefGroup* groups[] = {&group};
    int active[] = {1};
    std::unique_ptr<uint8_t[]> frameStorage(new uint8_t[sizeof(H3DefFrame)]{});
    H3DefFrame* frames[] = {reinterpret_cast<H3DefFrame*>(frameStorage.get())};
    group.count = 1; group.frames = frames;
    def->groupsCount = 1; def->groups = groups; def->activeGroups = active;
    CodecStack stack = {};
    CheckAbi_(!RestoreCreatureDefReady_(nullptr, stack), "NULL DEF rejected before any game write");
    CheckAbi_(RestoreCreatureDefReady_(def, stack), "readable selected frame and base divisor accepted");
    group.count = 0;
    CheckAbi_(!RestoreCreatureDefReady_(def, stack), "zero base divisor rejected before write");
    group.count = 1; active[0] = 0;
    CheckAbi_(!RestoreCreatureDefReady_(def, stack), "inactive base group rejected before write");
    active[0] = 1; frames[0] = nullptr;
    CheckAbi_(!RestoreCreatureDefReady_(def, stack), "NULL selected frame rejected before write");
    std::printf("PASS restore render: production 2x20 mask, empty/live/corpse/reserved/position/type and DEF gates\n");
}

// v5 指纹增补：双方英雄身份/19 个穿戴槽宝物 id、城镇身份参与计算；
// 战斗中会变的字段（存活数等）不得影响指纹。原始字节存储，绝不构造
// 带 H3ResourceItem 析构的游戏对象。
static void TestFingerprint_()
{
    std::unique_ptr<uint8_t[]> mgrStorage(new uint8_t[sizeof(H3CombatManager)]{});
    H3CombatManager* mgr = reinterpret_cast<H3CombatManager*>(mgrStorage.get());
    std::unique_ptr<uint8_t[]> hero0Storage(new uint8_t[sizeof(H3Hero)]{});
    std::unique_ptr<uint8_t[]> hero1Storage(new uint8_t[sizeof(H3Hero)]{});
    std::unique_ptr<uint8_t[]> townStorage(new uint8_t[sizeof(H3Town)]{});
    std::unique_ptr<uint8_t[]> armyStorage[2];
    for (int side = 0; side < 2; ++side) {
        armyStorage[side].reset(new uint8_t[sizeof(H3Army)]{});
        mgr->army[side] = reinterpret_cast<H3Army*>(armyStorage[side].get());
        for (int slot = 0; slot < 7; ++slot) {
            mgr->army[side]->type[slot] = slot ? -1 : 45 + side;
            mgr->army[side]->count[slot] = slot ? 0 : 10 + side;
        }
    }
    H3Hero* hero0 = reinterpret_cast<H3Hero*>(hero0Storage.get());
    H3Hero* hero1 = reinterpret_cast<H3Hero*>(hero1Storage.get());
    H3Town* town = reinterpret_cast<H3Town*>(townStorage.get());
    mgr->hero[0] = hero0; mgr->hero[1] = hero1; mgr->town = town;
    hero0->id = 12; hero0->experience = 1000; hero0->level = 5;
    hero0->bodyArtifacts[0].id = 7; hero0->bodyArtifacts[18].id = 130;
    hero1->id = 13; hero1->experience = 2000; hero1->level = 6;
    town->number = 9; town->type = 2; town->owner = 3;
    std::string first, again;
    CheckAbi_(BattleInitialFingerprint_(mgr, &first, nullptr), "fingerprint computes on fake manager");
    CheckAbi_(first.size() == 64, "fingerprint is 64 hex chars");
    CheckAbi_(BattleInitialFingerprint_(mgr, &again, nullptr) && first == again, "stable across recomputation");
    hero1->bodyArtifacts[3].id = 55;
    CheckAbi_(BattleInitialFingerprint_(mgr, &again, nullptr) && first != again, "worn artifact id changes key");
    hero1->bodyArtifacts[3].id = 0;
    hero1->level = 7;
    CheckAbi_(BattleInitialFingerprint_(mgr, &again, nullptr) && first != again, "hero level changes key");
    hero1->level = 6;
    hero1->id = 99;
    CheckAbi_(BattleInitialFingerprint_(mgr, &again, nullptr) && first != again, "hero id changes key");
    hero1->id = 13;
    town->number = 10;
    CheckAbi_(BattleInitialFingerprint_(mgr, &again, nullptr) && first != again, "town identity changes key");
    town->number = 9;
    mgr->hero[1] = nullptr;
    CheckAbi_(BattleInitialFingerprint_(mgr, &again, nullptr) && first != again, "hero absence changes key");
    mgr->hero[1] = hero1;
    mgr->army[1]->count[0] += 1;
    CheckAbi_(BattleInitialFingerprint_(mgr, &again, nullptr) && first != again, "prebattle army count changes initial key");
    mgr->army[1]->count[0] -= 1;
    mgr->army[0]->type[0] += 1;
    CheckAbi_(BattleInitialFingerprint_(mgr, &again, nullptr) && first != again, "prebattle army type changes initial key");
    mgr->army[0]->type[0] -= 1;
    mgr->army[1] = nullptr;
    CheckAbi_(!BattleInitialFingerprint_(mgr, &again, nullptr), "missing prebattle army rejected");
    mgr->army[1] = reinterpret_cast<H3Army*>(armyStorage[1].get());
    g_preBattleKey_.clear(); g_preBattleManager_ = nullptr;
    std::string error;
    CheckAbi_(!BattleFingerprint_(mgr, &again, &error) && !error.empty(), "no frozen cache rejected");
    g_preBattleKey_ = first; g_preBattleManager_ = mgr;
    mgr->stacks[0][0].type = 99; mgr->stacks[0][0].numberAtStart = 123;
    mgr->stacks[0][0].numberAlive = 9; mgr->heroMonCount[0] = 19;
    mgr->army[0]->type[0] = 99; mgr->army[0]->count[0] = 123;
    mgr->heroOwner[0] = 7; mgr->landType = 8; mgr->specialTerrain = 9;
    mgr->absoluteObstacleId = 42; mgr->squares[22].obstacleBits = 4;
    hero0->id = 77; hero1->bodyArtifacts[3].id = 55; town->owner = 6;
    CheckAbi_(BattleFingerprint_(mgr, &again, nullptr) && first == again,
        "frozen key ignores combat topology/counts/army/owner/terrain/obstacles/hero mutations");
    CheckAbi_(!BattleFingerprint_(nullptr, &again, nullptr)
        && !BattleFingerprint_(mgr, nullptr, nullptr), "cache requires same manager and output");
    g_preBattleKey_.clear();
    CheckAbi_(!BattleFingerprint_(mgr, &again, nullptr), "empty key rejected even with matching manager");
    g_preBattleManager_ = nullptr;
    std::printf("PASS fingerprint: initial army/hero/town differences, frozen cache mutation immunity, missing cache rejection\n");
}

static void TestObstacles_()
{
    uint16_t kind = 0;
    for (uint16_t i = 0; i <= 90; ++i)
        CheckAbi_(ObstacleKindOf_(ObstacleInfoFor_(i), &kind) && kind == i, "terrain kind address roundtrip");
    for (uint16_t i = 100; i <= 104; ++i)
        CheckAbi_(ObstacleKindOf_(ObstacleInfoFor_(i), &kind) && kind == i, "spell kind address roundtrip");
    CheckAbi_(!ObstacleKindOf_(reinterpret_cast<H3ObstacleInfo*>(0x63C7C9), &kind)
        && !ObstacleInfoFor_(91) && !ObstacleInfoFor_(99) && !ObstacleInfoFor_(105), "misaligned and unknown kind rejected");
    CheckAbi_(ObstacleCellHex_(51, -16) == 34 && ObstacleCellHex_(34, -16) == 18
        && ObstacleCellHex_(51, 0) == 51, "exact odd-row to even-row correction");
    CheckAbi_(ObstacleKindBits_(100) == 4 && ObstacleKindBits_(101) == 8
        && ObstacleKindBits_(102) == 0x22 && ObstacleKindBits_(103) == 0x22
        && ObstacleKindBits_(104) == 0x10 && ObstacleKindBits_(90) == 2, "kind bits map");
    char name[16];
    CheckAbi_(ObstacleName_("C17SPE1.DEF", name) && strcmp(name, "C17SPE1.DEF") == 0
        && !ObstacleName_("", name) && !ObstacleName_("0123456789abcdef", name), "bounded DEF name read");
    std::unique_ptr<uint8_t[]> mgrStorage(new uint8_t[sizeof(H3CombatManager)]{});
    H3CombatManager* mgr = reinterpret_cast<H3CombatManager*>(mgrStorage.get());
    std::unique_ptr<uint8_t[]> entriesStorage(new uint8_t[3 * sizeof(H3Obstacle)]{});
    H3Obstacle* entries = reinterpret_cast<H3Obstacle*>(entriesStorage.get());
    uintptr_t* header = reinterpret_cast<uintptr_t*>(&mgr->obstacleInfo);
    header[1] = reinterpret_cast<uintptr_t>(entries);
    header[2] = reinterpret_cast<uintptr_t>(entries + 2);
    header[3] = reinterpret_cast<uintptr_t>(entries + 3);
    UINT count = 0;
    CheckAbi_(ObstacleVectorReady_(mgr->obstacleInfo, true, &count) && count == 2, "raw vector writable header and range");
    header[2] += 1;
    CheckAbi_(!ObstacleVectorReady_(mgr->obstacleInfo, false, &count), "misaligned raw vector rejected before Count");
    header[2] -= 1;
    header[3] = header[1];
    CheckAbi_(!ObstacleVectorReady_(mgr->obstacleInfo, true, &count), "capacity below end rejected");
    std::vector<RestoreObstacleKey_> keys;
    keys.push_back({100, 22, 1}); keys.push_back({101, 40, 7});
    CheckAbi_(RestoreObstacleFind_(keys, 101, 40)->index == 7
        && !RestoreObstacleFind_(keys, 101, 22), "diff key is kind plus anchor not vector order");
    std::unique_ptr<CodecCapture> capture(new CodecCapture{});
    CodecObstacle item = {}; item.kindId = 101; item.anchorHex = 40;
    capture->obstacles.push_back(item);
    CheckAbi_(RestoreObstacleSaved_(*capture, 101, 40) != nullptr
        && RestoreObstacleSaved_(*capture, 100, 22) == nullptr, "diff saved membership");
    std::printf("PASS obstacles v4: kind/static map, parity, bits, bounded names, raw vector and diff keys; no game calls\n");
}

// Raw storage only: none of these fixtures run a native resource/vector destructor.
static void EmptySpellSets_(H3CombatManager* mgr, uint32_t (&heads)[2][5])
{
    for (int side = 0; side < 2; ++side) {
        uint32_t* header = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mgr) + 0x545C + side * 16);
        memset(header, 0, 16);
        header[1] = reinterpret_cast<uint32_t>(heads[side]);
    }
}
struct ObstacleFixture_;
static ObstacleFixture_* obstacleFixture_ = nullptr;
static void __fastcall MockObstacleDeref_(H3LoadedDef*, int);
static void __fastcall MockObstacleRemove_(H3CombatManager*, int, unsigned int);
static int __fastcall MockObstacleInsert_(void*, int, H3Obstacle*, unsigned int, const H3Obstacle*);
static void __fastcall MockObstaclePlace_(H3CombatManager*, int, H3Obstacle*, int, int, unsigned int);
static H3LoadedDef* __fastcall MockObstacleLoad_(const char*, int);

struct ObstacleFixture_
{
    std::unique_ptr<uint8_t[]> manager{new uint8_t[sizeof(H3CombatManager)]{}};
    std::unique_ptr<uint8_t[]> entries{new uint8_t[4096 * sizeof(H3Obstacle)]{}};
    std::unique_ptr<uint8_t[]> defs[3];
    std::unique_ptr<uint8_t[]> frames[3];
    H3LoadedDef::DefGroup groups[3] = {};
    H3LoadedDef::DefGroup* groupPointers[3] = {};
    H3DefFrame* framePointers[3] = {};
    H3ObstacleInfo infos[3] = {};
    const H3ObstacleInfo* map[105] = {};
    void* vtable[2] = {};
    uint32_t emptySetHeads[2][5] = {};
    int loads = 0, removes = 0, inserts = 0, places = 0, derefs = 0;
    int failLoad = 0;
    bool noLoads = false, removeFault = false, insertFault = false, placeFault = false;
    bool removeNoop = false, derefFault = false;
    ObstacleFixture_() {
        obstacleFixture_ = this;
        g_restoreFatal = false;
        EmptySpellSets_(Mgr(), emptySetHeads);
        vtable[1] = reinterpret_cast<void*>(&MockObstacleDeref_);
        const char* names[] = { "T0.DEF", "T1.DEF", "T2.DEF" };
        for (int i = 0; i < 3; ++i) {
            defs[i].reset(new uint8_t[sizeof(H3LoadedDef)]{});
            frames[i].reset(new uint8_t[sizeof(H3DefFrame)]{});
            framePointers[i] = reinterpret_cast<H3DefFrame*>(frames[i].get());
            groups[i].count = 1; groups[i].frames = &framePointers[i];
            groupPointers[i] = &groups[i];
            Def(i)->groupsCount = 1; Def(i)->groups = &groupPointers[i];
            *reinterpret_cast<void***>(Def(i)) = vtable;
            infos[i].defName = names[i]; infos[i].blockedCount = 1;
            infos[i].relativeCells[0] = 0;
            map[i] = &infos[i];
        }
        g_obstacleInfoMapTest_ = map;
        uintptr_t* header = reinterpret_cast<uintptr_t*>(&Mgr()->obstacleInfo);
        header[1] = reinterpret_cast<uintptr_t>(Items());
        header[2] = header[1]; header[3] = reinterpret_cast<uintptr_t>(Items() + 4096);
        kObstacleRemove = reinterpret_cast<ObstacleRemoveFn_>(&MockObstacleRemove_);
        kObstacleInsert = reinterpret_cast<ObstacleVectorInsertFn_>(&MockObstacleInsert_);
        kObstaclePlace = reinterpret_cast<ObstaclePlaceFn_>(&MockObstaclePlace_);
        kObstacleDefLoad = reinterpret_cast<ObstacleDefLoadFn_>(&MockObstacleLoad_);
    }
    ~ObstacleFixture_() { g_obstacleInfoMapTest_ = nullptr; obstacleFixture_ = nullptr; g_restoreFatal = false; }
    H3CombatManager* Mgr() { return reinterpret_cast<H3CombatManager*>(manager.get()); }
    H3Obstacle* Items() { return reinterpret_cast<H3Obstacle*>(entries.get()); }
    H3LoadedDef* Def(int i) { return reinterpret_cast<H3LoadedDef*>(defs[i].get()); }
    INT32& Refs(int i) { return *reinterpret_cast<INT32*>(defs[i].get() + 0x18); }
    void Count(UINT count) { reinterpret_cast<uintptr_t*>(&Mgr()->obstacleInfo)[2] = reinterpret_cast<uintptr_t>(Items() + count); }
    CodecObstacle Saved(uint16_t kind, uint8_t anchor) {
        CodecObstacle item = {}; item.kindId = kind; item.anchorHex = anchor;
        item.ownerSide = -1; item.cellCount = 1; item.cells[0] = anchor;
        strcpy_s(item.defName, infos[kind].defName);
        item.featureDuration = 1; item.animationIndex = 7;
        return item;
    }
    void Add(uint16_t kind, uint8_t anchor) {
        const UINT count = Mgr()->obstacleInfo.Count();
        H3Obstacle& item = Items()[count];
        item.def = Def(kind); item.info = &infos[kind]; item.anchorHex = anchor;
        item.ownerSide = -1; item.featureDuration = 1; item.animationIndex = 7;
        ++Refs(kind); Count(count + 1);
    }
};

static void __fastcall MockObstacleDeref_(H3LoadedDef* def, int)
{
    ObstacleFixture_& f = *obstacleFixture_;
    ++f.derefs;
    if (f.derefFault) RaiseException(kOriginalException_, 0, 0, nullptr);
    INT32* refs = reinterpret_cast<INT32*>(reinterpret_cast<uint8_t*>(def) + 0x18);
    CheckAbi_(*refs > 0, "no double DEF release");
    --*refs;
}
static void __fastcall MockObstacleRemove_(H3CombatManager* mgr, int, unsigned int index)
{
    ObstacleFixture_& f = *obstacleFixture_; ++f.removes;
    if (f.removeFault) RaiseException(kOriginalException_, 0, 0, nullptr);
    if (f.removeNoop) return;
    H3Obstacle& item = mgr->obstacleInfo.begin()[index];
    CheckAbi_(item.def != nullptr, "expiry never removes an expired tombstone twice");
    MockObstacleDeref_(item.def, 0); item.def = nullptr;
    mgr->squares[item.anchorHex].obstacleBits = 0;
    mgr->squares[item.anchorHex].obstacleIndex = -1;
}
static int __fastcall MockObstacleInsert_(void* vector, int, H3Obstacle* end,
    unsigned int count, const H3Obstacle* item)
{
    ObstacleFixture_& f = *obstacleFixture_; ++f.inserts;
    if (f.insertFault) RaiseException(kOriginalException_, 0, 0, nullptr);
    CheckAbi_(vector == &f.Mgr()->obstacleInfo && count == 1 && end == f.Items() + f.Mgr()->obstacleInfo.Count(),
        "production insert receives real vector/end/count");
    *end = *item;
    f.Count(f.Mgr()->obstacleInfo.Count() + 1);
    return 1;
}
static void __fastcall MockObstaclePlace_(H3CombatManager* mgr, int, H3Obstacle* entry,
    int index, int anchor, unsigned int bits)
{
    ObstacleFixture_& f = *obstacleFixture_; ++f.places;
    if (f.placeFault) RaiseException(kOriginalException_, 0, 0, nullptr);
    CheckAbi_(entry == f.Items() + index && entry->anchorHex == anchor, "production place index/anchor");
    mgr->squares[anchor].obstacleBits = static_cast<UINT8>(mgr->squares[anchor].obstacleBits | bits);
    mgr->squares[anchor].obstacleIndex = index;
}
static H3LoadedDef* __fastcall MockObstacleLoad_(const char* name, int)
{
    ObstacleFixture_& f = *obstacleFixture_; ++f.loads;
    CheckAbi_(!f.noLoads, "rollback must not call loader");
    if (f.failLoad == f.loads) return nullptr;
    for (int i = 0; i < 3; ++i) if (strcmp(name, f.infos[i].defName) == 0) {
        ++f.Refs(i); return f.Def(i);
    }
    CheckAbi_(false, "loader name must come from matched static info");
    return nullptr;
}
static void ObstacleTarget_(ObstacleFixture_& f, CodecCapture* capture, uint16_t kind, uint8_t anchor)
{
    capture->obstacles.push_back(f.Saved(kind, anchor));
    capture->squares[anchor].obstacleBits = static_cast<uint8_t>(ObstacleKindBits_(kind));
}
static void ObstacleExpiry_(ObstacleFixture_& f)
{
    // Faithful native expiry condition: deliberately no DEF check.
    for (UINT i = 0; i < f.Mgr()->obstacleInfo.Count(); ++i)
        if (f.Items()[i].featureDuration && --f.Items()[i].featureDuration == 0)
            MockObstacleRemove_(f.Mgr(), 0, i);
}
static void TestObstacleTransaction_()
{
    {
        ObstacleFixture_ f;
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        ObstacleTarget_(f, target.get(), 0, 22); ObstacleTarget_(f, target.get(), 0, 40);
        std::string error;
        RestoreObstacleResources_ resources;
        CheckAbi_(RestoreObstaclePreflight_(f.Mgr(), *target, &error)
            && resources.Prepare(f.Mgr(), *before, *target, &error), "0->N preflight/prepare");
        CheckAbi_(f.loads == 1 && f.Refs(0) == 1 && f.Mgr()->obstacleInfo.Count() == 0, "same-kind preload once before first write");
        CheckAbi_(RestoreApply_(f.Mgr(), *target, resources, nullptr, &error), "0->N production apply");
        CheckAbi_(f.Refs(0) == 3 && f.inserts == 2 && f.places == 2
            && f.Mgr()->squares[40].obstacleIndex == 1, "same-kind multiple anchors own distinct refs and rebuilt squares");
        CheckAbi_(resources.Release(&error) && f.Refs(0) == 2, "success pool release balanced");
        ObstacleExpiry_(f);
        CheckAbi_(f.Refs(0) == 0, "entry lifecycle releases both donor refs");
    }
    {
        ObstacleFixture_ f; f.Add(0, 22); f.Add(0, 40);
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), empty(new CodecCapture{});
        ObstacleTarget_(f, before.get(), 0, 22); ObstacleTarget_(f, before.get(), 0, 40);
        // Existing dispel zombie must remain untouched during clean preflight.
        f.Count(3); f.Items()[2].featureDuration = 1; f.Items()[2].animationIndex = 9;
        std::string error; RestoreObstacleResources_ resources;
        CheckAbi_(RestoreObstaclePreflight_(f.Mgr(), *empty, &error) && f.Items()[2].featureDuration == 1,
            "preflight never cleans old dispel zombie");
        CheckAbi_(resources.Prepare(f.Mgr(), *before, *empty, &error) && f.Refs(0) == 3, "pin before last reference removal");
        CheckAbi_(RestoreApply_(f.Mgr(), *empty, resources, nullptr, &error) && f.Refs(0) == 1, "N->0 holds rollback pin");
        for (UINT i = 0; i < 3; ++i) CheckAbi_(f.Items()[i].featureDuration == 0
            && f.Items()[i].animationIndex == 0xFFFFFFFFu, "removed and existing zombie duration/animation cleared");
        ObstacleExpiry_(f); CheckAbi_(f.removes == 2, "next turn never repeats zombie remove");
        f.noLoads = true;
        CheckAbi_(RestoreApply_(f.Mgr(), *before, resources, nullptr, &error), "reverse restores solely from pinned DEF");
        CheckAbi_(resources.Release(&error) && f.Refs(0) == 2 && f.loads == 0
            && f.Mgr()->squares[22].obstacleIndex == 3 && f.Mgr()->squares[40].obstacleIndex == 4,
            "rollback refs balanced, tombstone indices retained, squares rebuilt");
        ObstacleExpiry_(f); CheckAbi_(f.Refs(0) == 0, "rollback entries release refs");
    }
    {
        ObstacleFixture_ f; f.Add(0, 22);
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        ObstacleTarget_(f, before.get(), 0, 22); ObstacleTarget_(f, target.get(), 1, 22);
        std::string error; RestoreObstacleResources_ resources;
        CheckAbi_(RestoreObstaclePreflight_(f.Mgr(), *target, &error)
            && resources.Prepare(f.Mgr(), *before, *target, &error), "type replacement prepare");
        CheckAbi_(RestoreApply_(f.Mgr(), *target, resources, nullptr, &error) && !f.Items()[0].def
            && f.Items()[1].info == &f.infos[1] && f.Refs(0) == 1 && f.Refs(1) == 2, "type replacement removes then appends");
        f.noLoads = true;
        CheckAbi_(RestoreApply_(f.Mgr(), *before, resources, nullptr, &error) && resources.Release(&error)
            && f.Refs(0) == 1 && f.Refs(1) == 0, "type reverse restores with no new load/ref leak");
        ObstacleExpiry_(f);
    }
    {
        ObstacleFixture_ f; f.Add(0, 22);
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        ObstacleTarget_(f, before.get(), 0, 22); ObstacleTarget_(f, target.get(), 0, 22);
        CodecObstacle& saved = target->obstacles[0]; saved.ownerSide = 1; saved.featureTriggered = 0;
        saved.featureDamage = 33; saved.featureDuration = 8; saved.animationIndex = 19;
        std::string error; RestoreObstacleResources_ resources;
        CheckAbi_(resources.Prepare(f.Mgr(), *before, *target, &error)
            && RestoreApply_(f.Mgr(), *target, resources, nullptr, &error) && resources.Release(&error), "scalar production apply");
        H3Obstacle& item = f.Items()[0];
        CheckAbi_(item.ownerSide == 1 && item.featureTriggered == 0 && item.featureDamage == 33
            && item.featureDuration == 8 && item.animationIndex == 19 && f.inserts == 0
            && f.removes == 0 && f.loads == 0 && f.Refs(0) == 1, "five scalar updates without topology churn");
    }
    {
        ObstacleFixture_ f; f.Add(0, 22);
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        ObstacleTarget_(f, before.get(), 0, 22);
        ObstacleTarget_(f, target.get(), 0, 22); ObstacleTarget_(f, target.get(), 0, 40);
        ObstacleTarget_(f, target.get(), 0, 60);
        std::string error;
        {
            RestoreObstacleResources_ resources;
            CheckAbi_(resources.Prepare(f.Mgr(), *before, *target, &error)
                && RestoreApply_(f.Mgr(), *target, resources, nullptr, &error), "live same-kind donor adds multiple anchors");
            CheckAbi_(f.Refs(0) == 4 && f.loads == 0 && f.inserts == 2, "borrowed donor entry refs plus independent pool pin");
        }
        CheckAbi_(f.Refs(0) == 3 && f.derefs == 1, "RAII success release balances donor pin");
        ObstacleExpiry_(f); CheckAbi_(f.Refs(0) == 0, "same-kind donor lifecycle balanced");
    }
    for (int failure = 2; failure <= 3; ++failure) {
        ObstacleFixture_ f; f.failLoad = failure;
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        for (uint16_t i = 0; i < 3; ++i) ObstacleTarget_(f, target.get(), i, static_cast<uint8_t>(22 + i));
        std::string error; RestoreObstacleResources_ resources;
        CheckAbi_(!resources.Prepare(f.Mgr(), *before, *target, &error) && !g_restoreFatal
            && resources.Release(&error), "intermediate loader rejection clean cleanup");
        CheckAbi_(f.Refs(0) == 0 && f.Refs(1) == 0 && f.Refs(2) == 0
            && f.Mgr()->obstacleInfo.Count() == 0 && f.inserts == 0 && f.removes == 0,
            "all earlier preload refs returned without game writes");
    }
    {
        ObstacleFixture_ f; f.groups[0].count = 0;
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        ObstacleTarget_(f, target.get(), 0, 22);
        std::string error; RestoreObstacleResources_ resources;
        CheckAbi_(!resources.Prepare(f.Mgr(), *before, *target, &error) && !g_restoreFatal
            && resources.Release(&error) && f.Refs(0) == 0, "invalid loaded DEF rejected with ref returned");
        f.groups[0].count = 1; f.Add(0, 22); f.framePointers[0] = nullptr;
        CheckAbi_(!RestoreObstaclePreflight_(f.Mgr(), *target, &error) && !g_restoreFatal,
            "invalid live DEF rejected in clean preflight");
    }
    for (UINT count = 4094; count <= 4096; ++count) {
        ObstacleFixture_ f; f.Add(0, 22); f.Count(count);
        std::unique_ptr<CodecCapture> target(new CodecCapture{});
        std::string error;
        CheckAbi_(RestoreObstaclePreflight_(f.Mgr(), *target, &error) == (count <= 4095),
            "4095/4096 boundary reserves rollback addition");
        ObstacleTarget_(f, target.get(), 1, 22);
        CheckAbi_(RestoreObstaclePreflight_(f.Mgr(), *target, &error) == (count == 4094),
            "4096 total includes forward plus reverse additions");
        CheckAbi_(f.removes == 0 && f.loads == 0, "capacity rejection is read-only");
        if (count == 4095) {
            std::unique_ptr<CodecCapture> before(new CodecCapture{}), empty(new CodecCapture{});
            ObstacleTarget_(f, before.get(), 0, 22);
            RestoreObstacleResources_ resources;
            CheckAbi_(resources.Prepare(f.Mgr(), *before, *empty, &error)
                && RestoreApply_(f.Mgr(), *empty, resources, nullptr, &error), "4095 forward remove commits");
            f.noLoads = true;
            CheckAbi_(RestoreApply_(f.Mgr(), *before, resources, nullptr, &error)
                && f.Mgr()->obstacleInfo.Count() == 4096 && resources.Release(&error)
                && f.Refs(0) == 1, "4096 reverse append uses reserved capacity and balanced pin");
        }
    }
    for (int fault = 0; fault < 5; ++fault) {
        ObstacleFixture_ f; f.Add(0, 22);
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        ObstacleTarget_(f, before.get(), 0, 22); ObstacleTarget_(f, target.get(), 1, 40);
        std::string error; RestoreObstacleResources_ resources;
        CheckAbi_(resources.Prepare(f.Mgr(), *before, *target, &error), "fault fixture prepared");
        f.removeFault = fault == 0; f.insertFault = fault == 1; f.placeFault = fault == 2;
        f.removeNoop = fault == 3; f.derefFault = fault == 4;
        if (fault == 4) {
            CheckAbi_(!resources.Release(&error) && g_restoreFatal, "pool deref fault fatal");
        }
        else {
            CheckAbi_(!RestoreApply_(f.Mgr(), *target, resources, nullptr, &error) && g_restoreFatal,
                "remove/insert/place fault or uncleared DEF is fail-stop");
            CheckAbi_(f.inserts == (fault == 1 || fault == 2 ? 1 : 0)
                && f.places == (fault == 2 ? 1 : 0), "no next primitive after failure");
        }
        const int calls = f.removes + f.inserts + f.places + f.loads + f.derefs;
        CheckAbi_(!RestoreApply_(f.Mgr(), *before, resources, nullptr, &error) && !resources.Release(&error)
            && calls == f.removes + f.inserts + f.places + f.loads + f.derefs,
            "fatal blocks rollback/release and every further game primitive");
        // Fatal pool deliberately does not touch uncertain game resources again.
    }
    kObstacleRemove = reinterpret_cast<ObstacleRemoveFn_>(0x466710);
    kObstacleInsert = reinterpret_cast<ObstacleVectorInsertFn_>(0x46AA60);
    kObstaclePlace = reinterpret_cast<ObstaclePlaceFn_>(0x466590);
    kObstacleDefLoad = reinterpret_cast<ObstacleDefLoadFn_>(0x55C9C0);
    std::printf("PASS obstacle transaction: production preflight/pool/apply; 0->N/N->0/type/scalars/donor refs, preload rejection, pinned rollback/no loader, balanced refs, zombie expiry, 4095/4096, primitive fail-stop\n");
}

// Raw heap resources only: loaders and vtable +4 own one ref per slot.
struct SiegeFixture_;
static SiegeFixture_* siegeFixture_ = nullptr;
static H3LoadedDef* __fastcall MockSiegeDefLoad_(const char*);
static H3LoadedPcx* __fastcall MockSiegePcxLoad_(const char*);
static void __fastcall MockSiegeDeref_(H3LoadedDef*, int);

struct SiegeFixture_
{
    std::unique_ptr<uint8_t[]> manager{new uint8_t[sizeof(H3CombatManager)]{}};
    std::unique_ptr<uint8_t[]> defs[2], pcxs[2], frames[2][2], palettes[2];
    H3LoadedDef::DefGroup groups[2] = {};
    H3LoadedDef::DefGroup* groupPointers[2] = {};
    H3DefFrame* framePointers[2][2] = {};
    int active[2] = {1, 1};
    uint8_t pixels[2][4] = {}, pcxPixels[2][4] = {};
    void* vtable[2] = {};
    int loads = 0, defLoads = 0, pcxLoads = 0, derefs = 0, released[4] = {};
    int failLoad = 0, faultDeref = 0;
    bool noLoads = false;
    const bool oldInitialized = g_battleInitialized;
    const SiegeDefLoadFn_ oldDefLoad = kSiegeDefLoad_;
    const SiegePcxLoadFn_ oldPcxLoad = kSiegePcxLoad_;
    SiegeFixture_() {
        CheckAbi_(!siegeFixture_, "one siege fixture at a time");
        siegeFixture_ = this; g_restoreFatal = false; g_battleInitialized = true;
        vtable[1] = reinterpret_cast<void*>(&MockSiegeDeref_);
        const char* defNames[] = {"S0.DEF", "S1.DEF"};
        const char* pcxNames[] = {"W0.PCX", "W1.PCX"};
        for (int i = 0; i < 2; ++i) {
            defs[i].reset(new uint8_t[sizeof(H3LoadedDef)]{});
            pcxs[i].reset(new uint8_t[sizeof(H3LoadedPcx)]{});
            palettes[i].reset(new uint8_t[sizeof(H3Palette565)]{});
            InitResource(defs[i].get(), defNames[i]);
            InitResource(pcxs[i].get(), pcxNames[i]);
            for (int frame = 0; frame < 2; ++frame) {
                frames[i][frame].reset(new uint8_t[sizeof(H3DefFrame)]{});
                H3DefFrame* value = reinterpret_cast<H3DefFrame*>(frames[i][frame].get());
                framePointers[i][frame] = value;
                value->width = value->frameWidth = value->width2 = 2;
                value->height = value->frameHeight = 2;
                value->rawDataSize = value->dataSize = 4;
                value->rawData = pixels[i];
            }
            groups[i].count = 2; groups[i].frames = framePointers[i];
            groupPointers[i] = &groups[i];
            Def(i)->groupsCount = 1; Def(i)->groups = &groupPointers[i];
            Def(i)->activeGroups = &active[i];
            Def(i)->widthDEF = Def(i)->heightDEF = 2;
            Def(i)->palette565 = reinterpret_cast<H3Palette565*>(palettes[i].get());
            Pcx(i)->width = Pcx(i)->height = Pcx(i)->scanlineSize = 2;
            Pcx(i)->bufSize = 4; Pcx(i)->buffer = pcxPixels[i];
        }
        kSiegeDefLoad_ = &MockSiegeDefLoad_; kSiegePcxLoad_ = &MockSiegePcxLoad_;
    }
    ~SiegeFixture_() {
        if (!g_restoreFatal) CheckLiveRefs();
        kSiegeDefLoad_ = oldDefLoad; kSiegePcxLoad_ = oldPcxLoad;
        g_battleInitialized = oldInitialized; g_restoreFatal = false; siegeFixture_ = nullptr;
    }
    void InitResource(uint8_t* bytes, const char* name) {
        *reinterpret_cast<void***>(bytes) = vtable;
        strcpy_s(reinterpret_cast<char*>(bytes + 4), 12, name);
    }
    H3CombatManager* Mgr() { return reinterpret_cast<H3CombatManager*>(manager.get()); }
    H3LoadedDef* Def(int i) { return reinterpret_cast<H3LoadedDef*>(defs[i].get()); }
    H3LoadedPcx* Pcx(int i) { return reinterpret_cast<H3LoadedPcx*>(pcxs[i].get()); }
    H3ResourceItem* Item(int i) { return i < 2 ? static_cast<H3ResourceItem*>(Def(i)) : Pcx(i - 2); }
    INT32& Refs(int i) { return *reinterpret_cast<INT32*>(reinterpret_cast<uint8_t*>(Item(i)) + 0x18); }
    int Index(H3ResourceItem* item) {
        for (int i = 0; i < 4; ++i) if (item == Item(i)) return i;
        CheckAbi_(false, "siege primitive only sees fixture-owned resource bytes"); return -1;
    }
    void CheckLiveRefs() {
        int slots[4] = {};
        for (int i = 0; i < 96; ++i) {
            H3ResourceItem* item = RestoreSiege_::LiveOwned(Mgr(), i);
            if (item) ++slots[Index(item)];
        }
        for (int i = 0; i < 4; ++i)
            CheckAbi_(Refs(i) == slots[i], "siege refs equal distinct live ownership slots");
    }
    void Bind(const CodecCapture& capture) {
        Mgr()->siegeKind2 = capture.siegeKind2;
        for (int i = 0; i < 3; ++i) {
            uint8_t* tower = reinterpret_cast<uint8_t*>(&Mgr()->towers[i]);
            memcpy(tower, capture.towers[i].scalars, 4);
            memcpy(tower + 0x0C, &capture.towers[i].scalars[1], 24);
        }
        for (int i = 0; i < 96; ++i) {
            const char* name = RestoreSiege_::Name(capture, i);
            if (!name[0]) continue;
            H3ResourceItem* item = i < 6 ? static_cast<H3ResourceItem*>(Def(name[1] - '0')) : Pcx(name[1] - '0');
            if (i < 6) {
                if (i % 2) Mgr()->towers[i / 2].shotDefLoaded = static_cast<H3LoadedDef*>(item);
                else Mgr()->towers[i / 2].monDefLoaded = static_cast<H3LoadedDef*>(item);
            }
            else Mgr()->townSiegePcx[(i - 6) / 5][(i - 6) % 5] = static_cast<H3LoadedPcx*>(item);
            ++Refs(Index(item));
        }
    }
};
static H3LoadedDef* __fastcall MockSiegeDefLoad_(const char* name)
{
    SiegeFixture_& f = *siegeFixture_; ++f.loads; ++f.defLoads;
    CheckAbi_(!f.noLoads, "siege switch/rollback never calls loader");
    if (f.loads == f.failLoad) return nullptr;
    for (int i = 0; i < 2; ++i) if (strcmp(name, i ? "S1.DEF" : "S0.DEF") == 0) {
        ++f.Refs(i); return f.Def(i);
    }
    CheckAbi_(false, "siege DEF loader receives fixture name"); return nullptr;
}
static H3LoadedPcx* __fastcall MockSiegePcxLoad_(const char* name)
{
    SiegeFixture_& f = *siegeFixture_; ++f.loads; ++f.pcxLoads;
    CheckAbi_(!f.noLoads, "siege switch/rollback never calls PCX loader");
    if (f.loads == f.failLoad) return nullptr;
    for (int i = 0; i < 2; ++i) if (strcmp(name, i ? "W1.PCX" : "W0.PCX") == 0) {
        ++f.Refs(i + 2); return f.Pcx(i);
    }
    CheckAbi_(false, "siege PCX loader receives fixture name"); return nullptr;
}
static void __fastcall MockSiegeDeref_(H3LoadedDef* item, int)
{
    SiegeFixture_& f = *siegeFixture_;
    const int index = f.Index(item); ++f.derefs;
    if (f.derefs == f.faultDeref) RaiseException(kOriginalException_, 0, 0, nullptr);
    CheckAbi_(f.Refs(index) > 0, "siege releases each slot once, never double deref");
    --f.Refs(index); ++f.released[index];
}
static void SiegeTarget_(CodecCapture* capture, int resource = 0, int scalarBase = 10)
{
    capture->siegeKind2 = 1; capture->siegeKind = 0;
    for (int i = 0; i < 3; ++i) {
        CodecTower_& tower = capture->towers[i];
        strcpy_s(tower.defName, resource ? "S1.DEF" : "S0.DEF");
        strcpy_s(tower.missileName, resource ? "S1.DEF" : "S0.DEF");
        for (int j = 0; j < 7; ++j) tower.scalars[j] = scalarBase + i * 10 + j;
        tower.scalars[3] = i % 2; tower.scalars[4] = 0; tower.scalars[5] = 1; tower.scalars[6] = i;
    }
    for (int i : {0, 20, 89}) strcpy_s(capture->wallPcxNames[i], resource ? "W1.PCX" : "W0.PCX");
}
static void CheckSiegeLive_(SiegeFixture_& f, const CodecCapture& capture)
{
    for (int i = 0; i < 3; ++i)
        CheckAbi_(RestoreSiege_::TowerMatches(f.Mgr()->towers[i], capture.towers[i]), "siege swap installs all tower scalar/frame bytes");
    for (int i = 0; i < 96; ++i)
        CheckAbi_(RestoreSiege_::ResourceMatches(RestoreSiege_::LiveOwned(f.Mgr(), i), capture, i),
            "siege swap installs every target resource slot including required door/wall");
}
static void TestSiegeTransaction_()
{
    for (int direction = 0; direction < 2; ++direction) for (int rollback = 0; rollback < 2; ++rollback) {
        SiegeFixture_ f;
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        SiegeTarget_(direction ? before.get() : target.get()); f.Bind(*before);
        const std::vector<uint8_t> original(f.manager.get(), f.manager.get() + sizeof(H3CombatManager));
        std::string error; RestoreSiege_ siege;
        CheckAbi_(siege.Prepare(f.Mgr(), *before, *target, &error), "siege empty->N/N->empty production Prepare");
        CheckAbi_(memcmp(original.data(), f.manager.get(), original.size()) == 0
            && f.loads == (direction ? 0 : 9) && f.defLoads == (direction ? 0 : 6)
            && f.pcxLoads == (direction ? 0 : 3) && f.Refs(0) == 6 && f.Refs(2) == 3,
            "siege preload is read-only and cache aliases acquire one ref per slot");
        f.noLoads = true;
        CheckAbi_(siege.Switch(f.Mgr()) && siege.switched, "siege production forward Switch");
        CheckSiegeLive_(f, *target);
        if (rollback) {
            CheckAbi_(siege.Switch(f.Mgr()) && !siege.switched
                && memcmp(original.data(), f.manager.get(), original.size()) == 0,
                "siege second Switch restores exact before manager bytes without loader");
            CheckSiegeLive_(f, *before);
        }
        const int releases = (rollback ? !direction : direction) ? 9 : 0;
        CheckAbi_(siege.Release(&error) && siege.Release(&error) && f.derefs == releases
            && f.released[0] == (releases ? 6 : 0) && f.released[2] == (releases ? 3 : 0),
            "siege commit/rollback releases detached aliases per slot and is idempotent");
        f.CheckLiveRefs();
    }
    for (int resource = 0; resource < 2; ++resource) {
        SiegeFixture_ f;
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        SiegeTarget_(before.get()); SiegeTarget_(target.get(), resource, 70); f.Bind(*before);
        const std::vector<uint8_t> original(f.manager.get(), f.manager.get() + sizeof(H3CombatManager));
        std::string error; RestoreSiege_ siege;
        CheckAbi_(siege.Prepare(f.Mgr(), *before, *target, &error) && f.loads == 9
            && f.Refs(0) == (resource ? 6 : 12) && f.Refs(2) == (resource ? 3 : 6),
            "siege live/prepared same-pointer aliases retain independent slot refs");
        f.noLoads = true;
        CheckAbi_(siege.Switch(f.Mgr()), "siege populated replacement Switch"); CheckSiegeLive_(f, *target);
        CheckAbi_(siege.Switch(f.Mgr()) && memcmp(original.data(), f.manager.get(), original.size()) == 0
            && siege.Release(&error) && f.derefs == 9, "siege populated double swap/release preserves original ownership");
        CheckAbi_(f.released[resource] == 6 && f.released[resource + 2] == 3, "siege rollback releases target aliases, not live refs");
        f.CheckLiveRefs();
    }
    for (int failure : {2, 7, 9}) {
        SiegeFixture_ f; f.failLoad = failure;
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        SiegeTarget_(target.get());
        const std::vector<uint8_t> original(f.manager.get(), f.manager.get() + sizeof(H3CombatManager));
        std::string error; RestoreSiege_ siege;
        CheckAbi_(!siege.Prepare(f.Mgr(), *before, *target, &error) && !error.empty() && !g_restoreFatal
            && siege.Release(&error) && f.loads == failure && f.derefs == failure - 1,
            "siege intermediate DEF/PCX preload failure returns every earlier slot ref cleanly");
        CheckAbi_(memcmp(original.data(), f.manager.get(), original.size()) == 0
            && f.Refs(0) == 0 && f.Refs(2) == 0, "siege clean preload failure never mutates live manager");
    }
    for (int invalid = 0; invalid < 3; ++invalid) {
        SiegeFixture_ f;
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        SiegeTarget_(target.get());
        if (invalid == 0) target->towers[0].scalars[5] = 2;
        if (invalid == 1) {
            // Tower selects frame zero; missile must validate every frame in group zero.
            target->towers[0].scalars[5] = 0; f.framePointers[0][1]->marginLeft = 1;
        }
        if (invalid == 2) f.Pcx(0)->bufSize = 3;
        std::string error; RestoreSiege_ siege;
        CheckAbi_(!siege.Prepare(f.Mgr(), *before, *target, &error) && !g_restoreFatal
            && siege.Release(&error) && f.derefs == f.loads && f.Refs(0) == 0 && f.Refs(2) == 0,
            "siege bad selected frame/missile later frame/PCX geometry is clean rejection");
    }
    for (int drift = 0; drift < 3; ++drift) {
        SiegeFixture_ f;
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        SiegeTarget_(before.get()); SiegeTarget_(target.get(), 1); f.Bind(*before);
        std::string error;
        {
            RestoreSiege_ siege; CheckAbi_(siege.Prepare(f.Mgr(), *before, *target, &error), "siege drift fixture prepared");
            if (drift == 0) ++f.Mgr()->towers[0].creatureX;
            if (drift == 1) f.Mgr()->townSiegePcx[4][0] = nullptr;
            if (drift == 2) --f.Refs(0);
            const std::vector<uint8_t> drifted(f.manager.get(), f.manager.get() + sizeof(H3CombatManager));
            CheckAbi_(!siege.Switch(f.Mgr()) && g_restoreFatal && !siege.Switch(f.Mgr())
                && !siege.Release(&error) && f.derefs == 0 && f.loads == 9
                && memcmp(drifted.data(), f.manager.get(), drifted.size()) == 0,
                "siege live tower/wall/ref drift fail-stops without swap/release or another primitive");
        }
        CheckAbi_(f.derefs == 0, "siege fatal destructor never releases uncertain ownership");
    }
    {
        SiegeFixture_ f;
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        SiegeTarget_(target.get()); std::string error;
        {
            RestoreSiege_ siege; CheckAbi_(siege.Prepare(f.Mgr(), *before, *target, &error), "siege release-fault fixture prepared");
            f.faultDeref = 1;
            CheckAbi_(!siege.Release(&error) && g_restoreFatal && f.derefs == 1
                && !siege.Owned(0) && siege.Owned(1) == f.Def(0)
                && f.Refs(0) == 6 && f.Refs(2) == 3, "siege first mockRaise forgets attempted slot and stops before every later release");
            CheckAbi_(!siege.Release(&error) && !siege.Switch(f.Mgr()) && f.derefs == 1,
                "siege first release fault forbids retry and all subsequent releases");
        }
        CheckAbi_(f.derefs == 1 && f.released[0] == 0 && f.released[2] == 0,
            "siege fatal RAII performs zero subsequent resource releases");
    }
    std::printf("PASS siege transaction: production Prepare/Switch/Release, raw DEF/PCX geometry, empty/N, per-slot alias refs, exact double swap, clean preload rejection, drift and first-release SEH fail-stop; no native game resource calls\n");
}

static void TestRestoreManagerScalars_()
{
    const ObstacleRemoveFn_ oldRemove = kObstacleRemove;
    const ObstacleVectorInsertFn_ oldInsert = kObstacleInsert;
    const ObstaclePlaceFn_ oldPlace = kObstaclePlace;
    const ObstacleDefLoadFn_ oldLoad = kObstacleDefLoad;
    ObstacleFixture_ f;
    std::unique_ptr<CodecCapture> target(new CodecCapture{});
    std::unique_ptr<uint8_t[]> heroes[2];
    uint32_t random = 0x1234ABCDu;
    auto next = [&]() { random = random * 1664525u + 1013904223u; return random; };
#define SEED_MANAGER_FIELD_(field) do { \
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&target->field); \
    for (size_t i = 0; i < sizeof(target->field); ++i) bytes[i] = static_cast<uint8_t>(next() >> 24); \
} while (0)
    SEED_MANAGER_FIELD_(landType); SEED_MANAGER_FIELD_(absoluteObstacleId);
    SEED_MANAGER_FIELD_(siegeKind); SEED_MANAGER_FIELD_(hasMoat); SEED_MANAGER_FIELD_(specialTerrain);
    SEED_MANAGER_FIELD_(antiMagicGarrison); SEED_MANAGER_FIELD_(creatureBank); SEED_MANAGER_FIELD_(boatCombat);
    SEED_MANAGER_FIELD_(siegeKind2); SEED_MANAGER_FIELD_(finished); SEED_MANAGER_FIELD_(autoCombat);
    SEED_MANAGER_FIELD_(tacticsPhase); SEED_MANAGER_FIELD_(tacticsDifference);
    SEED_MANAGER_FIELD_(isNotAI); SEED_MANAGER_FIELD_(isHuman); SEED_MANAGER_FIELD_(heroOwner);
    SEED_MANAGER_FIELD_(heroMonCount); SEED_MANAGER_FIELD_(action); SEED_MANAGER_FIELD_(actionParameter);
    SEED_MANAGER_FIELD_(actionTarget); SEED_MANAGER_FIELD_(actionParameter2);
    SEED_MANAGER_FIELD_(turn); SEED_MANAGER_FIELD_(waitPhase);
    SEED_MANAGER_FIELD_(necromancyRaisedAmount); SEED_MANAGER_FIELD_(necromancyRaisedMonsters);
    SEED_MANAGER_FIELD_(artifactAutoCast); SEED_MANAGER_FIELD_(heroCasted); SEED_MANAGER_FIELD_(heroSpellPower);
    SEED_MANAGER_FIELD_(turnsSinceLastEnchanterCast); SEED_MANAGER_FIELD_(summonedMonster);
    SEED_MANAGER_FIELD_(fortWallsHp); SEED_MANAGER_FIELD_(fortWallsAlive);
    SEED_MANAGER_FIELD_(massSpellTarget); SEED_MANAGER_FIELD_(accessibleSquares); SEED_MANAGER_FIELD_(accessibleSquares2);
    SEED_MANAGER_FIELD_(extraScalars);
#undef SEED_MANAGER_FIELD_
    // H3API calls this a DWORD, but capture owns only the native moat byte.
    target->hasMoat = 1;
    target->artifactAutoCast[0] = 1; target->artifactAutoCast[1] = 1;
    target->currentMonSide = 1; target->currentMonIndex = 19; target->currentActiveSide = 1;
    for (int side = 0; side < 2; ++side) {
        heroes[side].reset(new uint8_t[sizeof(H3Hero)]{});
        f.Mgr()->hero[side] = reinterpret_cast<H3Hero*>(heroes[side].get());
        target->spellPoints[side] = static_cast<int16_t>(side ? -123 : 321);
    }
    target->stacks[0][0].occupied = target->stacks[1][19].occupied = true;
    target->stacks[0][0].aiTarget = {1, 19}; target->stacks[1][19].aiTarget = {-1, -1};
    for (int side = 0; side < 2; ++side) {
        CodecStack& stack = target->stacks[side][side ? 19 : 0];
        for (auto& byte : stack.extraScalars) byte = static_cast<uint8_t>(next() >> 24);
    }
    for (int tower = 0; tower < 3; ++tower)
        for (auto& scalar : target->towers[tower].scalars) scalar = static_cast<int32_t>(next());
    for (int square = 0; square < 187; ++square) {
        CodecSquare& cell = target->squares[square];
        for (auto& byte : cell.extraScalars) byte = static_cast<uint8_t>(next() >> 24);
        cell.stackSide = cell.stackIndex = -1;
        cell.obstacleBits = static_cast<uint8_t>(next());
        cell.twoHexMonsterSquare = static_cast<uint8_t>(next());
        cell.deadStacksNumber = square % 15;
        for (int corpse = 0; corpse < 14; ++corpse) {
            cell.deadStackSide[corpse] = static_cast<int8_t>(next());
            cell.deadStackIndex[corpse] = static_cast<int8_t>(next());
            cell.belongsToAttacker[corpse] = static_cast<uint8_t>(next());
        }
        cell.availableForLeftSquare = static_cast<uint8_t>(next());
        cell.availableForRightSquare = static_cast<uint8_t>(next());
    }
    std::string error; RestoreObstacleResources_ resources;
    CheckAbi_(RestoreApply_(f.Mgr(), *target, resources, nullptr, &error), "pure manager apply without game globals");
#define CHECK_MANAGER_FIELD_(field) CheckAbi_(sizeof(f.Mgr()->field) == sizeof(target->field) \
    && memcmp(&f.Mgr()->field, &target->field, sizeof(target->field)) == 0, "manager restores " #field)
    CHECK_MANAGER_FIELD_(landType); CHECK_MANAGER_FIELD_(absoluteObstacleId);
    CHECK_MANAGER_FIELD_(siegeKind); CHECK_MANAGER_FIELD_(specialTerrain);
    CheckAbi_(f.manager[0x53A8] == static_cast<uint8_t>(target->hasMoat), "native moat byte restored independently");
    CHECK_MANAGER_FIELD_(antiMagicGarrison); CHECK_MANAGER_FIELD_(creatureBank); CHECK_MANAGER_FIELD_(boatCombat);
    CHECK_MANAGER_FIELD_(siegeKind2); CHECK_MANAGER_FIELD_(finished); CHECK_MANAGER_FIELD_(autoCombat);
    CHECK_MANAGER_FIELD_(tacticsPhase); CHECK_MANAGER_FIELD_(tacticsDifference);
    CHECK_MANAGER_FIELD_(isNotAI); CHECK_MANAGER_FIELD_(isHuman); CHECK_MANAGER_FIELD_(heroOwner);
    CHECK_MANAGER_FIELD_(heroMonCount); CHECK_MANAGER_FIELD_(action); CHECK_MANAGER_FIELD_(actionParameter);
    CHECK_MANAGER_FIELD_(actionTarget); CHECK_MANAGER_FIELD_(actionParameter2);
    CHECK_MANAGER_FIELD_(currentMonSide); CHECK_MANAGER_FIELD_(currentMonIndex); CHECK_MANAGER_FIELD_(currentActiveSide);
    CHECK_MANAGER_FIELD_(turn);
    CheckAbi_(f.manager[0x13DE4] == static_cast<uint8_t>(target->waitPhase), "native wait-phase byte restored without padding");
    CHECK_MANAGER_FIELD_(necromancyRaisedAmount); CHECK_MANAGER_FIELD_(necromancyRaisedMonsters);
    CHECK_MANAGER_FIELD_(artifactAutoCast); CHECK_MANAGER_FIELD_(heroCasted); CHECK_MANAGER_FIELD_(heroSpellPower);
    CHECK_MANAGER_FIELD_(turnsSinceLastEnchanterCast); CHECK_MANAGER_FIELD_(summonedMonster);
    CHECK_MANAGER_FIELD_(fortWallsHp); CHECK_MANAGER_FIELD_(fortWallsAlive);
    CHECK_MANAGER_FIELD_(massSpellTarget); CHECK_MANAGER_FIELD_(accessibleSquares); CHECK_MANAGER_FIELD_(accessibleSquares2);
#undef CHECK_MANAGER_FIELD_
    CheckAbi_(f.Mgr()->activeStack == &f.Mgr()->stacks[1][19], "activeStack rebuilt from manager identity");
    CheckAbi_(f.Mgr()->blueHighlight == 0 && f.Mgr()->mouseCoord == -1 && f.Mgr()->creatureAtMousePos == -1
        && f.Mgr()->attackerCoord == -1 && f.Mgr()->moveType == -99
        && *reinterpret_cast<int32_t*>(f.manager.get() + 0x132E0) == 0, "hover intent invalidated after scalar apply");
    size_t packed = 0;
    for (const auto& range : kManagerExtraRanges_) {
        CheckAbi_(memcmp(f.manager.get() + range.offset, target->extraScalars + packed, range.size) == 0,
            "all captured omitted manager numeric ranges restored");
        packed += range.size;
    }
    for (int tower = 0; tower < 3; ++tower) {
        const uint8_t* raw = f.manager.get() + 0x13D78 + tower * 0x24;
        CheckAbi_(memcmp(raw, target->towers[tower].scalars, 4) == 0
            && memcmp(raw + 0x0C, &target->towers[tower].scalars[1], 24) == 0,
            "all tower scalar/frame bytes restored without resource calls");
    }
    for (int side = 0; side < 2; ++side) {
        const int slot = side ? 19 : 0;
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(&f.Mgr()->stacks[side][slot]);
        packed = 0;
        for (const auto& range : kStackExtraRanges_) {
            CheckAbi_(memcmp(raw + range.offset, target->stacks[side][slot].extraScalars + packed, range.size) == 0,
                "all captured omitted stack numeric ranges restored");
            packed += range.size;
        }
        CheckAbi_(f.Mgr()->hero[side]->spellPoints == target->spellPoints[side], "both hero mana fields restored");
    }
    CheckAbi_(*reinterpret_cast<H3CombatCreature**>(reinterpret_cast<uint8_t*>(&f.Mgr()->stacks[0][0]) + 0x538)
        == &f.Mgr()->stacks[1][19]
        && !*reinterpret_cast<H3CombatCreature**>(reinterpret_cast<uint8_t*>(&f.Mgr()->stacks[1][19]) + 0x538),
        "AI pointers rebuilt as manager identities and NULL");
    for (int square = 0; square < 187; ++square) {
        const H3CombatSquare& live = f.Mgr()->squares[square]; const CodecSquare& saved = target->squares[square];
        packed = 0;
        for (const auto& range : kSquareExtraRanges_) {
            CheckAbi_(memcmp(reinterpret_cast<const uint8_t*>(&live) + range.offset,
                saved.extraScalars + packed, range.size) == 0, "all captured omitted square numeric ranges restored");
            packed += range.size;
        }
        CheckAbi_(*(reinterpret_cast<const uint8_t*>(&live) + 0x4C) == 0, "square hover byte invalidated");
        CheckAbi_(live.obstacleIndex == -1 && live.obstacleBits == saved.obstacleBits
            && live.stackSide == saved.stackSide && live.stackIndex == saved.stackIndex
            && static_cast<uint8_t>(live.twoHexMonsterSquare) == saved.twoHexMonsterSquare && live.deadStacksNumber == saved.deadStacksNumber
            && memcmp(live.deadStackSide, saved.deadStackSide, sizeof(saved.deadStackSide)) == 0
            && memcmp(live.deadStackIndex, saved.deadStackIndex, sizeof(saved.deadStackIndex)) == 0
            && memcmp(live.belongsToAttacker, saved.belongsToAttacker, sizeof(saved.belongsToAttacker)) == 0
            && static_cast<uint8_t>(live.availableForLeftSquare) == saved.availableForLeftSquare
            && static_cast<uint8_t>(live.availableForRightSquare) == saved.availableForRightSquare, "all187 square payloads restored");
    }
    CheckAbi_(f.loads == 0 && f.removes == 0 && f.inserts == 0 && f.places == 0 && f.derefs == 0,
        "manager scalar apply invokes no native resource primitive");
    kObstacleRemove = oldRemove; kObstacleInsert = oldInsert;
    kObstaclePlace = oldPlace; kObstacleDefLoad = oldLoad;
    std::printf("PASS manager apply: all scalar/array/extra ranges, tower frames, AI identities, mana, squares; no game globals\n");
}

static void TestPreparationSideEffects_()
{
    uint8_t* oldMap = kObjectResourceMap_, *oldText = kObjectTextBuffer_;
    const uint8_t* const* oldNil = kObjectResourceNilSlot_;
    std::unique_ptr<uint8_t[]> manager(new uint8_t[sizeof(H3CombatManager)]{});
    uint32_t map[4] = {}, head[9] = {}, root[9] = {}, leaf[9] = {}, nil[9] = {};
    uint32_t outsideSound[13] = {}, liveSound[13] = {}, texture[13] = {};
    uint8_t text[512], originalText[512];
    for (int i = 0; i < 512; ++i) text[i] = static_cast<uint8_t>(i * 29);
    memcpy(originalText, text, sizeof(text));
    outsideSound[5] = liveSound[5] = 32; texture[5] = 16;
    outsideSound[6] = liveSound[6] = texture[6] = 1;
    outsideSound[10] = 7; outsideSound[11] = 71; outsideSound[12] = 19;
    liveSound[10] = 8; liveSound[11] = 81; liveSound[12] = 29;
    map[1] = reinterpret_cast<uint32_t>(head); map[3] = 2;
    head[1] = reinterpret_cast<uint32_t>(root);
    root[0] = reinterpret_cast<uint32_t>(nil); root[1] = reinterpret_cast<uint32_t>(head);
    root[2] = reinterpret_cast<uint32_t>(leaf); root[7] = reinterpret_cast<uint32_t>(outsideSound);
    leaf[0] = leaf[2] = reinterpret_cast<uint32_t>(nil);
    leaf[1] = reinterpret_cast<uint32_t>(root); leaf[7] = reinterpret_cast<uint32_t>(texture);
    const uint8_t* nilPointer = reinterpret_cast<const uint8_t*>(nil);
    kObjectResourceMap_ = reinterpret_cast<uint8_t*>(map);
    kObjectResourceNilSlot_ = &nilPointer; kObjectTextBuffer_ = text;
    H3CombatManager* mgr = reinterpret_cast<H3CombatManager*>(manager.get());
    uint8_t* live = reinterpret_cast<uint8_t*>(&mgr->stacks[0][0]);
    *reinterpret_cast<void**>(live + 0x170) = liveSound;
    *reinterpret_cast<void**>(live + 0x174) = outsideSound;
    {
        ObjectSoundGuard_ guard;
        CheckAbi_(guard.Capture(mgr) && guard.entries.size() == 2,
            "prepare captures outside cached WAV, deduplicates live aliases, excludes PCX");
        outsideSound[10] = outsideSound[11] = outsideSound[12] = 0;
        liveSound[10] = liveSound[11] = liveSound[12] = 0;
        texture[10] = 1234; memset(text, 0, sizeof(text));
    }
    CheckAbi_(!g_restoreFatal && outsideSound[10] == 7 && outsideSound[11] == 71 && outsideSound[12] == 19
        && liveSound[10] == 8 && liveSound[11] == 81 && liveSound[12] == 29 && texture[10] == 1234
        && memcmp(text, originalText, sizeof(text)) == 0, "prepare guard restores existing shared WAVs and all512 text bytes");
    for (int defect = 0; defect < 3; ++defect) {
        const uint32_t count = map[3], parent = leaf[1], refs = outsideSound[6];
        if (!defect) map[3] = 1;
        if (defect == 1) leaf[1] = reinterpret_cast<uint32_t>(head);
        if (defect == 2) outsideSound[6] = 0;
        {
            ObjectSoundGuard_ guard;
            CheckAbi_(!guard.Capture(mgr), "prepare guard cleanly rejects bad cache count, parent, resource refs");
        }
        map[3] = count; leaf[1] = parent; outsideSound[6] = refs;
        CheckAbi_(!g_restoreFatal, "invalid cache rejection does not enter native fatal path");
    }
    kObjectResourceMap_ = oldMap; kObjectResourceNilSlot_ = oldNil; kObjectTextBuffer_ = oldText;
    std::printf("PASS preparation side effects: outside/live WAV aliases, resource map guards, all512 scratch bytes\n");
}

static unsigned test_rngSeed_ = 0;
static int test_rngCalls_ = 0;
static bool test_rngCleanup_ = false;
static void __cdecl MockRngSet_(unsigned seed)
{
    ++test_rngCalls_; test_rngSeed_ = seed;
}
struct RngCleanupProbe_
{
    ~RngCleanupProbe_() {
        test_rngCleanup_ = true;
        test_rngSeed_ = 0xBAD00001u;
        *reinterpret_cast<uint32_t*>(0x67FBE4) = 0xBAD00002u;
    }
};
static void TestRestoreRngGuard_()
{
    void* mapping = nullptr;
    if (!Readable_(reinterpret_cast<void*>(0x67FBE4), 4)) {
        mapping = VirtualAlloc(reinterpret_cast<void*>(0x670000), 0x10000,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        CheckAbi_(mapping == reinterpret_cast<void*>(0x670000), "map controlled RNG mirror page");
    }
    CheckAbi_(!IsBadWritePtr(reinterpret_cast<void*>(0x67FBE4), 4), "RNG mirror page writable");
    const uint32_t oldMirror = *reinterpret_cast<uint32_t*>(0x67FBE4);
    const RngSetFn oldSet = kRngSet;
    kRngSet = &MockRngSet_;
    std::unique_ptr<CodecCapture> before(new CodecCapture{}), saved(new CodecCapture{});
    before->rngTlsSeed = 0x12345678u; before->rngMirrorSeed = 0x87654321u;
    saved->rngTlsSeed = 0x0BADF00Du; saved->rngMirrorSeed = 0xCAFEBABEu;
    g_restoreFatal = false; test_rngCalls_ = 0; test_rngCleanup_ = false;
    {
        RestoreRngGuard_ guard(*before);
        RngCleanupProbe_ cleanup;
        test_rngSeed_ = 99; *reinterpret_cast<uint32_t*>(0x67FBE4) = 100;
        CheckAbi_(test_rngCalls_ == 0, "RNG guard construction performs no draw or game call");
    }
    CheckAbi_(test_rngCleanup_ && test_rngCalls_ == 1 && test_rngSeed_ == before->rngTlsSeed
        && *reinterpret_cast<uint32_t*>(0x67FBE4) == before->rngMirrorSeed,
        "scope exit restores before RNG after transaction cleanup");
    test_rngCalls_ = 0; test_rngCleanup_ = false;
    {
        RestoreRngGuard_ guard(*before);
        { RngCleanupProbe_ cleanup; }
        CheckAbi_(test_rngCleanup_ && guard.Finish(*saved), "Finish saved runs after cleanup");
        CheckAbi_(test_rngCalls_ == 1 && test_rngSeed_ == saved->rngTlsSeed
            && *reinterpret_cast<uint32_t*>(0x67FBE4) == saved->rngMirrorSeed, "Finish writes exact saved TLS and mirror");
    }
    CheckAbi_(test_rngCalls_ == 1 && test_rngSeed_ == saved->rngTlsSeed
        && *reinterpret_cast<uint32_t*>(0x67FBE4) == saved->rngMirrorSeed,
        "finished guard destructor never rewrites before");
    test_rngCalls_ = 0;
    {
        RestoreRngGuard_ guard(*before);
        g_restoreFatal = true;
        CheckAbi_(!guard.Finish(*saved), "fatal Finish is rejected before RNG setter");
    }
    CheckAbi_(test_rngCalls_ == 0 && test_rngSeed_ == saved->rngTlsSeed
        && *reinterpret_cast<uint32_t*>(0x67FBE4) == saved->rngMirrorSeed, "fatal guard never calls RNG setter or mirror write");
    g_restoreFatal = false; kRngSet = oldSet;
    *reinterpret_cast<uint32_t*>(0x67FBE4) = oldMirror;
    if (mapping) CheckAbi_(VirtualFree(mapping, 0, MEM_RELEASE) != FALSE, "release controlled RNG mirror page");
    std::printf("PASS RNG guard: mocked setter/mirror, exit-before after cleanup, Finish(saved), fatal no calls\n");
}

static void TestRestoreReasonZh_()
{
    const char* reasons[] = {"battle generation changed", "saved AI target invalid", "archive CRC mismatch",
        "Section CRC MISMATCH", "unknown future diagnostic", "", "城墙状态超出有效范围", "恢复随机数状态时发生异常，已停止战斗"};
    for (const char* raw : reasons) {
        const std::string result = UiRestoreReasonZh_(raw);
        wchar_t wide[256] = {};
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, result.c_str(), -1, wide, _countof(wide));
        bool chinese = false, english = false;
        for (int i = 0; i + 1 < count; ++i) {
            chinese = chinese || (wide[i] >= 0x4E00 && wide[i] <= 0x9FFF);
            english = english || (wide[i] >= L'A' && wide[i] <= L'Z') || (wide[i] >= L'a' && wide[i] <= L'z');
        }
        CheckAbi_(!result.empty() && count > 0 && chinese && !english, "common/Chinese/CRC/unknown/empty reason is nonempty Chinese");
    }
    g_restoreFatal = true;
    UiRestoreFailure_("fatal-test", "unknown future diagnostic");
    CheckAbi_(g_restoreFatal, "fatal UI failure returns without calling absolute game Show");
    g_restoreFatal = false;
    std::printf("PASS restore reason: production Chinese mapping/fallback/empty/CRC; fatal never Show\n");
}

// Native ownership backend: all absolute game calls are replaced before use.
struct ObjectFixture_;
static ObjectFixture_* objectFixture_ = nullptr;
static H3CombatCreature* __fastcall MockStackCtor_(H3CombatCreature*, int);
static void __fastcall MockStackDtor_(H3CombatCreature*, int);
static void __fastcall MockStackInit_(H3CombatCreature*, int, int, int, H3Hero*, int, int, int);
static void __fastcall MockStackResources_(H3CombatCreature*, int);
static void* __cdecl MockGameAlloc_(unsigned);
static void __cdecl MockGameFree_(void*);
static void* __fastcall MockSpellSetCtor_(void*, int, const char*, const char*);
static void __fastcall MockSpellSetInsert_(void*, int, void*, const int*);
static void __fastcall MockSpellSetDtor_(void*, int);
static void __fastcall MockDequeTrimTail_(H3CombatCreature*, int);

struct ObjectFixture_
{
    struct Allocation { void* address; unsigned bytes; };
    std::vector<Allocation> allocations;
    std::unique_ptr<uint8_t[]> manager{new uint8_t[sizeof(H3CombatManager)]{}};
    std::unique_ptr<uint8_t[]> defBytes{new uint8_t[sizeof(H3LoadedDef)]{}};
    std::unique_ptr<uint8_t[]> frameBytes{new uint8_t[sizeof(H3DefFrame)]{}};
    H3LoadedDef::DefGroup group = {};
    H3LoadedDef::DefGroup* groupPointer = &group;
    H3DefFrame* framePointer = reinterpret_cast<H3DefFrame*>(frameBytes.get());
    int activeGroup = 1;
    uint32_t nil[5] = {};
    void* nilMapping = nullptr;
    uint32_t oldNil = 0;
    int allocCalls = 0, frees = 0, failAlloc = 0, ctors = 0, dtors = 0;
    int inits = 0, resources = 0, resourceReleases = 0, refs = 0;
    int setCtors = 0, setInserts = 0, setDtors = 0, setAllocations = 0, setFrees = 0, trims = 0;
    uint8_t* oldResourceMap = kObjectResourceMap_;
    const uint8_t* const* oldResourceNilSlot = kObjectResourceNilSlot_;
    uint8_t* oldTextBuffer = kObjectTextBuffer_;
    StackCtorFn_ oldCtor = kStackCtor_;
    StackDtorFn_ oldDtor = kStackDtor_, oldTrim = kDequeTrimTail_;
    StackInitFn_ oldInit = kStackInit_;
    StackResourcesFn_ oldResources = kStackResources_;
    GameAllocFn_ oldAlloc = kGameAlloc_;
    GameFreeFn_ oldFree = kGameFree_;
    SpellSetCtorFn_ oldSetCtor = kSpellSetCtor_;
    SpellSetInsertFn_ oldSetInsert = kSpellSetInsert_;
    SpellSetDtorFn_ oldSetDtor = kSpellSetDtor_;
    ObjectFixture_() {
        CheckAbi_(!objectFixture_, "one native ownership fixture at a time");
        objectFixture_ = this; g_restoreFatal = false;
        kObjectResourceMap_ = nullptr; kObjectResourceNilSlot_ = nullptr; kObjectTextBuffer_ = nullptr;
        if (!Readable_(reinterpret_cast<void*>(0x694FA0), 4)) {
            nilMapping = VirtualAlloc(reinterpret_cast<void*>(0x690000), 0x10000,
                MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            CheckAbi_(nilMapping == reinterpret_cast<void*>(0x690000), "map shared native set nil slot");
        }
        CheckAbi_(!IsBadWritePtr(reinterpret_cast<void*>(0x694FA0), 4), "native set nil slot writable");
        oldNil = *reinterpret_cast<uint32_t*>(0x694FA0);
        *reinterpret_cast<uint32_t*>(0x694FA0) = reinterpret_cast<uint32_t>(nil);
        nil[0] = nil[1] = nil[2] = reinterpret_cast<uint32_t>(nil);
        Def()->groupsCount = 1; Def()->groups = &groupPointer; Def()->activeGroups = &activeGroup;
        group.count = 1; group.frames = &framePointer;
        kStackCtor_ = reinterpret_cast<StackCtorFn_>(&MockStackCtor_);
        kStackDtor_ = reinterpret_cast<StackDtorFn_>(&MockStackDtor_);
        kStackInit_ = reinterpret_cast<StackInitFn_>(&MockStackInit_);
        kStackResources_ = reinterpret_cast<StackResourcesFn_>(&MockStackResources_);
        kGameAlloc_ = &MockGameAlloc_; kGameFree_ = &MockGameFree_;
        kSpellSetCtor_ = reinterpret_cast<SpellSetCtorFn_>(&MockSpellSetCtor_);
        kSpellSetInsert_ = reinterpret_cast<SpellSetInsertFn_>(&MockSpellSetInsert_);
        kSpellSetDtor_ = reinterpret_cast<SpellSetDtorFn_>(&MockSpellSetDtor_);
        kDequeTrimTail_ = reinterpret_cast<StackDtorFn_>(&MockDequeTrimTail_);
        for (int side = 0; side < 2; ++side) {
            MockSpellSetCtor_(Set(side), 0, "", "");
            for (int slot = 0; slot < 20; ++slot) MockStackCtor_(&Mgr()->stacks[side][slot], 0);
            Mgr()->stacks[side][20].type = -1;
        }
    }
    ~ObjectFixture_() {
        CheckAbi_(!g_restoreFatal, "native fixture never enters fatal game path");
        for (int side = 0; side < 2; ++side) {
            for (int slot = 0; slot < 21; ++slot) MockStackDtor_(&Mgr()->stacks[side][slot], 0);
            MockSpellSetDtor_(Set(side), 0);
        }
        CheckAbi_(allocations.empty() && refs == 0 && setAllocations == setFrees,
            "native fixture releases every live allocation and resource");
        kObjectResourceMap_ = oldResourceMap; kObjectResourceNilSlot_ = oldResourceNilSlot; kObjectTextBuffer_ = oldTextBuffer;
        kStackCtor_ = oldCtor; kStackDtor_ = oldDtor; kStackInit_ = oldInit;
        kStackResources_ = oldResources; kGameAlloc_ = oldAlloc; kGameFree_ = oldFree;
        kSpellSetCtor_ = oldSetCtor; kSpellSetInsert_ = oldSetInsert; kSpellSetDtor_ = oldSetDtor;
        kDequeTrimTail_ = oldTrim;
        *reinterpret_cast<uint32_t*>(0x694FA0) = oldNil;
        if (nilMapping) CheckAbi_(VirtualFree(nilMapping, 0, MEM_RELEASE) != FALSE, "unmap test native set nil");
        objectFixture_ = nullptr;
    }
    H3CombatManager* Mgr() { return reinterpret_cast<H3CombatManager*>(manager.get()); }
    H3LoadedDef* Def() { return reinterpret_cast<H3LoadedDef*>(defBytes.get()); }
    uint32_t* Set(int side) { return reinterpret_cast<uint32_t*>(manager.get() + 0x545C + side * 16); }
    void Bind(int side, int slot, const CodecStack& source) {
        H3CombatCreature* stack = &Mgr()->stacks[side][slot];
        MockStackInit_(stack, 0, source.type, source.numberAtStart, Mgr()->hero[side],
            source.side, source.sideIndex, source.position);
        MockStackResources_(stack, 0);
    }
};

static H3CombatCreature* __fastcall MockStackCtor_(H3CombatCreature* stack, int)
{
    ++objectFixture_->ctors;
    memset(stack, 0, sizeof(*stack)); stack->type = -1;
    return stack;
}
static void* __cdecl MockGameAlloc_(unsigned bytes)
{
    ObjectFixture_& f = *objectFixture_;
    if (++f.allocCalls == f.failAlloc) return nullptr;
    void* memory = std::malloc(bytes);
    CheckAbi_(memory != nullptr, "host allocation for native ownership mock");
    f.allocations.push_back({memory, bytes});
    return memory;
}
static void __cdecl MockGameFree_(void* memory)
{
    ObjectFixture_& f = *objectFixture_;
    auto found = std::find_if(f.allocations.begin(), f.allocations.end(),
        [=](const ObjectFixture_::Allocation& item) { return item.address == memory; });
    CheckAbi_(found != f.allocations.end(), "native free belongs to tracked allocation, never double free");
    ++f.frees; f.allocations.erase(found); std::free(memory);
}
static void __fastcall MockStackDtor_(H3CombatCreature* stack, int)
{
    ObjectFixture_& f = *objectFixture_; ++f.dtors;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(stack);
    uint32_t* deque = reinterpret_cast<uint32_t*>(bytes + 0x420);
    if (deque[11]) {
        CheckAbi_(deque[7] > deque[5] && deque[7] < deque[6], "detached deque has no empty tail or boundary end");
        uint32_t** block = reinterpret_cast<uint32_t**>(deque[4]);
        uint32_t** last = reinterpret_cast<uint32_t**>(deque[8]);
        uint32_t* current = reinterpret_cast<uint32_t*>(deque[3]);
        uint32_t* blockEnd = reinterpret_cast<uint32_t*>(deque[2]);
        uint32_t remaining = deque[11];
        while (remaining) {
            ++current; --remaining;
            if (current == blockEnd) {
                MockGameFree_(*block);
                CheckAbi_(block < last && remaining > 0, "native pop_front advances only to nonempty next block");
                ++block; current = *block; blockEnd = current + 1024;
            }
        }
        CheckAbi_(block == last && current == reinterpret_cast<uint32_t*>(deque[7]),
            "native pop_front reaches declared final iterator");
        MockGameFree_(*block);
        MockGameFree_(reinterpret_cast<void*>(deque[9]));
    }
    memset(deque, 0, 48);
    for (int relation = 0; relation < 4; ++relation) {
        uint32_t* header = reinterpret_cast<uint32_t*>(bytes + 0x4F4 + relation * 16);
        if (header[1]) MockGameFree_(reinterpret_cast<void*>(header[1]));
        memset(header, 0, 16);
    }
    if (stack->def) {
        CheckAbi_(stack->def == f.Def() && f.refs > 0, "release mock-owned creature DEF exactly once");
        --f.refs; ++f.resourceReleases; stack->def = nullptr;
    }
}
static void __fastcall MockStackInit_(H3CombatCreature* stack, int, int type, int count,
    H3Hero* hero, int side, int slot, int position)
{
    ObjectFixture_& f = *objectFixture_; ++f.inits;
    CheckAbi_(hero == f.Mgr()->hero[side], "stack init preserves hero argument");
    stack->type = type; stack->numberAtStart = count; stack->numberAlive = count;
    stack->side = side; stack->sideIndex = slot; stack->position = position;
}
static void __fastcall MockStackResources_(H3CombatCreature* stack, int)
{
    ObjectFixture_& f = *objectFixture_; ++f.resources; ++f.refs; stack->def = f.Def();
}
static void* __fastcall MockSpellSetCtor_(void* object, int, const char* left, const char* right)
{
    ObjectFixture_& f = *objectFixture_; ++f.setCtors;
    CheckAbi_(left && right && !*left && !*right, "set constructor receives native zero comparators");
    uint32_t* header = reinterpret_cast<uint32_t*>(object);
    memset(header, 0, 16);
    uint32_t* head = static_cast<uint32_t*>(std::calloc(5, sizeof(uint32_t)));
    CheckAbi_(head != nullptr, "allocate mock VC6 set head"); ++f.setAllocations;
    head[0] = head[1] = head[2] = reinterpret_cast<uint32_t>(f.nil);
    header[1] = reinterpret_cast<uint32_t>(head);
    return object;
}
static void __fastcall MockSpellSetInsert_(void* object, int, void* result, const int* spell)
{
    ObjectFixture_& f = *objectFixture_; ++f.setInserts;
    CheckAbi_(result && spell && *spell >= 0 && *spell < 81, "native set insertion ABI");
    uint32_t* header = reinterpret_cast<uint32_t*>(object);
    uint32_t* head = reinterpret_cast<uint32_t*>(header[1]);
    uint32_t* parent = head;
    uint32_t* link = &head[1];
    while (reinterpret_cast<uint32_t*>(*link) != f.nil) {
        parent = reinterpret_cast<uint32_t*>(*link);
        if (static_cast<int32_t>(parent[3]) == *spell) return;
        link = &parent[*spell < static_cast<int32_t>(parent[3]) ? 0 : 2];
    }
    uint32_t* node = static_cast<uint32_t*>(std::calloc(5, sizeof(uint32_t)));
    CheckAbi_(node != nullptr, "allocate mock VC6 set node"); ++f.setAllocations;
    node[0] = node[2] = reinterpret_cast<uint32_t>(f.nil);
    node[1] = reinterpret_cast<uint32_t>(parent); node[3] = static_cast<uint32_t>(*spell);
    *link = reinterpret_cast<uint32_t>(node); ++header[3];
    if (header[3] == 1 || *spell < static_cast<int32_t>(reinterpret_cast<uint32_t*>(head[0])[3])) head[0] = *link;
    if (header[3] == 1 || *spell > static_cast<int32_t>(reinterpret_cast<uint32_t*>(head[2])[3])) head[2] = *link;
    reinterpret_cast<uint32_t*>(result)[0] = *link;
    reinterpret_cast<uint32_t*>(result)[1] = 1;
}
static void __fastcall MockSpellSetDtor_(void* object, int)
{
    ObjectFixture_& f = *objectFixture_; ++f.setDtors;
    uint32_t* header = reinterpret_cast<uint32_t*>(object);
    uint32_t* head = reinterpret_cast<uint32_t*>(header[1]);
    CheckAbi_(head && Readable_(head, 20), "set destructor owns readable native head");
    std::vector<uint32_t*> pending;
    if (reinterpret_cast<uint32_t*>(head[1]) != f.nil) pending.push_back(reinterpret_cast<uint32_t*>(head[1]));
    while (!pending.empty()) {
        uint32_t* node = pending.back(); pending.pop_back();
        for (int child : {0, 2})
            if (reinterpret_cast<uint32_t*>(node[child]) != f.nil) pending.push_back(reinterpret_cast<uint32_t*>(node[child]));
        std::free(node); ++f.setFrees;
    }
    std::free(head); ++f.setFrees; memset(header, 0, 16);
}
static void __fastcall MockDequeTrimTail_(H3CombatCreature*, int)
{
    ++objectFixture_->trims;
    CheckAbi_(false, "prepared deque must never need native empty-tail trim");
}
static CodecStack ObjectSavedStack_(int side, int slot, int type, int position)
{
    CodecStack saved = {};
    saved.occupied = true; saved.side = side; saved.sideIndex = slot; saved.type = type;
    saved.numberAtStart = saved.numberAlive = 10; saved.position = position; saved.infoCombat[0] = 20;
    return saved;
}
static void TestPreparedDeque_()
{
    {
        ObjectFixture_ f;
        std::vector<int32_t> read;
        CheckAbi_(ReadSpellSet_(reinterpret_cast<const uint8_t*>(f.Set(0)), &read) && read.empty()
            && ReadSpellSet_(reinterpret_cast<const uint8_t*>(f.Set(1)), &read) && read.empty(),
            "both native empty set headers own readable 20-byte heads");
        const int values[] = {40, 5, 80, 0, 7, 5};
        uint32_t result[2] = {};
        for (const int& value : values) MockSpellSetInsert_(f.Set(0), 0, result, &value);
        const std::vector<int32_t> expected = {0, 5, 7, 40, 80};
        CheckAbi_(ReadSpellSet_(reinterpret_cast<const uint8_t*>(f.Set(0)), &read) && read == expected
            && f.Set(0)[3] == 5, "VC6 set unordered insert/duplicate yields sorted unique native readback");
    }
    const unsigned sizes[] = {0, 1, 512, 1023, 1024, 1025, 2048};
    for (unsigned size : sizes) {
        ObjectFixture_ f;
        std::unique_ptr<uint8_t[]> stack(new uint8_t[sizeof(H3CombatCreature)]{});
        MockStackCtor_(reinterpret_cast<H3CombatCreature*>(stack.get()), 0);
        std::vector<int32_t> values;
        for (unsigned i = 0; i < size; ++i) values.push_back(static_cast<int32_t>(i % 81));
        RestoreObjects_ objects; std::string error; std::vector<int32_t> read;
        CheckAbi_(objects.PrepareDeque(stack.get(), values, &error), "prepare deque boundary size");
        const uint32_t* header = reinterpret_cast<const uint32_t*>(stack.get() + 0x420);
        CheckAbi_(ReadDequeInts_(stack.get() + 0x420, &read) && read == values, "production deque multiblock readback");
        if (size) {
            const unsigned offset = size % 1024 ? 0 : 1;
            const unsigned blocks = (size + offset + 1023) / 1024;
            CheckAbi_(header[3] == header[1] + offset * 4 && header[10] == blocks + 2
                && header[11] == size && header[7] > header[5] && header[7] < header[6],
                "exact multiples begin+4, end within occupied tail and map bounds");
            CheckAbi_(f.allocCalls == static_cast<int>(blocks + 1), "one deque map plus occupied blocks only");
        }
        else CheckAbi_(f.allocCalls == 0 && header[11] == 0, "empty deque allocates nothing");
        CheckAbi_(ObjectStackDestroySeh_(reinterpret_cast<H3CombatCreature*>(stack.get()))
            && f.allocations.empty() && f.frees == f.allocCalls && f.trims == 0,
            "native deque destruction releases every block/map without trim");
    }
    for (int fail = 1; fail <= 4; ++fail) {
        ObjectFixture_ f; f.failAlloc = fail;
        std::unique_ptr<uint8_t[]> stack(new uint8_t[sizeof(H3CombatCreature)]{});
        std::vector<int32_t> values(2048, 5); RestoreObjects_ objects; std::string error;
        CheckAbi_(!objects.PrepareDeque(stack.get(), values, &error) && !error.empty() && !g_restoreFatal,
            "deque allocation failure is clean rejection");
        CheckAbi_(f.allocCalls == fail && f.frees == fail - 1 && f.allocations.empty()
            && reinterpret_cast<uint32_t*>(stack.get() + 0x420)[11] == 0,
            "failed deque releases earlier blocks/map and leaves empty header");
    }
    std::printf("PASS native deque: 0/1/512/1023/1024/1025/2048, exact-multiple begin+4, multiblock readback, allocation failure cleanup\n");
}
static void TestLogLevelList_()
{
    // Persistent single-select list: status box left of the key box; option rows
    // sit below the bar, right of the save list — rectangles must never overlap.
    CheckAbi_(kUiLogLevelX >= kUiListWidth, "log rows start right of save list");
    CheckAbi_(kUiLogLevelX + kUiLogLevelWidth - 2 < kUiBarWidth - 58,
        "status box never overlaps the key box");
    CheckAbi_(kUiLogLevelRows == 5, "five levels trace..error");
    for (int i = 0; i < 5; ++i)
        CheckAbi_(strcmp(kUiLogLevelNames_[i], LogLevelName_(i)) == 0,
            "list labels match config level names");
    const int savedX = g_ui.x, savedY = g_ui.y;
    const int savedHover = g_ui.logLevelHover;
    g_ui.x = 8; g_ui.y = 8;
    CheckAbi_(UiHitLogLevelTrigger_(8 + kUiLogLevelX, 8 + 10), "status box hit inside its rect");
    CheckAbi_(!UiHitLogLevelTrigger_(8 + kUiLogLevelX - 1, 8 + 10), "status box left edge exclusive");
    CheckAbi_(!UiHitLogLevelTrigger_(8 + kUiBarWidth - 58, 8 + 10), "key box is not the status box");
    CheckAbi_(UiHitLogLevelItem_(8 + kUiLogLevelX + 3, 8 + kUiBarHeight + 2) == 0,
        "first option row hit");
    CheckAbi_(UiHitLogLevelItem_(8 + kUiLogLevelX + 3,
        8 + kUiBarHeight + 4 * kUiLogLevelRowHeight + 1) == 4, "last option row hit");
    CheckAbi_(UiHitLogLevelItem_(8 + kUiLogLevelX + 3,
        8 + kUiBarHeight + kUiLogLevelRows * kUiLogLevelRowHeight) == -1,
        "below the option list misses");
    CheckAbi_(UiHitLogLevelItem_(8 + 3, 8 + kUiBarHeight + 2) == -1,
        "save list area is not a log item hit");
    // The list is always visible: rows are reachable without any open state.
    CheckAbi_(UiHitLogLevelItem_(8 + kUiLogLevelX + 3, 8 + kUiBarHeight + 2) == 0,
        "hover poll reaches rows without open flag");
    // Selection must persist through the user ini, not the shipped default.
    g_log_level = LOG_INFO;
    CheckAbi_(SaveLogLevel_(LOG_WARN) && g_log_level == LOG_WARN,
        "saving a level applies it immediately");
    char value[16] = {};
    IniReadUtf8(g_user_ini_path, "Logging", "MinLevel", "", value, (int)sizeof(value));
    CheckAbi_(strcmp(value, "warn") == 0, "user ini stores the selected level");
    CheckAbi_(SaveLogLevel_(LOG_INFO) && g_log_level == LOG_INFO, "restore info default");
    IniWriteKeyUtf8(g_user_ini_path, "Logging", "MinLevel", "info");
    g_ui.x = savedX; g_ui.y = savedY; g_ui.logLevelHover = savedHover;
    std::printf("PASS log level list: always-visible rows, disjoint hit areas, user ini persistence\n");
}
static void TestObjectSwitch_()
{
    for (int rollback = 0; rollback < 2; ++rollback) {
        ObjectFixture_ f;
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        before->stacks[0][0] = ObjectSavedStack_(0, 0, 45, 22);
        before->stacks[1][1] = ObjectSavedStack_(1, 1, 46, 40);
        before->stacks[1][3] = ObjectSavedStack_(1, 3, 49, 80);
        f.Bind(0, 0, before->stacks[0][0]); f.Bind(1, 1, before->stacks[1][1]);
        f.Bind(1, 3, before->stacks[1][3]);
        *target = *before;
        target->stacks[1][3] = CodecStack{};
        target->stacks[0][0].type = 47;
        target->stacks[0][2] = ObjectSavedStack_(0, 2, 48, 60);
        target->stacks[0][0].spellIds.assign(2048, 5);
        target->stacks[1][1].spellIds = {7, 8};
        target->stacks[0][0].relations[0] = {{1, 1}, {0, 2}};
        target->stacks[1][1].relations[3] = {{0, 0}};
        target->stacks[0][20].spellIds = {4, 5};
        target->stacks[0][20].relations[1] = {{0, 0}};
        target->eagleEye[0] = {0, 5, 80}; target->eagleEye[1] = {7};
        RestoreObjects_ oldContainers; std::string error;
        CheckAbi_(oldContainers.PrepareDeque(reinterpret_cast<uint8_t*>(&f.Mgr()->stacks[0][0]), {3, 4}, &error)
            && oldContainers.PrepareRelations(reinterpret_cast<uint8_t*>(&f.Mgr()->stacks[0][0]), f.Mgr(),
                target->stacks[1][1], &error), "seed old detached ownership");
        CheckAbi_(oldContainers.PrepareDeque(reinterpret_cast<uint8_t*>(&f.Mgr()->stacks[0][20]), {9}, &error),
            "seed reserved slot old container ownership");
        int oldSpell = 12; uint32_t insertResult[2] = {};
        MockSpellSetInsert_(f.Set(0), 0, insertResult, &oldSpell);
        std::vector<uint8_t> original(f.manager.get(), f.manager.get() + sizeof(H3CombatManager));
        const std::vector<ObjectFixture_::Allocation> oldAllocations = f.allocations;
        const int oldSetFrees = f.setFrees, oldResourceReleases = f.resourceReleases;
        RestoreObjects_ objects;
        CheckAbi_(objects.Prepare(f.Mgr(), *before, *target, &error), "prepare changed type/summon/containers/sets");
        CheckAbi_(objects.slots[0][0].replace && objects.slots[0][2].replace
            && objects.slots[1][3].replace && !objects.slots[1][1].replace,
            "only changed topology reconstructs native resources");
        CheckAbi_(f.ctors == 82 && f.setCtors == 4 && f.inits == 5
            && f.resources == 5 && f.refs == 5 && f.setInserts == 5,
            "constructor/init/resources/set insertion counts include prepared ownership");
        const int allocationCalls = f.allocCalls;
        objects.Switch(f.Mgr());
        CheckAbi_(objects.switched && f.allocCalls == allocationCalls, "switch has no game allocations");
        CheckAbi_(f.Mgr()->stacks[0][0].type == 47 && f.Mgr()->stacks[0][2].type == 48
            && f.Mgr()->stacks[1][3].type == -1, "switch installs replacement/summon and removes old stack");
        std::vector<int32_t> spells;
        CheckAbi_(ReadDequeInts_(reinterpret_cast<uint8_t*>(&f.Mgr()->stacks[0][0]) + 0x420, &spells)
            && spells == target->stacks[0][0].spellIds, "switched multiblock deque is readable in live slot");
        CheckAbi_(ReadDequeInts_(reinterpret_cast<uint8_t*>(&f.Mgr()->stacks[0][20]) + 0x420, &spells)
            && spells == target->stacks[0][20].spellIds && !objects.slots[0][20].replace,
            "reserved slot swaps containers without combat resource initialization");
        CheckAbi_(ReadSpellSet_(reinterpret_cast<uint8_t*>(f.Set(0)), &spells)
            && spells == target->eagleEye[0], "switched VC6 set nonempty readback");
        CheckAbi_(ReadSpellSet_(reinterpret_cast<uint8_t*>(f.Set(1)), &spells)
            && spells == target->eagleEye[1], "second side VC6 set readback");
        const auto& relations = *reinterpret_cast<const H3Vector<H3CombatCreature*>*>(
            reinterpret_cast<uint8_t*>(&f.Mgr()->stacks[0][0]) + 0x4F4);
        std::vector<CodecIdentity> identities;
        CheckAbi_(ReadPointerRelations_(f.Mgr(), relations, &identities) && identities.size() == 2
            && identities[0].side == 1 && identities[0].slot == 1 && identities[1].slot == 2,
            "switched relations target manager identities");
        CheckAbi_(relations[0] == &f.Mgr()->stacks[1][1] && relations[1] == &f.Mgr()->stacks[0][2]
            && relations[0] != reinterpret_cast<H3CombatCreature*>(objects.slots[1][1].bytes.get())
            && relations[1] != reinterpret_cast<H3CombatCreature*>(objects.slots[0][2].bytes.get()),
            "relation pointers never target prepared detached stack addresses");
        if (rollback) {
            objects.Switch(f.Mgr());
            CheckAbi_(!objects.switched && memcmp(original.data(), f.manager.get(), original.size()) == 0,
                "second switch restores every manager byte and original ownership");
        }
        CheckAbi_(objects.Release(&error) && objects.Release(&error)
            && f.dtors == 42 && f.setDtors == 2, "detached ownership release is idempotent with exact native destructor counts");
        if (rollback) {
            CheckAbi_(f.allocations.size() == oldAllocations.size() && f.refs == 3
                && f.resourceReleases == oldResourceReleases + 2, "rollback releases prepared resources only");
            for (const auto& allocation : oldAllocations)
                CheckAbi_(std::any_of(f.allocations.begin(), f.allocations.end(),
                    [&](const ObjectFixture_::Allocation& live) { return live.address == allocation.address; }),
                    "rollback preserves every original allocation");
        }
        else {
            for (const auto& allocation : oldAllocations)
                CheckAbi_(std::none_of(f.allocations.begin(), f.allocations.end(),
                    [&](const ObjectFixture_::Allocation& live) { return live.address == allocation.address; }),
                    "commit releases every old deque/map/relation allocation");
            CheckAbi_(f.refs == 3 && f.resourceReleases == oldResourceReleases + 2
                && f.setFrees == oldSetFrees + 3, "commit releases old replaced/removed DEFs and both old set heads/nodes");
        }
        CheckAbi_(f.trims == 0, "prepared ownership never calls absolute native tail trim");
    }
    for (int fail = 1; fail <= 6; ++fail) {
        ObjectFixture_ f; f.failAlloc = fail;
        std::unique_ptr<CodecCapture> before(new CodecCapture{}), target(new CodecCapture{});
        target->stacks[0][0] = ObjectSavedStack_(0, 0, 45, 22);
        target->stacks[0][0].spellIds.assign(2048, 5);
        target->stacks[0][0].relations[0] = {{0, 0}};
        target->stacks[0][0].relations[1] = {{0, 0}};
        target->eagleEye[0] = {5};
        RestoreObjects_ objects; std::string error;
        CheckAbi_(!objects.Prepare(f.Mgr(), *before, *target, &error) && !g_restoreFatal
            && objects.Release(&error), "partial object preparation allocation failure releases detached ownership");
        CheckAbi_(f.allocCalls == fail && f.frees == fail - 1 && f.allocations.empty() && f.refs == 0,
            "partial deque/relation allocation failure returns all previous allocations and DEFs");
    }
    std::printf("PASS native ownership: stack ctor/init/resources/dtor, VC6 set insert/readback, manager-address relations, allocation cleanup, double-switch byte rollback, old ownership release\n");
}

int main()
{
    wchar_t* path = new (std::nothrow) wchar_t[kPathCap_ / 2]();
    CheckAbi_(path != nullptr, "allocate patcher path");
    const DWORD length = GetEnvironmentVariableW(L"H3BATTLE_PATCHER_DLL", path, kPathCap_ / 2);
    CheckAbi_(length > 0 && length < kPathCap_ / 2, "H3BATTLE_PATCHER_DLL required");
    CheckAbi_((length >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
        || (length >= 2 && path[0] == L'\\' && path[1] == L'\\'), "patcher path must be absolute");
    HMODULE patcher = LoadLibraryW(path);
    if (!patcher) std::fprintf(stderr, "LoadLibrary error=%lu\n", GetLastError());
    CheckAbi_(patcher != nullptr, "load exact installed patcher");
    delete[] path;
    // Resolve from the exact module without a second basename DLL search.
    typedef Patcher* (__stdcall* GetPatcherFn)();
    GetPatcherFn getPatcher = reinterpret_cast<GetPatcherFn>(GetProcAddress(patcher, "_GetPatcherX86@0"));
    CheckAbi_(getPatcher != nullptr, "GetPatcher export");
    _P = getPatcher();
    CheckAbi_(_P != nullptr, "GetPatcher");
    _PI = _P->CreateInstance("H3BattleStore.RealHookAbiTests");
    CheckAbi_(_PI != nullptr, "create ABI test patcher instance");
    g_disable_log = true;
    g_restoreFatal = false;
    TestRestoreFrames_();
    TestFingerprint_();
    TestObstacles_();
    TestObstacleTransaction_();
    TestSiegeTransaction_();
    TestRestoreManagerScalars_();
    TestPreparationSideEffects_();
    TestRestoreRngGuard_();
    TestRestoreReasonZh_();
    TestPreparedDeque_();
    TestObjectSwitch_();
    TestLogLevelList_();
    HiHook* executor = _PI->WriteHiHook(reinterpret_cast<UINT32>(&MockExecute_),
        SPLICE_, EXTENDED_, THISCALL_, Hook_BattleExecute_);
    CheckAbi_(executor != nullptr, "install real production executor");
    CheckAbi_(_PI->WriteHiHook(reinterpret_cast<UINT32>(&MockStart_), SPLICE_,
        EXTENDED_, THISCALL_, Hook_BattleStart_) != nullptr, "install real production start");
    CheckAbi_(_PI->WriteHiHook(reinterpret_cast<UINT32>(&MockStop_), SPLICE_,
        EXTENDED_, THISCALL_, Hook_BattleStop_) != nullptr, "install real production stop");
    CheckAbi_(_PI->WriteHiHook(reinterpret_cast<UINT32>(&MockCycle_), SPLICE_,
        EXTENDED_, THISCALL_, Hook_CycleCombatScreen_) != nullptr, "install real production cycle");
    CheckAbi_(_PI->WriteHiHook(reinterpret_cast<UINT32>(&MockMessage_), SPLICE_,
        EXTENDED_, THISCALL_, Hook_CombatMessage_) != nullptr, "install real production message");
    TestExecutor_(executor);
    TestLifecycle_();
    TestCycleAndMessage_();
    std::printf("PASS Hook ABI: actual patcher, included production callbacks; no deployment\n");
    return 0;
}
