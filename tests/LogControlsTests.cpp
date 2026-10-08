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
    // 列表最大行数固定 30（2026-10-08 用户裁定），与同场磁盘保留条数一致。
    Check(hbs_ui::kUiListMaxRows == 30, "list rows fixed at 30 retained records");
    Check(hbs_ui::ForFont(16).Height(hbs_ui::kUiListMaxRows) == 588,
        "30 default-font rows are 588 tall; at y=8 they end at 596 of 600");
    std::puts("PASS: list rows fixed at 30, fitting 600-height default font");
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
    Check(argc == 2, "fixture directory argument");
    const std::wstring root = argv[1];
    Check(CreateDirectoryW(root.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS,
        "fixture directory");
    TestLogPolicies_(root);
    g_disable_log = true;
    g_log_path_w[0] = 0;
    SetPath(g_ini_path, root + L"\\H3BattleStore.default.ini");
    SetPath(g_user_ini_path, root + L"\\H3BattleStore.user.ini");
    Check(ParseLogLevel_(nullptr) == LOG_INFO && ParseLogLevel_("garbage") == LOG_INFO,
        "missing and invalid levels default to info");
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
    Check(g_log_level == LOG_INFO, "missing layered config defaults to info");
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
    std::puts("PASS: default info, five levels persisted/reloaded, failed write preserves level");
    std::puts("PASS: newest five full logs, LZMA 7z, Unicode CF_HDROP, clipboard failure");
    return 0;
}
