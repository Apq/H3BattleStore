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
        // 2026-10-05 实测：玩家空闲悬停时该四元组也持续非零（疑似 UI 预备动作），
        // 不能作为保存门槛；保存是纯读快照，动态风险由 actionUndergoing /
        // travelingSquares / finished / tacticsPhase 把关。此处仅记录观察值。
        static DWORD lastActionLog = 0;
        const DWORD actionNow = GetTickCount();
        if (actionNow - lastActionLog > 2000) {
            LogInfo("保存时动作字段非零：action=%d p=%d t=%d p2=%d",
                (int)mgr->action, (int)(INT_PTR)mgr->actionParameter,
                (int)(INT_PTR)mgr->actionTarget, (int)(INT_PTR)mgr->actionParameter2);
            lastActionLog = actionNow;
        }
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

// 2026-10-05 实测：actionUndergoing 在背景动画（岩浆/旗帜等）常驻非零，
// 直接拒绝会让"按 G"看起来无反应。改为等待静止帧：置 3 秒窗口由绘制帧轮询，
// 静止即存；超时强制存（采集是纯读快照，风险由恢复端指纹校验兜底）。
// 移动中的部队同理短暂等待。
static void TrySaveOrWait_(H3CombatManager* mgr, int messageResult)
{
    const char* reason = nullptr;
    if (CombatFullyIdle_(mgr, messageResult, &reason)) {
        g_uiWaitSaveUntil = 0;
        TryCaptureCombat_();
        return;
    }
    if (reason && (strcmp(reason, "animation in progress") == 0
        || strcmp(reason, "creature is moving") == 0)) {
        g_uiWaitSaveUntil = GetTickCount() + 3000;
        LogInfo("战斗未静止（%s），等待静止帧存档（最多3秒）", reason);
        return;
    }
    LogWarn("保存被拒绝：%s", reason ? reason : "unsafe");
}

static HHOOK g_combatKeyboardHook = nullptr;
static HHOOK g_combatMouseHook = nullptr;
static volatile LONG g_pendingSaveKey = 0;
static volatile LONG g_pendingClickX = -1;
static volatile LONG g_pendingClickY = -1;
static volatile LONG g_pendingRightClickX = -1;
static volatile LONG g_pendingRightClickY = -1;

static bool UiGamePointFromScreen_(POINT screenPoint, int* gameX, int* gameY)
{
    HWND gameWindow = *reinterpret_cast<HWND*>(0x699650);
    H3WindowManager* wnd = H3WindowManager::Get();
    POINT clientPoint = screenPoint;
    RECT client = {};
    if (!gameWindow || !wnd || !wnd->screenPcx16
        || !ScreenToClient(gameWindow, &clientPoint)
        || !GetClientRect(gameWindow, &client)
        || client.right <= client.left || client.bottom <= client.top)
        return false;
    *gameX = MulDiv(clientPoint.x, wnd->screenPcx16->width, client.right - client.left);
    *gameY = MulDiv(clientPoint.y, wnd->screenPcx16->height, client.bottom - client.top);
    return true;
}

static LRESULT CALLBACK CombatKeyboardHook_(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && !(lParam & 0x80000000) && !(lParam & 0x40000000)) {
        const char letter = UiVirtualKeyToLetter_((int)wParam, true);
        if (letter) {
            InterlockedExchange(&g_pendingSaveKey, letter);
            LogInfo("系统键盘边沿：vk=%d letter=%c", (int)wParam, letter);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

static LRESULT CALLBACK CombatMouseHook_(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION) {
        const bool leftDown = wParam == WM_LBUTTONDOWN;
        const bool leftUp = wParam == WM_LBUTTONUP;
        const bool rightDown = wParam == WM_RBUTTONDOWN;
        const bool rightUp = wParam == WM_RBUTTONUP;
        const bool move = wParam == WM_MOUSEMOVE;
        if (move && !g_ui.dragging)
            return CallNextHookEx(nullptr, code, wParam, lParam);
        if (leftDown || leftUp || rightDown || rightUp || move) {
            // 只在真实战斗且悬浮条显示时介入，避免战斗外误吞点击。
            H3CombatManager* combat = H3CombatManager::Get();
            if (!CombatIsReadable_(combat) || combat->finished || !combat->dlg)
                return CallNextHookEx(nullptr, code, wParam, lParam);
            const MOUSEHOOKSTRUCT* mouse = reinterpret_cast<const MOUSEHOOKSTRUCT*>(lParam);
            int gameX = 0;
            int gameY = 0;
            if (!mouse || !UiGamePointFromScreen_(mouse->pt, &gameX, &gameY))
                return CallNextHookEx(nullptr, code, wParam, lParam);
            const bool inBar = UiPointInBar_(gameX, gameY);
            const int row = UiHitRow_(gameX, gameY);
            const bool hitList = row >= 0 && row < (int)g_ui.entries.size();
            if (move) {
                if (g_ui.dragging) {
                    g_ui.x = gameX - g_ui.dragOffX;
                    g_ui.y = gameY - g_ui.dragOffY;
                }
            } else if (leftDown && inBar) {
                g_ui.dragging = true;
                g_ui.dragOffX = gameX - g_ui.x;
                g_ui.dragOffY = gameY - g_ui.y;
                g_ui.listOpen = false;
                return 1;
            } else if (leftUp && g_ui.dragging) {
                g_ui.dragging = false;
                H3WindowManager* wnd = H3WindowManager::Get();
                if (wnd && wnd->screenPcx16) {
                    if (g_ui.x < 0) g_ui.x = 0;
                    if (g_ui.y < 0) g_ui.y = 0;
                    if (g_ui.x + kUiBarWidth > wnd->screenPcx16->width)
                        g_ui.x = wnd->screenPcx16->width - kUiBarWidth;
                    if (g_ui.y + kUiBarHeight > wnd->screenPcx16->height)
                        g_ui.y = wnd->screenPcx16->height - kUiBarHeight;
                }
                UiSaveBarPosition_();
                LogInfo("悬浮条拖动完成：(%d,%d)", g_ui.x, g_ui.y);
                return 1;
            } else if (leftUp && (inBar || hitList)) {
                InterlockedExchange(&g_pendingClickX, gameX);
                InterlockedExchange(&g_pendingClickY, gameY);
                LogInfo("点击已吞并：game=(%d,%d)", gameX, gameY);
                return 1;
            } else if (rightDown && (inBar || hitList)) {
                return 1;
            } else if (rightUp && hitList) {
                InterlockedExchange(&g_pendingRightClickX, gameX);
                InterlockedExchange(&g_pendingRightClickY, gameY);
                LogInfo("右键已吞并：game=(%d,%d)", gameX, gameY);
                return 1;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

static void EnsureCombatKeyboardHook_()
{
    if (g_combatKeyboardHook) return;
    HWND gameWindow = *reinterpret_cast<HWND*>(0x699650);
    if (!gameWindow) return;
    const DWORD threadId = GetWindowThreadProcessId(gameWindow, nullptr);
    if (!threadId) return;
    g_combatKeyboardHook = SetWindowsHookExA(WH_KEYBOARD, CombatKeyboardHook_, g_hModule, threadId);
    if (!g_combatMouseHook)
        g_combatMouseHook = SetWindowsHookExA(WH_MOUSE, CombatMouseHook_, g_hModule, threadId);
    LogInfo("战斗输入钩子：键盘=%s 鼠标=%s hwnd=%p",
        g_combatKeyboardHook ? "已安装" : "安装失败",
        g_combatMouseHook ? "已安装" : "安装失败", gameWindow);
}

static int __stdcall Hook_CombatMessage_(HiHook* hook, H3CombatManager* mgr, H3Msg* msg)
{
    static DWORD lastInput = 0;
    const DWORD now = GetTickCount();
    if (msg && now - lastInput > 250) {
        LogInfo("战斗消息：cmd=%d sub=%d item=%d x=%d y=%d",
            (int)msg->command, (int)msg->subtype, msg->itemId, msg->position.x, msg->position.y);
        lastInput = now;
    }
    if (msg && (msg->command == eMsgCommand::KEY_DOWN || msg->command == eMsgCommand::KEY_UP)) {
        const char pressed = UiVirtualKeyToLetter_(msg->subtype, false);
        LogInfo("战斗按键：virtual=%d letter=%c save=%c", msg->subtype,
            pressed ? pressed : '?', g_ui.saveKey);
        const int result = THISCALL_2(int, hook->GetDefaultFunc(), mgr, msg);
        if (g_ui.awaitingRebind) {
            UiHandleRebindKey_(pressed, msg->subtype == h3::NH3VKey::H3VK_ESCAPE);
            return result;
        }
        if (pressed && pressed == g_ui.saveKey)
            TrySaveOrWait_(mgr, result);
        return result;
    }
    // 拖动跟随只走系统 WH_MOUSE 钩子：消息钩子对每条消息（含 cmd=0 空帧
    // position=(0,0)）都更新位置，会把悬浮条拉回 (0,0)，2026-10-05 实测。
    const bool onBar = UiHitBar_(msg);
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
    if (readable && !mgr->finished && mgr->dlg) {
        EnsureCombatKeyboardHook_();
        UiPollRebindKey_();
        static bool keyWasDown = false;
        const bool keyDown = !g_ui.awaitingRebind
            && (GetAsyncKeyState(g_ui.saveKey) & 0x8000) != 0;
        if (keyDown && !keyWasDown)
            InterlockedExchange(&g_pendingSaveKey, g_ui.saveKey);
        keyWasDown = keyDown;
        const LONG clickX = InterlockedExchange(&g_pendingClickX, -1);
        const LONG clickY = InterlockedExchange(&g_pendingClickY, -1);
        if (clickX >= 0 && clickY >= 0)
            UiHandleFrameClick_((int)clickX, (int)clickY);
        const LONG rightX = InterlockedExchange(&g_pendingRightClickX, -1);
        const LONG rightY = InterlockedExchange(&g_pendingRightClickY, -1);
        if (rightX >= 0 && rightY >= 0)
            UiHandleFrameRightClick_((int)rightX, (int)rightY);
        const char pressed = g_ui.awaitingRebind ? 0 : (char)InterlockedExchange(&g_pendingSaveKey, 0);
        if (pressed == g_ui.saveKey)
            TrySaveOrWait_(mgr, 0);
        if (g_uiWaitSaveUntil) {
            const char* waitReason = nullptr;
            if (CombatFullyIdle_(mgr, 0, &waitReason)) {
                g_uiWaitSaveUntil = 0;
                LogInfo("动画已结束，执行存档");
                TryCaptureCombat_();
            } else if (GetTickCount() >= g_uiWaitSaveUntil) {
                LogWarn("等待动画超时（%s），强制存档", waitReason ? waitReason : "unsafe");
                g_uiWaitSaveUntil = 0;
                TryCaptureCombat_();
            }
        }
        UiDrawBar_(mgr);
    }
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

