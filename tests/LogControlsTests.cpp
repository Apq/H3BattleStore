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
int wmain(int argc, wchar_t** argv)
{
    Check(argc == 2, "fixture directory argument");
    const std::wstring root = argv[1];
    Check(CreateDirectoryW(root.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS,
        "fixture directory");
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
