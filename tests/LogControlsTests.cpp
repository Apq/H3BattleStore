#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cwchar>
#include <new>
#include <string>
#include "../modules/IniUtf8.inc.cpp"
#include "../modules/ConfigLog.inc.cpp"
#include "../modules/LogContext.inc.cpp"
#define H3_GUARD_CONTEXT_(reason) LogRecentContext_(reason)
#include "GuardLogRegression.hpp"
#include "../modules/BattleInputPolicy.hpp"
#include "UiLayoutRegression.hpp"

// Exercise the real CF_HDROP payload without changing the user's clipboard.
static bool clipboardFail = false;
static std::wstring copiedPath;
static BOOL TestOpenClipboard(HWND) { return TRUE; }
static BOOL TestEmptyClipboard() { return TRUE; }
static BOOL TestCloseClipboard() { return TRUE; }
static HANDLE TestSetClipboardData(UINT format, HANDLE handle)
{
    if (clipboardFail) return nullptr;
    if (format != CF_HDROP) std::abort();
    const BYTE* bytes = static_cast<const BYTE*>(GlobalLock(handle));
    if (!bytes || *reinterpret_cast<const DWORD*>(bytes) != 20
        || *reinterpret_cast<const DWORD*>(bytes + 16) != 1) std::abort();
    copiedPath = reinterpret_cast<const wchar_t*>(bytes + 20);
    const wchar_t* tail = reinterpret_cast<const wchar_t*>(bytes + 20) + copiedPath.size();
    if (tail[0] || tail[1]) std::abort();
    GlobalUnlock(handle);
    GlobalFree(handle);
    return reinterpret_cast<HANDLE>(1);
}
#define OpenClipboard TestOpenClipboard
#define EmptyClipboard TestEmptyClipboard
#define CloseClipboard TestCloseClipboard
#define SetClipboardData TestSetClipboardData
#include "../modules/LogPack.inc.cpp"
#undef OpenClipboard
#undef EmptyClipboard
#undef CloseClipboard
#undef SetClipboardData

static void Check(bool ok, const char* name)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
static void SetPath(char* out, const std::wstring& path)
{
    Check(WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, out, kPathCap_, nullptr, nullptr) > 0,
        "UTF-8 path conversion");
}
static void WriteFixture(const std::wstring& path, int index)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE | FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(h != INVALID_HANDLE_VALUE, "create fixture");
    std::string content = "BEGIN initialization\r\n" + std::string(10000 + index, (char)('A' + index))
        + "\r\nEND complete log " + std::to_string(index);
    DWORD wrote = 0;
    Check(WriteFile(h, content.data(), (DWORD)content.size(), &wrote, nullptr) && wrote == content.size(),
        "write full fixture");
    ULARGE_INTEGER value; value.QuadPart = 133000000000000000ULL + index * 10000000ULL;
    FILETIME time = { value.LowPart, value.HighPart };
    Check(SetFileTime(h, nullptr, nullptr, &time) != 0, "set fixture ordering");
    CloseHandle(h);
}
static std::string ReadFixtureText_(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "read log fixture");
    DWORD size = GetFileSize(file, nullptr), read = 0;
    std::string text(size, '\0');
    Check(ReadFile(file, &text[0], size, &read, nullptr) && read == size, "read complete log fixture");
    CloseHandle(file);
    return text;
}

static void TestLogPolicies_(const std::wstring& root)
{
    BattleStorageWindowState_ window = {true, false, true, false, false, false,
        false, false, false, true, true};
    Check(BattleStorageAllowed_(window), "human idle turn allows storage regardless of refresh animation");
    window.executing = true;
    Check(!BattleStorageAllowed_(window), "actual action execution rejects storage");
    window.executing = false;
    window.casting = true;
    Check(!BattleStorageAllowed_(window), "spell execution rejects storage even with no creature action");
    window.casting = false;
    Check(BattleStorageAllowed_(window), "spell return restores idle storage window");
    window.action = true;
    Check(!BattleStorageAllowed_(window), "submitted action rejects storage before executor starts");
    window.action = false;
    window.humanTurn = false;
    Check(!BattleStorageAllowed_(window), "nonhuman turn rejects storage");
    std::puts("PASS: idle animation allowed, submitted action/executor/spell/nonhuman turn rejected");
    Check(g_log_level == LOG_DEBUG, "initial default log level is debug");
    Check(hbs_ui::kUiListVisibleRows == 20, "list viewport holds 20 rows");
    Check(hbs_ui::ListWidth == 168 && hbs_ui::ListWidth + hbs_ui::ScrollbarWidth == 182,
        "wider numbered list reserves 168px plus 14px scrollbar");
    Check(hbs_ui_test::ScrollListRegression(), "scroll offsets, wheel, hit mapping and thumb endpoints");
    Check(hbs_ui_test::ScrollbarDragRegression(), "relative drag retains zero-motion position for long lists");
    hbs_ui::ScrollList activeDrag;
    activeDrag.first = 3;
    activeDrag.dragging = true;
    activeDrag.Wheel(-120, 1000);
    Check(activeDrag.first == 3 && activeDrag.wheelRemainder == 0,
        "wheel during thumb drag is consumed without fighting the drag origin");
    BattleUiPointerGesture_ gesture;
    gesture.Begin(false);
    gesture.CancelClick();
    Check(gesture.Release(true, false) == BattleUiRelease_::Swallow,
        "row down then wheel then row up never loads a different archive");
    gesture.Begin(true);
    Check(gesture.Release(true, false) == BattleUiRelease_::Swallow,
        "scrollbar down then row up cannot become an archive click");
    gesture.Begin(false);
    Check(gesture.Release(false, false) == BattleUiRelease_::Swallow,
        "overlay down then outside up still consumes release");
    Check(gesture.Release(false, false) == BattleUiRelease_::Pass,
        "unowned release outside overlay passes through");
    gesture.Begin(false);
    Check(gesture.Release(true, false) == BattleUiRelease_::Activate,
        "normal owned row click activates");
    gesture.Begin(true);
    bool dragging = true;
    gesture.StopReleasedDrag(dragging, false);
    Check(!dragging && gesture.Release(true, false) == BattleUiRelease_::Swallow,
        "physical release outside window stops drag and cancels activation");
    Check(BattleUiLeftDown_(WM_LBUTTONDOWN) && BattleUiLeftDown_(WM_LBUTTONDBLCLK)
        && BattleUiRightDown_(WM_RBUTTONDOWN) && BattleUiRightDown_(WM_RBUTTONDBLCLK)
        && !BattleUiLeftDown_(WM_MOUSEMOVE), "double clicks share overlay down routing");
    std::puts("PASS: 20-row viewport, wheel/drag mapping, cancelled gestures and double-click routing");
    Check(LogStageLevel_("capture.stacks") == LOG_DEBUG
        && LogStageLevel_("restore.commit-objects") == LOG_INFO
        && LogStageLevel_("restore.rollback") == LOG_WARN, "phase severity policy");
    Check(LogOutcomeLevel_("rejected", false) == LOG_INFO
        && LogOutcomeLevel_("cancelled", false) == LOG_INFO
        && LogOutcomeLevel_("failed", false) == LOG_ERROR
        && LogOutcomeLevel_("rejected", true) == LOG_ERROR, "outcome severity policy");
    LogRepeatGate_ gate;
    unsigned skipped = 0;
    Check(gate.Admit(0, 30000, &skipped) && skipped == 0, "first error is immediate");
    Check(!gate.Admit(1, 30000, &skipped) && !gate.Admit(29999, 30000, &skipped), "repeats are suppressed");
    Check(gate.Admit(30000, 30000, &skipped) && skipped == 2, "interval reports suppressed count");
    gate = {};
    Check(gate.Admit(0xFFFFFFF0u, 100, &skipped)
        && !gate.Admit(0x20u, 100, &skipped)
        && gate.Admit(0x60u, 100, &skipped) && skipped == 1, "rate limit survives tick wrap");
    LogFailureWindow_ failure;
    Check(failure.Observe(true, 0, 30000, &skipped) == 1, "first drawing failure reports");
    for (DWORD i = 1; i < 30000; ++i)
        Check(failure.Observe((i & 1) == 0, i, 30000, &skipped) == 0,
            "alternating failure/success cannot reset rate limit");
    Check(failure.Observe(true, 30000, 30000, &skipped) == 1 && skipped == 14999,
        "flapping failures report aggregate once per interval");
    Check(failure.Observe(false, 59999, 30000, &skipped) == 0
        && failure.Observe(false, 60000, 30000, &skipped) == 2,
        "recovery requires continuous quiet interval");
    LogKeyEdges_ keys;
    Check(keys.Level(57, true) == LOG_DEBUG && keys.Level(57, true) == LOG_TRACE
        && keys.Level(57, false) == LOG_DEBUG && keys.Level(57, true) == LOG_DEBUG,
        "repeat keydowns trace, release and next press debug");
    Check(keys.Level(58, true) == LOG_DEBUG && keys.Level(57, true) == LOG_TRACE
        && keys.Level(-1, true) == LOG_TRACE, "independent key latches and invalid key");
    Check(LogCommandLevel_(false, LOG_DEBUG) == LOG_TRACE
        && LogCommandLevel_(true, LOG_DEBUG) == LOG_DEBUG
        && LogCommandLevel_(true, LOG_TRACE) == LOG_TRACE, "ordinary commands trace, relevant key edges debug");
    Check(LogSnapshotLevel_("readback") == LOG_TRACE
        && LogSnapshotLevel_("captured") == LOG_INFO, "readback summary is trace, original snapshot remains visible");
    LogActivityWindow_ activity;
    activity.Observe(0xFFFFFFF0u, 2, 1);
    activity.Observe(0x10u, 7, 3);
    Check(activity.count == 2 && activity.maxDepth == 3 && !activity.Due(0x1377u, false)
        && activity.Due(0x1378u, false), "activity summary interval and tick wrap");
    activity.Reported(0x20u, true);
    activity.Observe(0x30u, 1, 2);
    Check(!activity.Due(0x1377u, false) && activity.Due(0x1378u, false), "forced activity report preserves cadence");
    Check(hbs_guard_test::PolicyRegression(), "Guard time window and storm counters");
    Check(hbs_guard_test::WriterRegression(root), "Guard actual writer/filter time window");
    const std::wstring path = root + L"\\severity-test.txt";
    Check(path.size() < kPathCap_ / 2, "test log path capacity");
    wcscpy_s(g_log_path_w, kPathCap_ / 2, path.c_str());
    g_disable_log = false;
    g_log_level = LOG_INFO;
    LogTrace("FILTER_TRACE_HIDDEN"); LogDebug("FILTER_DEBUG_HIDDEN");
    LogInfo("FILTER_INFO_VISIBLE"); LogWarn("FILTER_WARN_VISIBLE"); LogError("FILTER_ERROR_VISIBLE");
    g_log_level = LOG_DEBUG;
    LogDebug("FILTER_DEBUG_VISIBLE"); LogTrace("FILTER_TRACE_STILL_HIDDEN");
    g_disable_log = true;
    LogError("FILTER_DISABLED_HIDDEN");
    const std::string text = ReadFixtureText_(path);
    Check(text.find("FILTER_INFO_VISIBLE") != std::string::npos
        && text.find("FILTER_WARN_VISIBLE") != std::string::npos
        && text.find("FILTER_ERROR_VISIBLE") != std::string::npos
        && text.find("FILTER_DEBUG_VISIBLE") != std::string::npos
        && text.find("HIDDEN") == std::string::npos, "real writer filters info/debug/trace/DisableLog");
    const std::wstring contextPath = root + L"\\context-test.txt";
    wcscpy_s(g_log_path_w, kPathCap_ / 2, contextPath.c_str());
    g_disable_log = false; g_log_level = LOG_DEBUG;
    g_logContextSequence = g_logContextReported = 0;
    for (unsigned i = 0; i < 120; ++i) LogDetail_("DETAIL_%03u action=7 current=1:3", i);
    Check(GetFileAttributesW(contextPath.c_str()) == INVALID_FILE_ATTRIBUTES,
        "normal debug details use memory only");
    LogRecentContext_("synthetic-fault");
    std::string context = ReadFixtureText_(contextPath);
    Check(context.find("overwritten=24") != std::string::npos
        && context.find("DETAIL_000") == std::string::npos
        && context.find("DETAIL_024") != std::string::npos
        && context.find("DETAIL_119") != std::string::npos,
        "fault dumps recent bounded details chronologically and states overwritten count");
    LogRecentContext_("no-new-details");
    Check(ReadFixtureText_(contextPath) == context, "no duplicate context dump without new details");
    g_log_level = LOG_INFO; LogDetail_("INFO_HIDDEN_CONTEXT");
    g_log_level = LOG_TRACE; LogDetail_("TRACE_DETAIL_VISIBLE");
    g_disable_log = true; LogDetail_("DISABLED_HIDDEN_CONTEXT");
    g_disable_log = false;
    context = ReadFixtureText_(contextPath);
    Check(context.find("TRACE_DETAIL_VISIBLE") != std::string::npos
        && context.find("INFO_HIDDEN_CONTEXT") == std::string::npos
        && context.find("DISABLED_HIDDEN_CONTEXT") == std::string::npos,
        "detail writer respects trace, info and DisableLog");
    g_log_level = LOG_DEBUG;
    LogDetail_("WRITE_RETRY_DETAIL");
    const auto unreported = g_logContextReported;
    wcscpy_s(g_log_path_w, kPathCap_ / 2, (contextPath + L"\\unwritable").c_str());
    Check(!LogRecentContext_("write-failure") && g_logContextReported == unreported,
        "failed context write never marks records reported");
    wcscpy_s(g_log_path_w, kPathCap_ / 2, contextPath.c_str());
    Check(LogRecentContext_("write-retry"), "context write retry succeeds");
    GuardSetLogPathW(contextPath.c_str());
    s_guard_context_pending = false;
    const int hook = GuardRegisterHook_("ContextRegression");
    EXCEPTION_RECORD er = {};
    er.ExceptionCode = 0xC0000005; er.ExceptionAddress = (void*)0x11223344;
    er.NumberParameters = 2; er.ExceptionInformation[1] = 0x1881;
    EXCEPTION_POINTERS ep = {&er, nullptr};
    s_guard_regression_tick_ = 20000;
    LogDetail_("FIRST_FILTER_CONTEXT original=1/32 translated=512/13/2010");
    GuardCrashFilter_(hook, &ep);
    context = ReadFixtureText_(contextPath);
    Check(context.find("FIRST_FILTER_CONTEXT") != std::string::npos
        && context.find("reason=guard-first") != std::string::npos, "actual first Guard filter dumps command context");
    LogDetail_("PERIOD_FILTER_CONTEXT action=7 current=1:3");
    s_guard_regression_tick_ = 24999; GuardCrashFilter_(hook, &ep);
    Check(ReadFixtureText_(contextPath).find("PERIOD_FILTER_CONTEXT") == std::string::npos,
        "context follows Guard time window without per-fault flood");
    s_guard_regression_tick_ = 25000; GuardFlushFaults_("period", false);
    Check(ReadFixtureText_(contextPath).find("PERIOD_FILTER_CONTEXT") != std::string::npos,
        "actual periodic Guard report dumps pending context");
    LogDetail_("LOCK_RETRY_CONTEXT");
    InterlockedExchange(&g_logContextLock, 1);
    s_guard_regression_tick_ = 30000; GuardCrashFilter_(hook, &ep);
    InterlockedExchange(&g_logContextLock, 0);
    Check(s_guard_context_pending, "busy context lock marks retry without blocking Guard");
    s_guard_regression_tick_ = 35000; GuardFlushFaults_("retry", false);
    Check(!s_guard_context_pending && ReadFixtureText_(contextPath).find("LOCK_RETRY_CONTEXT") != std::string::npos,
        "quiet cycle retries deferred context even without new fault");
    LogDetail_("FATAL_FILTER_CONTEXT native_begin=1");
    s_guard_regression_tick_ = 35001; GuardCrashFilter_(hook, &ep);
    GuardWriteFatalReport_(&ep);
    Check(ReadFixtureText_(contextPath).find("FATAL_FILTER_CONTEXT") != std::string::npos
        && s_guard_hooks[hook].window.reported == s_guard_hooks[hook].window.total,
        "fatal callback records recent context and last pending signature");
    LogDetail_("STACK_OVERFLOW_CONTEXT_SKIPPED");
    er.ExceptionCode = 0xC00000FD;
    GuardWriteFatalReport_(&ep);
    Check(ReadFixtureText_(contextPath).find("STACK_OVERFLOW_CONTEXT_SKIPPED") == std::string::npos,
        "stack overflow skips extra context stack usage");
    const int delayedHook = GuardRegisterHook_("ContextRegression.DelayedFirst");
    er.ExceptionCode = 0xC0000005;
    InterlockedExchange(&s_guard_fault_log_busy, 1);
    GuardCrashFilter_(delayedHook, &ep);
    InterlockedExchange(&s_guard_fault_log_busy, 0);
    er.ExceptionCode = 0xC00000FD;
    GuardCrashFilter_(delayedHook, &ep);
    Check(ReadFixtureText_(contextPath).find("STACK_OVERFLOW_CONTEXT_SKIPPED") == std::string::npos,
        "current stack overflow filter never dumps delayed earlier AV context");
    GuardSetLogPathW(nullptr);
    g_log_path_w[0] = 0;
    g_log_level = LOG_INFO;
    std::puts("PASS: Guard time windows and debug recent-context diagnostics");
    std::puts("PASS: severity policy, repeat aggregation, tick wrap, actual log level filtering");
}

int wmain(int argc, wchar_t** argv)
{
    Check(BattleIsKeyboardMessage_(1) && BattleIsKeyboardMessage_(2)
        && !BattleIsKeyboardMessage_(0x200), "only original key messages are keyboard input");
    Check(!BattleUiMayConsumeMessage_(1) && !BattleUiMayConsumeMessage_(2),
        "Space and other keyboard messages bypass overlay click routing");
    Check(!BattleUiMayConsumeMessage_(0x200),
        "Defend/Wait item clicks, hotkeys and redraw commands bypass overlay routing");
    Check(!BattleUiMayConsumeMessage_(8) && !BattleUiMayConsumeMessage_(16)
        && !BattleUiMayConsumeMessage_(32) && !BattleUiMayConsumeMessage_(64),
        "raw mouse transitions are handled only by the system mouse hook");
    Check(BattleUiMayConsumeMessage_(4), "hover can update overlay state");
    std::puts("PASS: native commands and keyboard input never enter overlay click routing");
    {
        // 2026-10-10 玩家日志回归：绝对消息深度阻断读档消费与超时取消。
        const BattleRestoreMaintainState_ base = {true, true, 21, 21, 0};
        Check(BattleRestoreMaintainDecision_(base) == BattleRestoreMaintain_::Keep,
            "fresh queued request in the same battle is kept");
        Check(BattleRestoreMaintainDecision_({false, true, 21, 21, 60000})
            == BattleRestoreMaintain_::Keep, "maintenance ignores already-consumed requests");
        Check(BattleRestoreMaintainDecision_({true, false, 21, 21, 60000})
            == BattleRestoreMaintain_::CancelBattleChanged, "finished/unreadable battle cancels first");
        Check(BattleRestoreMaintainDecision_({true, true, 21, 25, 60000})
            == BattleRestoreMaintain_::CancelBattleChanged, "generation change cancels before timeout");
        Check(BattleRestoreMaintainDecision_({true, true, 21, 21, 19999ul})
            == BattleRestoreMaintain_::Keep, "under twenty seconds still waits for the boundary");
        Check(BattleRestoreMaintainDecision_({true, true, 21, 21, 20000ul})
            == BattleRestoreMaintain_::Keep, "exactly twenty seconds keeps strict-greater timeout");
        Check(BattleRestoreMaintainDecision_({true, true, 21, 21, 20001ul})
            == BattleRestoreMaintain_::CancelTimeout, "over twenty seconds cancels even without any boundary");
        BattleMessageFrames_ frames = {};
        BattleMessageFrame_ ancestor = {};
        const auto ancestorToken = frames.Enter(ancestor, 19u, (const void*)0x11110000, (const void*)0xAAAA0000);
        ancestor.nativeReturned = true;
        frames.Update(ancestor, ancestorToken);
        Check(frames.Depth(19u) == 1 && frames.Depth(21u) == 0,
            "old-generation ancestor never counts toward the current battle depth");
        BattleMessageFrame_ outer = {};
        const auto outerToken = frames.Enter(outer, 21u, (const void*)0x22220000, (const void*)0xBBBB0000);
        Check(!frames.Boundary(21u, (const void*)0x22220000, (const void*)0xBBBB0000),
            "outermost frame is not a boundary while the original function is still running");
        outer.nativeReturned = true;
        frames.Update(outer, outerToken);
        Check(frames.Boundary(21u, (const void*)0x22220000, (const void*)0xBBBB0000)
            && frames.Depth(21u) == 1,
            "outermost returned frame of the current battle is the restore boundary despite stale ancestors");
        BattleMessageFrame_ nested = {};
        const auto nestedToken = frames.Enter(nested, 21u, (const void*)0x22220000, (const void*)0xBBBB0000);
        nested.nativeReturned = true;
        frames.Update(nested, nestedToken);
        Check(!frames.Boundary(21u, (const void*)0x22220000, (const void*)0xBBBB0000),
            "same-battle nested modal (options dialog) is not a boundary");
        frames.Leave(nested, nestedToken);
        Check(frames.Boundary(21u, (const void*)0x22220000, (const void*)0xBBBB0000),
            "boundary returns after the nested modal unwinds");
        Check(!frames.Boundary(21u, (const void*)0x33330000, (const void*)0xBBBB0000)
            && !frames.Boundary(21u, (const void*)0x22220000, (const void*)0xCCCC0000)
            && !frames.Boundary(21u, (const void*)0x22220000, nullptr),
            "manager reuse, dialog replacement and missing dialog all reject the boundary");
        frames.Leave(outer, outerToken);
        frames.Leave(ancestor, ancestorToken);
        Check(frames.Depth(21u) == 0 && !frames.Boundary(21u, (const void*)0x22220000, (const void*)0xBBBB0000),
            "empty registry has no boundary");
        // 玩家日志证明链指针无效；最初破坏链的机制仍待证。
        // 下列夹具模拟残留登记、栈数据覆写和地址复用，不冒充原生退栈实测。
        {
            BattleMessageFrames_ reuse = {};
            BattleMessageFrame_ frame = {};
            const auto oldToken = reuse.Enter(frame, 21u, (const void*)0x22220000, (const void*)0xBBBB0000);
            memset(&frame, 0x81, sizeof(frame));
            Check(reuse.Depth(21u) == 1, "depth uses the owned copy after stack bytes are overwritten");
            const auto newToken = reuse.Enter(frame, 23u, (const void*)0x44440000, (const void*)0xDDDD0000);
            Check(reuse.Depth(21u) == 0 && reuse.Depth(23u) == 1,
                "generation transition retires stale registrations without dereferencing stack memory");
            frame.nativeReturned = true;
            reuse.Update(frame, newToken);
            Check(reuse.Boundary(23u, (const void*)0x44440000, (const void*)0xDDDD0000),
                "reborn frame is a valid boundary");
            reuse.Leave(frame, oldToken);
            frame.nativeReturned = false;
            frame.dialog = nullptr;
            reuse.Update(frame, oldToken);
            Check(reuse.Boundary(23u, (const void*)0x44440000, (const void*)0xDDDD0000),
                "stale token cannot remove or update a same-address new registration");
            reuse.Leave(frame, newToken);
            Check(!reuse.Boundary(23u, (const void*)0x44440000, (const void*)0xDDDD0000),
                "matching token leaves the current registration");
        }
        {
            BattleMessageFrames_ same = {};
            BattleMessageFrame_ frame = {};
            const auto oldToken = same.Enter(frame, 25u, (const void*)0x22220000, (const void*)0xBBBB0000);
            const auto newToken = same.Enter(frame, 25u, (const void*)0x22220000, (const void*)0xBBBB0000);
            frame.nativeReturned = true;
            same.Update(frame, newToken);
            same.Leave(frame, oldToken);
            frame.nativeReturned = false;
            same.Update(frame, oldToken);
            Check(same.Depth(25u) == 1 && same.Boundary(25u, (const void*)0x22220000, (const void*)0xBBBB0000),
                "same-generation address reuse requires the current invocation token");
            same.Leave(frame, newToken);
            Check(same.Depth(25u) == 0, "current token removes the reused address");
            BattleMessageFrame_ oldFrame = {}, newFrame = {};
            const auto oldGenerationToken = same.Enter(oldFrame, 25u, (const void*)0x22220000, (const void*)0xBBBB0000);
            const auto nextGenerationToken = same.Enter(newFrame, 26u, (const void*)0x22220000, (const void*)0xBBBB0000);
            newFrame.nativeReturned = true;
            same.Update(newFrame, nextGenerationToken);
            oldFrame.nativeReturned = true;
            same.Update(oldFrame, oldGenerationToken);
            same.Leave(oldFrame, oldGenerationToken);
            Check(same.Depth(25u) == 0 && same.Boundary(26u, (const void*)0x22220000, (const void*)0xBBBB0000),
                "late old-generation callbacks at a different address never resurrect stale registrations");
            same.Leave(newFrame, nextGenerationToken);
        }
        {
            BattleMessageFrames_ full = {};
            BattleMessageFrame_ frames65[BattleMessageFrames_::kMaxFrames_ + 1] = {};
            unsigned long long tokens[BattleMessageFrames_::kMaxFrames_ + 1] = {};
            for (int i = 0; i <= BattleMessageFrames_::kMaxFrames_; ++i)
                tokens[i] = full.Enter(frames65[i], 30u, (const void*)0x50000000, (const void*)0x60000000);
            Check(tokens[BattleMessageFrames_::kMaxFrames_] == 0,
                "overflow entry returns an invalid token");
            for (int i = 1; i < BattleMessageFrames_::kMaxFrames_; ++i)
                full.Leave(frames65[i], tokens[i]);
            frames65[0].nativeReturned = true;
            full.Update(frames65[0], tokens[0]);
            Check(full.Depth(30u) == 1 && !full.Boundary(30u, (const void*)0x50000000, (const void*)0x60000000),
                "saturation rejects a boundary even after depth falls to one");
            BattleMessageFrame_ nextBattle = {};
            const auto token = full.Enter(nextBattle, 31u, (const void*)0x50000000, (const void*)0x60000000);
            nextBattle.nativeReturned = true;
            full.Update(nextBattle, token);
            Check(full.Boundary(31u, (const void*)0x50000000, (const void*)0x60000000),
                "next generation clears stale entries and recovers from saturation");
        }
        std::puts("PASS: restore maintenance timeout/generation and per-battle message boundary policy");
    }
    Check(argc == 2, "fixture directory argument");
    const std::wstring root = argv[1];
    Check(CreateDirectoryW(root.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS,
        "fixture directory");
    TestLogPolicies_(root);
    g_disable_log = true;
    g_log_path_w[0] = 0;
    SetPath(g_ini_path, root + L"\\H3BattleStore.default.ini");
    SetPath(g_user_ini_path, root + L"\\H3BattleStore.user.ini");
    Check(ParseLogLevel_(nullptr) == LOG_DEBUG && ParseLogLevel_("") == LOG_DEBUG
        && ParseLogLevel_("garbage") == LOG_DEBUG, "missing and invalid levels default to debug");
    Check(ParseLogLevel_("INFO") == LOG_INFO && ParseLogLevel_("error") == LOG_ERROR,
        "valid info and error levels still parse normally");
    Check(std::strcmp(LogLevelName_(-1), "debug") == 0
        && std::strcmp(LogLevelName_(5), "debug") == 0
        && std::strcmp(LogLevelDisplayName_(-1), "调试") == 0
        && std::strcmp(LogLevelDisplayName_(5), "调试") == 0, "invalid level display falls back to debug");
    Check(std::strcmp(LogLevelName_(LOG_TRACE), "trace") == 0
        && std::strcmp(LogLevelName_(LOG_ERROR), "error") == 0,
        "persisted level names remain English");
    Check(std::strcmp(LogLevelDisplayName_(LOG_TRACE), "全部") == 0
        && std::strcmp(LogLevelDisplayName_(LOG_DEBUG), "调试") == 0
        && std::strcmp(LogLevelDisplayName_(LOG_INFO), "信息") == 0
        && std::strcmp(LogLevelDisplayName_(LOG_WARN), "警告") == 0
        && std::strcmp(LogLevelDisplayName_(LOG_ERROR), "错误") == 0,
        "display level names are Chinese");
    Check(std::strstr(kLogFeedbackContacts_, "1042362808")
        && std::strstr(kLogFeedbackContacts_, "740338251")
        && std::strstr(kLogFeedbackContacts_, "712999712")
        && std::strstr(kLogPackSuccess_, "00_说明.txt"),
        "pack feedback contacts are present");
    ReadConfig();
    Check(g_log_level == LOG_DEBUG, "missing layered config defaults to debug");
    Check(IniWriteKeyUtf8(g_ini_path, "Logging", "MinLevel", "debug"), "write default fixture");
    Check(IniWriteKeyUtf8(g_user_ini_path, "Logging", "MinLevel", "info"), "write user override fixture");
    ReadConfig();
    Check(g_log_level == LOG_INFO, "user info override remains higher priority than default debug");
    Check(IniWriteKeyUtf8(g_user_ini_path, "Logging", "MinLevel", "garbage"), "write invalid override fixture");
    ReadConfig();
    Check(g_log_level == LOG_DEBUG, "invalid user level falls back to debug without rewriting it");
    for (int i = 0; i < 5; ++i) {
        Check(SaveLogLevel_(i) && g_log_level == i, "selection writes before applying");
        ReadConfig();
        Check(g_log_level == i, "selection survives config reload");
    }
    SetPath(g_user_ini_path, root + L"\\missing\\H3BattleStore.user.ini");
    Check(!SaveLogLevel_(LOG_DEBUG) && g_log_level == LOG_ERROR, "write failure preserves level");
    Check(!SaveLogLevel_(-1) && !SaveLogLevel_(5), "invalid level rejected");
    char reason[192] = {};
    Check(!PackRecentLogs_(nullptr, 0, reason, sizeof(reason)) && reason[0], "no logs reported");
    for (int i = 0; i < 7; ++i)
        WriteFixture(root + L"\\H3BattleStore_fixture_" + std::to_wstring(i) + L".log", i);
    WriteFixture(root + L"\\H3Auto_unrelated.log", 9);
    LogPackEntry entries[5] = {};
    Check(LogPackCollectRecent_(entries, 5) == 5, "collect exactly five latest logs");
    Check(std::strcmp(entries[0].name, "H3BattleStore_fixture_6.log") == 0
        && std::strcmp(entries[4].name, "H3BattleStore_fixture_2.log") == 0, "newest-first ordering");
    Check(PackRecentLogs_(nullptr, 0, reason, sizeof(reason)), "LZMA 7z pack success");
    Check(copiedPath.find(L"H3BattleStore_logs_") != std::wstring::npos
        && GetFileAttributesW(copiedPath.c_str()) != INVALID_FILE_ATTRIBUTES, "wide CF_HDROP pack path");
    clipboardFail = true;
    Check(!PackRecentLogs_(nullptr, 0, reason, sizeof(reason)) && std::strstr(reason, "7z"),
        "clipboard failure reports generated file");
    Check(GetFileAttributesW(copiedPath.c_str()) != INVALID_FILE_ATTRIBUTES,
        "clipboard failure preserves generated archive");
    std::puts("PASS: default debug, user override priority, five levels persisted/reloaded, failed write preserves level");
    std::puts("PASS: newest five full logs, LZMA 7z, Unicode CF_HDROP, clipboard failure");
    return 0;
}
