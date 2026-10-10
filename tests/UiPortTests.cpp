// UiPortTests.cpp
// NullUi 契约测试：真实 NullUi 编制单元（经 IBattleStoreUi 调用）+ 最小 H3API 类型 stub。
// 不链接 H3API、不载入游戏、不依赖图形资源或 WinAPI 调用。
//
// 边界声明（这是契约测试，不是生产服务恢复测试）：
//   1. 只验证 NullUi 如何驱动服务与记录日志（副作用顺序、服务 call counts、传参），
//      不验证 StoreReloadList_/StoreMaintainRestore_/StoreConsumeRestore_ 的真实领域行为；
//      这三个符号由本测试文件的 Fake 服务替身提供。
//   2. 不验证界面绘制、悬停布局、改键交互与吞并策略——那些属于 HdNativeUi 契约。
//   3. 不验证恢复成功链路（指纹校验、档案定位、解码与写入）：无界面时 NullUi 必须
//      fail closed，本测试断言它“不确认、不执行恢复”，而不是断言恢复结果。

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <string>
#include <utility>
#include <vector>
#include <type_traits>

// ---- 最小 stub：契约只需要这些未定义的游戏类型 ----
namespace h3
{
struct H3CombatManager {};
struct H3Msg {};
}

// BattleUiPort.hpp 的接口刻意使用未限定的 H3CombatManager/H3Msg；生产 TU 同样是先
// using namespace h3 再 include 本头文件，这里保持相同顺序。
using namespace h3;
#include "../modules/BattleUiPort.hpp"

struct StoreEntry
{
    uint64_t timestampUtcMs;
    uint32_t sequence;
    uint32_t filenameAttempt;
    std::wstring path;
};

// 日志记录（捕获 LogInfo/LogWarn 的真实调用与顺序；不写盘、不依赖游戏日志系统）。
struct LogRecord
{
    char level;         // 'I' = info, 'W' = warn
    std::string text;   // 已按生产 printf 风格格式化后的完整文本
};

// Fake 服务替身：只记录调用，不实现领域逻辑（领域行为由 BattleStoreService 自身测试覆盖）。
struct FakeState
{
    // ---- StoreReloadList_ ----
    int reloadCalls = 0;
    const H3CombatManager* reloadMgr = nullptr;
    bool reloadResult = false;

    // ---- StoreMaintainRestore_ ----
    int maintainCalls = 0;
    H3CombatManager* maintainMgr = nullptr;
    bool maintainResult = false;

    // ---- StoreConsumeRestore_ ----
    int consumeCalls = 0;
    H3CombatManager* consumeMgr = nullptr;
    int consumeResult = 0;
    StoreEntry* consumeEntry = nullptr;
    unsigned* consumeGeneration = nullptr;
    std::string* consumeExpectedKey = nullptr;
    std::string consumedExpectedKey;
    bool consumeReturn = false;
    bool consumeQueuePending = false;   // Fake 侧模拟“服务里仍有排队请求”
    uint64_t consumedStamp = 0;
    unsigned consumedGeneration = 0;

    std::vector<std::string> serviceCalls;
    std::vector<LogRecord> logs;
};

static FakeState g_fake = {};

static void ResetFake_()
{
    g_fake = FakeState{};
}

static void AppendLog_(char level, const char* fmt, va_list args)
{
    char text[2048] = {};
    const int n = vsnprintf_s(text, sizeof(text), _TRUNCATE, fmt, args);
    if (n < 0) std::strncpy(text, "<log formatting failed>", sizeof(text) - 1);
    g_fake.logs.push_back({level, text});
}

// 与生产签名一致（ConfigLog.inc.cpp: static void LogInfo(const char* fmt, ...);）。
static void LogInfo(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    AppendLog_('I', fmt, args);
    va_end(args);
}

static void LogWarn(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    AppendLog_('W', fmt, args);
    va_end(args);
}

static bool StoreReloadList_(const H3CombatManager* mgr)
{
    ++g_fake.reloadCalls;
    g_fake.reloadMgr = mgr;
    g_fake.serviceCalls.push_back("reload");
    return g_fake.reloadResult;
}

static bool StoreMaintainRestore_(H3CombatManager* mgr)
{
    ++g_fake.maintainCalls;
    g_fake.maintainMgr = mgr;
    g_fake.serviceCalls.push_back("maintain");
    return g_fake.maintainResult;
}

// 契约上“ready”意味着：生产服务会在返回前把请求出队，并把快照写出到三个出参。
// Fake 按同样形状写快照，用来证明 NullUi 确实把出参交给了服务，并且没有任何
// 后续动作消费这些快照（无确认、无执行）。
static bool StoreConsumeRestore_(H3CombatManager* mgr, int result, StoreEntry* entry,
    unsigned* generation, std::string* expectedKey)
{
    ++g_fake.consumeCalls;
    g_fake.consumeMgr = mgr;
    g_fake.consumeResult = result;
    g_fake.consumeEntry = entry;
    g_fake.consumeGeneration = generation;
    g_fake.consumeExpectedKey = expectedKey;
    g_fake.consumedExpectedKey = expectedKey ? *expectedKey : "<null>";
    g_fake.serviceCalls.push_back("consume");
    if (g_fake.consumeReturn) {
        if (entry) entry->timestampUtcMs = 0x1234;
        if (generation) *generation = 77;
        if (expectedKey) *expectedKey = "fake-output-key";
        g_fake.consumedStamp = entry ? entry->timestampUtcMs : 0;
        g_fake.consumedGeneration = generation ? *generation : 0;
        g_fake.consumeQueuePending = false;   // 服务出队
    }
    return g_fake.consumeReturn;
}

#include "../modules/NullUiPort.inc.cpp"

static int g_failed = 0;
static int g_checks = 0;

static void Expect_(bool condition, const char* name)
{
    ++g_checks;
    if (!condition) {
        printf("FAIL %s\n", name);
        ++g_failed;
    }
}

static bool LogIs_(size_t index, char level, const char* text)
{
    return index < g_fake.logs.size() && g_fake.logs[index].level == level
        && g_fake.logs[index].text == text;
}

static bool ServiceSequenceIs_(const char* const* expected, size_t count)
{
    if (g_fake.serviceCalls.size() != count) return false;
    for (size_t i = 0; i < count; ++i)
        if (g_fake.serviceCalls[i] != expected[i]) return false;
    return true;
}

// ---- 测试1：入口全局端口、POD 事件与“不接管输入” ----
static void TestPortAndInputPassThrough_()
{
    ResetFake_();
    H3CombatManager mgr;
    H3Msg msg;

    // Port 头文件里的 static_assert 必须在本 TU 生效：加一条等价的本地断言作为定位标识。
    static_assert(std::is_trivial<UiKeyEvent_>::value
        && std::is_standard_layout<UiKeyEvent_>::value,
        "BattleUiPort.hpp POD assertion for UiKeyEvent_ is active in this TU");
    static_assert(std::is_trivial<UiMouseEvent_>::value
        && std::is_standard_layout<UiMouseEvent_>::value,
        "BattleUiPort.hpp POD assertion for UiMouseEvent_ is active in this TU");
    static_assert(std::is_abstract<IBattleStoreUi>::value,
        "the port stays an interface: no implementation may be selected here");

    // 装机端口确实是 NullUi，并且契约与具体实例无关（另建一个实例同行为）。
    IBattleStoreUi* ui = g_uiPort;
    Expect_(ui != nullptr && dynamic_cast<NullUi*>(ui) == &g_nullUi,
        "installed port is the NullUi instance, not a stub or null");
    NullUi second;
    IBattleStoreUi* other = &second;

    // POD 事件：可零初始化、可 sizeof/offsetof 布局稳定、可整块 memcpy（SEH 边界要求）。
    const UiKeyEvent_ zeroKey = {};
    Expect_(zeroKey.vk == 0 && !zeroKey.down && !zeroKey.up && !zeroKey.repeat
        && zeroKey.source == 0, "UiKeyEvent_ zero initializes every field");
    const UiMouseEvent_ zeroMouse = {};
    Expect_(zeroMouse.kind == 0 && zeroMouse.gameX == 0 && zeroMouse.gameY == 0
        && zeroMouse.wheelDelta == 0, "UiMouseEvent_ zero initializes every field");
    Expect_(offsetof(UiKeyEvent_, vk) == 0 && offsetof(UiKeyEvent_, source) == 8
        && offsetof(UiMouseEvent_, wheelDelta) == 12,
        "POD event layout stays field-for-field stable for hooks");
    Expect_(std::is_trivially_copyable<UiKeyEvent_>::value
        && std::is_trivially_copyable<UiMouseEvent_>::value
        && std::is_trivially_destructible<UiKeyEvent_>::value
        && std::is_trivially_destructible<UiMouseEvent_>::value,
        "POD events stay trivially copyable and destructible");
    UiKeyEvent_ key = {65, true, false, true, 1};
    char rawKey[sizeof(UiKeyEvent_)] = {};
    std::memcpy(rawKey, &key, sizeof(key));
    UiKeyEvent_ copiedKey = {};
    std::memcpy(&copiedKey, rawKey, sizeof(copiedKey));
    const bool keyRoundTrips = std::memcmp(&key, &copiedKey, sizeof(key)) == 0;

    ui->Initialize();
    Expect_(g_fake.logs.size() == 1 && LogIs_(0, 'I', "[NullUi] initialized"),
        "Initialize records the NullUi lifecycle event");

    // 系统键：down/up/repeat 的 8 种组合 + 两种 source，一律不透吞。
    for (int mask = 0; mask != 8; ++mask) {
        const UiKeyEvent_ event = {65, (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0, mask % 2};
        const bool passed = !ui->OnSystemKey(event) && !other->OnSystemKey(event);
        Expect_(passed && keyRoundTrips, "every system key down/up/repeat source combination passes through");
    }
    // 鼠标：kind 0..5 × 战斗开关，一律 false。
    for (int kind = 0; kind != 6; ++kind) {
        const UiMouseEvent_ event = {kind, 100 + kind, 200 - kind, kind == 5 ? -120 : 0};
        Expect_(!ui->OnSystemMouse(event, false) && !ui->OnSystemMouse(event, true)
            && !other->OnSystemMouse(event, true),
            "each mouse kind passes through with combat open or closed");
    }
    Expect_(!ui->HitBar(&msg, false) && !ui->HitBar(&msg, true)
        && !ui->OnGameMouse(&msg) && !other->OnGameMouse(&msg),
        "HitBar and GameMouse never consume");

    // 帧键轮询 / FrameClick / Draw / 改键轮询：不调用任何服务保存，也不写日志。
    ui->Draw(nullptr);
    ui->Draw(&mgr);
    ui->PollHover();
    ui->FrameClick();
    ui->PollRebindKey();
    ui->OnGameKeyBefore(&msg, 3);
    ui->OnGameKeyAfter(&mgr, &msg, 2, 1234, true);
    ui->OnFrameKeyPoll(&mgr, 5678, false);
    ui->OnListReloaded();
    ui->CancelRebind("test");
    Expect_(g_fake.serviceCalls.empty() && g_fake.logs.size() == 1,
        "draw/frame/rebind/game callbacks call no service and emit no log");
    Expect_(!ui->IsRebindWaiting() && ui->RebindLatchKey() == 0
        && !other->IsRebindWaiting() && other->RebindLatchKey() == 0,
        "NullUi reports no rebind wait and a zero latch");
}

// ---- 测试2：列表委托、恢复超时提示与 fail-closed 拒绝 ----
static void TestReloadAndRestore_()
{
    ResetFake_();
    H3CombatManager mgr;
    IBattleStoreUi* ui = g_uiPort;

    g_fake.reloadResult = true;
    Expect_(ui->ReloadEntries(&mgr), "ReloadEntries propagates service success");
    Expect_(g_fake.reloadCalls == 1 && g_fake.reloadMgr == &mgr,
        "ReloadEntries passes the original manager to the service");
    g_fake.reloadResult = false;
    Expect_(!ui->ReloadEntries(nullptr), "ReloadEntries propagates service failure");
    Expect_(g_fake.reloadCalls == 2 && g_fake.reloadMgr == nullptr,
        "ReloadEntries preserves a null manager argument");
    Expect_(ServiceSequenceIs_(kSeq{"reload", "reload"}, 2),
        "only the reload service runs during list refresh");

    ResetFake_();
    g_fake.consumeQueuePending = false;
    g_fake.maintainResult = false;
    ui->MaintainRestore(&mgr);
    Expect_(g_fake.maintainCalls == 1 && g_fake.maintainMgr == &mgr
        && ServiceSequenceIs_(kSeq{"maintain"}, 1) && g_fake.logs.empty(),
        "non-expired restore maintenance makes no timeout warning");

    g_fake.maintainResult = true;
    ui->MaintainRestore(&mgr);
    Expect_(g_fake.maintainCalls == 2 && g_fake.logs.size() == 1
        && LogIs_(0, 'W', "[NullUi] restore request expired; ui could not display timeout notice"),
        "only an expired restore request produces the timeout warning");

    // 没有 pending 请求：服务返回 false，界面静默（没有提示、没有后续动作）。
    ResetFake_();
    g_fake.consumeReturn = false;
    g_fake.consumeQueuePending = false;
    ui->ProcessRestore(&mgr, 41);
    Expect_(g_fake.consumeCalls == 1 && g_fake.consumeMgr == &mgr
        && g_fake.consumeResult == 41 && g_fake.logs.empty()
        && !g_fake.consumeQueuePending,
        "a missing restore request is a silent no-op without prompting");

    // 请求已就绪：服务出队并交出快照，NullUi 只丢弃并告警，不确认、不执行恢复。
    ResetFake_();
    g_fake.consumeReturn = true;
    g_fake.consumeQueuePending = true;
    ui->ProcessRestore(&mgr, -7);
    Expect_(g_fake.consumeCalls == 1 && g_fake.consumeMgr == &mgr
        && g_fake.consumeResult == -7 && g_fake.consumeEntry != nullptr
        && g_fake.consumeGeneration != nullptr && g_fake.consumeExpectedKey != nullptr
        && !g_fake.consumeQueuePending && g_fake.logs.size() == 1
        && LogIs_(0, 'W', "[NullUi] restore request cannot be confirmed without UI; request discarded"),
        "a ready restore request is discarded with one fail-closed warning");
    Expect_(g_fake.consumedStamp == 0x1234 && g_fake.consumedGeneration == 77
        && g_fake.consumeEntry != nullptr && g_fake.consumeEntry->timestampUtcMs == 0x1234
        && g_fake.consumeGeneration != nullptr && *g_fake.consumeGeneration == 77
        && g_fake.consumeExpectedKey != nullptr
        && *g_fake.consumeExpectedKey == "fake-output-key",
        "the snapshot handed over by the service is never executed or mutated");
    Expect_(ServiceSequenceIs_(kSeq{"consume"}, 1),
        "restore processing only consumes the request; no confirm or execute service runs");
}

// ---- 测试3：保存/提示日志与生命周期通知 ----
static void TestNotifications_()
{
    ResetFake_();
    IBattleStoreUi* ui = g_uiPort;
    const uint64_t timestamp = UINT64_C(0x0123456789abcdef);
    const std::string notice = u8"保存失败：档案不存在";
    char expected[96] = {};
    _snprintf_s(expected, _TRUNCATE, "[NullUi] saved timestamp_utc_ms=%llu",
        static_cast<unsigned long long>(timestamp));

    ui->MarkSaved(timestamp);
    ui->MarkNotice(notice.c_str());
    ui->MarkNotice(nullptr);
    ui->OnBattleReset();
    ui->OnFaultCleanup();

    Expect_(g_fake.logs.size() == 5 && g_fake.serviceCalls.empty(),
        "notifications only log, in call order, without touching services");
    Expect_(LogIs_(0, 'I', expected), "MarkSaved preserves the full UTC timestamp");
    Expect_(LogIs_(1, 'I', (std::string("[NullUi] notice logged: utf8=") + notice).c_str()),
        "MarkNotice preserves UTF-8 bytes");
    Expect_(LogIs_(2, 'I', "[NullUi] notice logged: utf8="),
        "MarkNotice accepts null safely");
    Expect_(LogIs_(3, 'I', "[NullUi] battle state reset")
        && LogIs_(4, 'I', "[NullUi] fault cleanup acknowledged"),
        "battle reset and fault cleanup notify in call order");
}

int main(int argc, char** argv)
{
    // runner 会传 fixture 目录参数；本测试不使用文件系统，容许其可选存在。
    (void)argv;
    Expect_(argc >= 1, "runner passes no required argument to this contract test");
    TestPortAndInputPassThrough_();
    TestReloadAndRestore_();
    TestNotifications_();
    if (g_failed == 0) printf("PASS %d checks: NullUi contract (no game, no H3API, no UI resources, no WinAPI)\n", g_checks);
    else printf("FAILED %d of %d NullUi contract checks\n", g_failed, g_checks);
    return g_failed == 0 ? 0 : 1;
}
