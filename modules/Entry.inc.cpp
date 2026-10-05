// ========== Entry.inc.cpp ==========
// 插件入口。战斗中存档/读档的 Hook 以后挂在 StartPlugin。

#pragma comment(lib, "version.lib")

static void LogSelfVersion_()
{
    wchar_t wpath[MAX_PATH] = {};
    GetModuleFileNameW(g_hModule, wpath, MAX_PATH);
    char utf8[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, utf8,
        (int)sizeof(utf8), nullptr, nullptr);
    char ver[64] = "?";
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(wpath, &handle);
    if (size) {
        BYTE* data = new BYTE[size];
        if (GetFileVersionInfoW(wpath, 0, size, data)) {
            struct LangCodePage { WORD lang, codepage; };
            LangCodePage* langs = nullptr; UINT lang_count = 0;
            if (VerQueryValueW(data, L"\\VarFileInfo\\Translation",
                    (LPVOID*)&langs, &lang_count)
                && lang_count > 0)
            {
                wchar_t key[80] = {};
                swprintf(key, 80, L"\\StringFileInfo\\%04X%04X\\ProductVersion",
                    langs[0].lang, langs[0].codepage);
                wchar_t* product = nullptr; UINT len = 0;
                if (VerQueryValueW(data, key, (LPVOID*)&product, &len) && product)
                    WideCharToMultiByte(CP_UTF8, 0, product, -1, ver,
                        (int)sizeof(ver), nullptr, nullptr);
            }
        }
        delete[] data;
    }
    LogInfo("战斗存档 v%s | DLL=%s", ver, utf8);
}

static bool CombatIsReadable_(const H3CombatManager* mgr)
{
    return mgr && !IsBadReadPtr(mgr, sizeof(H3CombatManager));
}

// ArchiveRoot_ 的栈缓冲在游戏回调线程上有溢出风险（kPathCap_ = 4MB），改用堆。
static std::wstring ArchiveRoot_()
{
    const size_t cap = 4096;
    wchar_t* path = new (std::nothrow) wchar_t[cap]();
    std::wstring result;
    if (path) {
        GetModuleFileNameW(g_hModule, path, (DWORD)cap);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (!slash) slash = wcsrchr(path, L'/');
        if (slash) *(slash + 1) = 0;
        else path[0] = 0;
        wcscat_s(path, cap, L"H3BattleStore.data");
        result = path;
        delete[] path;
    }
    return result;
}

static bool CombatCanCapture_(const H3CombatManager* mgr, const char** reason)
{
    if (!CombatIsReadable_(mgr)) { if (reason) *reason = "no combat"; return false; }
    if (mgr->finished) { if (reason) *reason = "combat finished"; return false; }
    if (mgr->autoCombat) { if (reason) *reason = "auto combat"; return false; }
    if (mgr->action != 0 || mgr->actionParameter || mgr->actionTarget || mgr->actionParameter2) {
        if (reason) *reason = "action in progress";
        return false;
    }
    if (mgr->actionUndergoing) { if (reason) *reason = "animation in progress"; return false; }
    return true;
}

static bool Sha256Hex_(const uint8_t* data, size_t size, std::string* out)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    BYTE digest[32] = {};
    DWORD digestSize = sizeof(digest);
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return false;
    const bool ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)
        && CryptHashData(hash, data, (DWORD)size, 0)
        && CryptGetHashParam(hash, HP_HASHVAL, digest, &digestSize, 0)
        && digestSize == sizeof(digest);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    if (!ok || !out) return false;
    static const char* hex = "0123456789abcdef";
    out->assign(64, '0');
    for (DWORD i = 0; i < digestSize; ++i) {
        (*out)[i * 2] = hex[digest[i] >> 4];
        (*out)[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    return true;
}

static bool BattleFingerprint_(const H3CombatManager* mgr, std::string* out, std::string* error)
{
    if (!CombatIsReadable_(mgr) || !out) return false;
    std::vector<uint8_t> bytes;
    auto put32 = [&](int32_t value) {
        const uint32_t u = (uint32_t)value;
        bytes.push_back((uint8_t)u);
        bytes.push_back((uint8_t)(u >> 8));
        bytes.push_back((uint8_t)(u >> 16));
        bytes.push_back((uint8_t)(u >> 24));
    };
    put32(mgr->landType);
    put32(mgr->specialTerrain);
    put32(mgr->siegeKind);
    put32(mgr->hasMoat);
    put32((int32_t)mgr->boatCombat);
    put32(mgr->town ? 1 : 0);
    for (int side = 0; side < 2; ++side) {
        put32(mgr->heroOwner[side]);
        put32(mgr->heroMonCount[side]);
        put32(mgr->hero[side] ? 1 : 0);
        for (int slot = 0; slot < 7; ++slot) {
            const H3CombatCreature& stack = mgr->stacks[side][slot];
            put32(stack.type);
            put32(stack.numberAtStart);
            put32(stack.slotIndex);
        }
    }
    if (!Sha256Hex_(bytes.data(), bytes.size(), out)) {
        if (error) *error = "battle fingerprint failed";
        return false;
    }
    return true;
}

static void TryCaptureCombat_()
{
    const H3CombatManager* mgr = H3CombatManager::Get();
    const char* reason = nullptr;
    if (!CombatCanCapture_(mgr, &reason)) {
        LogWarn("保存被拒绝：%s", reason ? reason : "unsafe");
        return;
    }
    CodecCapture capture;
    std::string error;
    if (!CaptureBattle_(mgr, &capture, &error)) {
        LogError("战斗采集失败：%s", error.c_str());
        return;
    }
    hbs::ArchiveDocument document;
    if (!BattleFingerprint_(mgr, &document.battleKey, &error)
        || !CodecEncode(capture, &document.sections, &error)) {
        LogError("战斗编码失败：%s", error.c_str());
        return;
    }
    document.targetKey.assign(64, '0');
    FILETIME now = {};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER value = {};
    value.LowPart = now.dwLowDateTime;
    value.HighPart = now.dwHighDateTime;
    document.timestampUtcMs = (value.QuadPart - 116444736000000000ull) / 10000ull;
    document.sequence = 0;

    hbs::ArchiveStore store(ArchiveRoot_());
    std::wstring storeError;
    if (!store.Save(document, storeError)) {
        char utf8[512] = {};
        WideCharToMultiByte(CP_UTF8, 0, storeError.c_str(), -1, utf8, sizeof(utf8), nullptr, nullptr);
        LogError("战斗存档写入失败：%s", utf8);
        return;
    }
    UiMarkSaved_(document.timestampUtcMs);
    if (g_ui.listOpen) UiReloadEntries_(mgr);
    LogInfo("战斗时刻已存档：回合 %d，当前 %d:%d", capture.turn, capture.currentMonSide, capture.currentMonIndex);
}

static bool LoadLatestCapture_(const H3CombatManager* mgr, CodecCapture* out, std::string* error)
{
    std::string battleKey;
    if (!BattleFingerprint_(mgr, &battleKey, error) || !out) return false;
    hbs::ArchiveStore store(ArchiveRoot_());
    std::vector<hbs::ArchiveRecord> records;
    std::wstring storeError;
    if (!store.List(battleKey, "", records, storeError) || records.empty()) {
        if (error) *error = records.empty() ? "no archive for this battle" : "archive list failed";
        return false;
    }
    hbs::ArchiveDocument document;
    if (!store.Load(records[0], document, storeError)) {
        if (error) *error = "archive load failed";
        return false;
    }
    return CodecDecode(document.sections, out, error);
}

static bool RestoreSameBattle_(H3CombatManager* mgr, const CodecCapture& capture, std::string* error);

static void TryRestoreCombat_()
{
    H3CombatManager* mgr = H3CombatManager::Get();
    const char* reason = nullptr;
    if (!CombatCanCapture_(mgr, &reason)) {
        LogWarn("读档被拒绝：%s", reason ? reason : "unsafe");
        return;
    }
    CodecCapture capture;
    std::string error;
    if (!LoadLatestCapture_(mgr, &capture, &error)) {
        LogError("读档失败：%s", error.c_str());
        return;
    }
    if (!RestoreSameBattle_(mgr, capture, &error)) {
        LogError("战斗恢复失败：%s", error.c_str());
        return;
    }
    LogInfo("战斗时刻已恢复：回合 %d，当前 %d:%d", capture.turn, capture.currentMonSide, capture.currentMonIndex);
}

static bool CombatFullyIdle_(const H3CombatManager* mgr, int messageResult, const char** reason)
{
    if (messageResult == 2) { if (reason) *reason = "battle message closes manager"; return false; }
    if (!CombatCanCapture_(mgr, reason)) return false;
    if (mgr->tacticsPhase) { if (reason) *reason = "tactics phase"; return false; }
    for (int i = 0; i < 187; ++i) {
        if (mgr->travelingSquares[i]) { if (reason) *reason = "creature is moving"; return false; }
    }
    return true;
}

static int __stdcall Hook_CombatMessage_(HiHook* hook, H3CombatManager* mgr, H3Msg* msg)
{
    if (msg && msg->command == eMsgCommand::KEY_DOWN) {
        const char pressed = UiVirtualKeyToLetter_(msg->subtype);
        LogInfo("战斗按键：virtual=%d letter=%c save=%c", msg->subtype,
            pressed ? pressed : '?', g_ui.saveKey);
        const int result = THISCALL_2(int, hook->GetDefaultFunc(), mgr, msg);
        if (g_ui.awaitingRebind) {
            UiHandleRebindKey_(pressed, msg->subtype == h3::NH3VKey::H3VK_ESCAPE);
            return result;
        }
        if (pressed && pressed == g_ui.saveKey) {
            const char* reason = nullptr;
            if (!CombatFullyIdle_(mgr, result, &reason))
                LogWarn("保存被拒绝：%s", reason ? reason : "unsafe");
            else
                TryCaptureCombat_();
        }
        return result;
    }
    UiHandleDragMove_(msg);
    const bool onBar = msg && UiHitBar_(msg->position.x, msg->position.y);
    const int row = msg ? UiHitRow_(msg->position.x, msg->position.y) : -1;
    if ((onBar || row >= 0 || g_ui.dragging) && msg) {
        if (msg->command == eMsgCommand::MOUSE_BUTTON) {
            UiHandleMouse_(msg);
            return 1;
        }
        if (msg->command == eMsgCommand::MOUSE_OVER && g_ui.listOpen) {
            UiHandleMouse_(msg);
            return 1;
        }
    } else if (msg && msg->command == eMsgCommand::MOUSE_BUTTON
        && msg->subtype == eMsgSubtype::LBUTTON_DOWN) {
        if (g_ui.awaitingRebind) g_ui.awaitingRebind = false;
        if (g_ui.listOpen) g_ui.listOpen = false;
    }
    return THISCALL_2(int, hook->GetDefaultFunc(), mgr, msg);
}

static int __stdcall Hook_CycleCombatScreen_(HiHook* hook, H3CombatManager* mgr)
{
    static DWORD lastFrame = 0;
    const int result = THISCALL_1(int, hook->GetDefaultFunc(), mgr);
    const DWORD now = GetTickCount();
    const bool readable = CombatIsReadable_(mgr);
    if (now - lastFrame > 1000) {
        LogDebug("战斗绘制帧：mgr=%p readable=%d finished=%d dlg=%p",
            mgr, readable ? 1 : 0,
            readable ? (mgr->finished ? 1 : 0) : -1,
            readable ? mgr->dlg : nullptr);
        lastFrame = now;
    }
    if (readable && !mgr->finished && mgr->dlg)
        UiDrawBar_(mgr);
    else if (now - lastFrame <= 20)
        LogInfo("悬浮条跳过绘制：readable=%d finished=%d dlg=%p",
            readable ? 1 : 0,
            readable ? (mgr->finished ? 1 : 0) : -1,
            readable ? mgr->dlg : nullptr);
    return result;
}

static void StartPlugin()
{
    LogSelfVersion_();
    UiLoadBarPosition_();
    _PI->WriteHiHook(0x473A00, SPLICE_, EXTENDED_, THISCALL_, Hook_CombatMessage_);
    _PI->WriteHiHook(0x495C50, SPLICE_, EXTENDED_, THISCALL_, Hook_CycleCombatScreen_);
    LogInfo("战斗存档: battle-store build enabled.");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    static bool initialized = false;
    if (reason == DLL_PROCESS_ATTACH && !initialized) {
        initialized = true;
        g_hModule = hModule;
        auto utf8_from_wide = [](const wchar_t* w, char* out, int out_size) {
            WideCharToMultiByte(CP_UTF8, 0, w, -1, out, out_size, nullptr, nullptr);
            if (out_size > 0) out[out_size - 1] = 0;
        };
        wchar_t* wpath = new wchar_t[kPathCap_ / 2]();
        auto set_dll_dir_file = [&](const wchar_t* name, char* out_utf8) {
            GetModuleFileNameW(hModule, wpath, kPathCap_ / 2);
            wchar_t* wslash = wcsrchr(wpath, L'\\');
            if (!wslash) wslash = wcsrchr(wpath, L'/');
            if (wslash) wcscpy(wslash + 1, name);
            else wcscpy(wpath, name);
            utf8_from_wide(wpath, out_utf8, kPathCap_);
        };
        set_dll_dir_file(L"H3BattleStore.default.ini", g_ini_path);
        set_dll_dir_file(L"H3BattleStore.user.ini", g_user_ini_path);
        g_disable_log = ReadDisableLogFromIniFiles();
        delete[] wpath;
        SetupDatedLogPathAndCleanup(hModule);
        LogInfo("战斗存档 loading.");
        _P = GetPatcher();
        if (!_P) { LogError("GetPatcher failed."); return TRUE; }
        _PI = _P->CreateInstance("HD.Plugin.H3BattleStore");
        if (!_PI) { LogError("CreateInstance failed."); return TRUE; }
        ReadConfig();
        StartPlugin();
    }
    return TRUE;
}

