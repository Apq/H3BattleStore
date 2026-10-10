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
    g_log_path_w[0] = 0;
    g_log_level = LOG_INFO;
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
        Check(BattleRestoreMaintainDecision_({true, true, 21, 21, 4999ul})
            == BattleRestoreMaintain_::Keep, "under five seconds still waits for the boundary");
        Check(BattleRestoreMaintainDecision_({true, true, 21, 21, 5000ul})
            == BattleRestoreMaintain_::Keep, "exactly five seconds keeps strict-greater timeout");
        Check(BattleRestoreMaintainDecision_({true, true, 21, 21, 5001ul})
            == BattleRestoreMaintain_::CancelTimeout, "over five seconds cancels even without any boundary");
        BattleMessageFrames_ frames = {};
        BattleMessageFrame_ ancestor = {};
        frames.Enter(ancestor, 19u, (const void*)0x11110000, (const void*)0xAAAA0000);
        ancestor.nativeReturned = true;
        Check(frames.Depth(19u) == 1 && frames.Depth(21u) == 0,
            "old-generation ancestor never counts toward the current battle depth");
        BattleMessageFrame_ outer = {};
        frames.Enter(outer, 21u, (const void*)0x22220000, (const void*)0xBBBB0000);
        Check(!frames.Boundary(21u, (const void*)0x22220000, (const void*)0xBBBB0000),
            "outermost frame is not a boundary while the original function is still running");
        outer.nativeReturned = true;
        Check(frames.Boundary(21u, (const void*)0x22220000, (const void*)0xBBBB0000)
            && frames.Depth(21u) == 1,
            "outermost returned frame of the current battle is the restore boundary despite stale ancestors");
        BattleMessageFrame_ nested = {};
        frames.Enter(nested, 21u, (const void*)0x22220000, (const void*)0xBBBB0000);
        nested.nativeReturned = true;
        Check(!frames.Boundary(21u, (const void*)0x22220000, (const void*)0xBBBB0000),
            "same-battle nested modal (options dialog) is not a boundary");
        frames.Leave(nested);
        Check(frames.Boundary(21u, (const void*)0x22220000, (const void*)0xBBBB0000),
            "boundary returns after the nested modal unwinds");
        Check(!frames.Boundary(21u, (const void*)0x33330000, (const void*)0xBBBB0000)
            && !frames.Boundary(21u, (const void*)0x22220000, (const void*)0xCCCC0000)
            && !frames.Boundary(21u, (const void*)0x22220000, nullptr),
            "manager reuse, dialog replacement and missing dialog all reject the boundary");
        frames.Leave(outer);
        frames.Leave(ancestor);
        Check(frames.top == nullptr && !frames.Boundary(21u, (const void*)0x22220000, (const void*)0xBBBB0000),
            "empty stack has no boundary");
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
