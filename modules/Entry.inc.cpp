// ========== Entry.inc.cpp ==========
// 插件入口。战斗中存档/读档的 Hook 以后挂在 StartPlugin。

#pragma comment(lib, "version.lib")

static const int GUARD_KEYBOARD = GuardRegisterHook_("BattleStore.Keyboard");
static const int GUARD_MOUSE = GuardRegisterHook_("BattleStore.Mouse");
static const int GUARD_MESSAGE = GuardRegisterHook_("BattleStore.Message");
static const int GUARD_EXECUTE = GuardRegisterHook_("BattleStore.Execute");
static const int GUARD_SPELL = GuardRegisterHook_("BattleStore.Spell");
static LogKeyEdges_ g_commandKeys;
static const int GUARD_CYCLE = GuardRegisterHook_("BattleStore.Cycle");
static const int GUARD_BLT = GuardRegisterHook_("BattleStore.AfterBlt");
static const int GUARD_INIT = GuardRegisterHook_("BattleStore.Init");
static unsigned g_waitKeyEvents = 0;
static unsigned g_waitMouseEvents = 0;
static unsigned g_waitGameEvents = 0;
static const char* g_waitReason = nullptr;


static void LogSelfVersion_()
{
    std::unique_ptr<wchar_t[]> path(new wchar_t[kPathCap_ / 2]());
    wchar_t* wpath = path.get();
    GetModuleFileNameW(g_hModule, wpath, kPathCap_ / 2);
    std::unique_ptr<char[]> utf8Path(new char[kPathCap_]());
    char* utf8 = utf8Path.get();
    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, utf8,
        kPathCap_, nullptr, nullptr);
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
    LogInfo("诊断构建=%s compiled=%s %s pid=%lu archive=%u codec=%u ptr=%u mgrSize=%u stackSize=%u",
        kDiagnosticBuild_, __DATE__, __TIME__, GetCurrentProcessId(), (unsigned)hbs::kFormatVersion,
        kCodecVersion, (unsigned)sizeof(void*), (unsigned)sizeof(H3CombatManager), (unsigned)sizeof(H3CombatCreature));
    LogInfo("[Coverage] codec=%u transactional same-battle restore; mismatch rollback; runtime/UI/RNG trajectory acceptance pending", kCodecVersion);
}

static bool CombatIsReadable_(const H3CombatManager* mgr)
{
    return mgr && !IsBadReadPtr(mgr, sizeof(H3CombatManager));
}

// ===== 存档根 v4（2026-10-06 对齐 HD Folders 的准确规则）=====
// HD 的 UI.Ext.ScenarioMgr.Folders=1（本机默认开）在原版档落 Games\ 根后，
// 再复制一份到 .\games\<目录名>\<年月日>.GM1（热座带 [hotseat] 前缀）。
// 目录名链（HD_SODSrc all_functions_named.c:191556-191640，与 HD dll 串池
// "Unnamed"/"\games\%s"/"%s\%d%d%d%s" 相邻互证）：
//   基础名 = *(char**)(*(DWORD*)0x699538 + 0x1fb40)   // 游戏名（热座=玩家输入；
//                                                        单人遭遇战引擎设为地图名，
//                                                        盘证 Games\{~c}山海界 v1.6 战略版}\）
//   指针 NULL 或空串 → "Unnamed"（HD s_Unnamed_011460e4；盘证 Games\Unnamed\
//   有随机图过天档；"Random" 属 HD.Misc.TournamentSaver 键，与此无关）
//   再经字符清洗（HD FUN_010e2e80 mode=1，花括号颜色码 {~c} 保留）。
// 本插件每次现读该链拼 <游戏根>\Games\<目录名>（用户拍板不缓存），
// 链不可读退回 Games\ 根。路径缓冲一律堆分配（AGENTS.md 规范）。
static bool FindGamesRoot_(std::wstring& gamesRoot)
{
    wchar_t* path = new (std::nothrow) wchar_t[kPathCap_ / 2]();
    if (!path) return false;
    bool ok = false;
    if (GetModuleFileNameW(g_hModule, path, kPathCap_ / 2)) {
        for (int level = 0; level < 8 && !ok; ++level) {
            wchar_t* slash = wcsrchr(path, L'\\');
            if (!slash) slash = wcsrchr(path, L'/');
            if (!slash || slash == path) break;
            *slash = 0;
            std::wstring candidate = std::wstring(path) + L"\\Games";
            const DWORD attrs = GetFileAttributesW(candidate.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                gamesRoot.swap(candidate);
                ok = true;
            }
        }
    }
    delete[] path;
    return ok;
}

// 游戏名（HD 语义）→ Games 下的子目录名。返回 true = folder 可信；
// false = 链不可读（调用方退回 Games\\战场存档）。空名 → "Unnamed"（对齐 HD）。
static bool ReadSaveFolderName_(std::wstring& folder)
{
    folder.clear();
    if (IsBadReadPtr((void*)0x699538, 4)) return false;
    const DWORD base = *(DWORD*)0x699538;
    if (!base || IsBadReadPtr((void*)(base + 0x1fb40), 4)) return false;
    const char* text = *(const char**)(base + 0x1fb40);
    if (!text) {
        folder.assign(L"Unnamed");
        return true;
    }
    if (IsBadStringPtrA(text, 260)) return false;
    char ansi[260];
    lstrcpynA(ansi, text, 260);
    // 去首尾空白
    char* end = ansi + strlen(ansi);
    while (end > ansi && (unsigned char)end[-1] <= ' ') *--end = 0;
    const char* begin = ansi;
    while (*begin && (unsigned char)*begin <= ' ') ++begin;
    if (!*begin) {
        folder.assign(L"Unnamed");
        return true;
    }
    if (strstr(begin, "..") || strchr(begin, ':')) return false;
    for (const char* c = begin; *c; ++c) {
        const unsigned char uc = (unsigned char)*c;
        if (uc < 0x20 || strchr("<>\"|?*", *c)) return false;
    }
    wchar_t wide[130];
    const int chars = MultiByteToWideChar(CP_ACP, 0, begin, -1, wide, 129);
    if (chars <= 1) return false;
    folder.assign(wide);
    return true;
}

static std::wstring ArchiveRoot_()
{
    std::wstring gamesRoot;
    if (!FindGamesRoot_(gamesRoot)) {
        // 兜底（几乎不可达）：DLL 同目录
        wchar_t* path = new (std::nothrow) wchar_t[kPathCap_ / 2]();
        std::wstring result;
        if (path) {
            GetModuleFileNameW(g_hModule, path, kPathCap_ / 2);
            wchar_t* slash = wcsrchr(path, L'\\');
            if (!slash) slash = wcsrchr(path, L'/');
            if (slash) *(slash + 1) = 0;
            else path[0] = 0;
            result = path;
            delete[] path;
        }
        return result + L"战场存档";
    }
    std::wstring folder;
    if (!ReadSaveFolderName_(folder)) {
        static bool warned = false;
        if (!warned) {
            LogWarn("[Archive] 游戏名链(*(DWORD*)0x699538 + 0x1fb40)不可读，使用 Games\\战场存档");
            warned = true;
        }
        folder.clear();
    }
    std::wstring root = gamesRoot;
    if (!folder.empty()) {
        root += L"\\";
        root += folder;
    }
    return root + L"\\战场存档";
}

static bool CombatCanCapture_(const H3CombatManager* mgr, const char** reason)
{
    if (!CombatIsReadable_(mgr)) { if (reason) *reason = "no combat"; return false; }
    if (mgr->finished) { if (reason) *reason = "combat finished"; return false; }
    if (mgr->autoCombat) { if (reason) *reason = "auto combat"; return false; }
    // The H3API tail boolean is shifted to +0x1402F (native auto-retreat),
    // not the actual +0x14030 action byte. Neither is used as a busy gate.
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

static std::string g_preBattleKey_;
static const H3CombatManager* g_preBattleManager_ = nullptr;

static_assert(offsetof(H3Main, mapInfo) + offsetof(H3MapInfo, mapName) == 0x1FB3C,
    "SoD map title layout");

static bool ReadFingerprintMap_(std::string* title)
{
    const H3Main* main = H3Main::Get();
    if (!main || !Readable_(&main->mapInfo.mapName, sizeof(H3String))) return false;
    const H3String& mapName = main->mapInfo.mapName;
    const UINT length = mapName.Length();
    const char* text = mapName.String();
    if (length > kPathCap_ || length > mapName.MaxLength()
        || (length && (!Readable_(text, (size_t)length + 1) || text[length] != 0))) return false;
    title->assign(length ? text : "", length);
    return true;
}

static bool BattleInitialFingerprint_(const H3CombatManager* mgr, std::string* out, std::string* error)
{
    if (!CombatIsReadable_(mgr) || !out) return false;
    std::string mapTitle;
    if (!ReadFingerprintMap_(&mapTitle)) {
        if (error) *error = "战前地图身份不可读取";
        return false;
    }
    std::vector<uint8_t> bytes;
    FingerprintAppendMap_(bytes, mapTitle);
    auto put32 = [&](int32_t value) { FingerprintPut32_(bytes, (uint32_t)value); };
    put32(mgr->landType);
    put32(mgr->specialTerrain);
    put32(mgr->siegeKind2); // Fortification level, not mutable door status at +0x53A4.
    put32(*((const uint8_t*)mgr + 0x53A8));
    put32(*((const uint8_t*)mgr + 0x53A9));
    put32((int32_t)mgr->boatCombat);
    put32(mgr->town ? 1 : 0);
    for (int side = 0; side < 2; ++side) {
        put32(mgr->heroOwner[side]);
        const H3Army* army = mgr->army[side];
        if (!Readable_(army, sizeof(H3Army))) {
            if (error) *error = "战前军队数据不可读取";
            return false;
        }
        put32(mgr->hero[side] ? 1 : 0);
        for (int slot = 0; slot < 7; ++slot) {
            put32(army->type[slot]);
            put32(army->count[slot]);
        }
    }
    // v5 增补（2026-10-06 用户拍板）：双方英雄身份（id/经验/等级）、19 个穿戴槽
    // 宝物 id、城镇身份（编号/类型/归属）。只取开战初态并冻结，用于阻断
    // "同色同阵容的不同英雄"与"穿戴不同导致强度不同"的跨场互读。
    // 机器死亡会移除战斗英雄的对应装备；其动态状态另外进入 codec v7。
    // Only called before native battle initialization. Mutable combat slots and
    // obstacles never participate, and subsequent requests use the frozen digest.
    for (int side = 0; side < 2; ++side) {
        const H3Hero* hero = mgr->hero[side];
        if (hero && !Readable_(hero, sizeof(H3Hero))) hero = nullptr;
        put32(hero ? hero->id : -1);
        put32(hero ? hero->experience : -1);
        put32(hero ? (int32_t)hero->level : -1);
        for (int slot = 0; slot < 19; ++slot)
            put32(hero ? hero->bodyArtifacts[slot].id : -1);
    }
    const H3Town* town = mgr->town;
    if (town && !Readable_(town, sizeof(H3Town))) town = nullptr;
    put32(town ? (int32_t)town->number : -1);
    put32(town ? (int32_t)town->type : -1);
    put32(town ? (int32_t)town->owner : -1);
    if (!Sha256Hex_(bytes.data(), bytes.size(), out)) {
        if (error) *error = "battle fingerprint failed";
        return false;
    }
    return true;
}

static bool BattleFingerprint_(const H3CombatManager* mgr, std::string* out, std::string* error)
{
    if (!out || mgr != g_preBattleManager_ || g_preBattleKey_.empty()) {
        if (error) *error = "未取得本场战斗的战前指纹，请重新进入战斗";
        return false;
    }
    *out = g_preBattleKey_;
    return true;
}

// 返回 true = 落盘且回读校验通过（悬浮条已打"已存档"戳）；
// false = 任一环节失败（拒绝/采集/编码/写盘/回读），调用方负责悬浮条提示。
static bool TryCaptureCombat_()
{
    const H3CombatManager* mgr = H3CombatManager::Get();
    if (!g_diag.id) DiagBegin_("save", "direct", mgr);
    DiagStage_("save.gate");
    const char* reason = nullptr;
    if (!CombatCanCapture_(mgr, &reason)) {
        DiagState_(mgr, "save-rejected");
        DiagEnd_("rejected", reason);
        return false;
    }
    std::unique_ptr<CodecCapture> captureStorage(new CodecCapture{});
    CodecCapture& capture = *captureStorage;
    std::string error;
    DiagStage_("save.capture");
    if (!CaptureBattle_(mgr, &capture, &error)) {
        DiagEnd_("failed", error.c_str());
        return false;
    }
    DiagSummary_(capture, "captured");
    hbs::ArchiveDocument document;
    DiagStage_("save.fingerprint");
    if (!BattleFingerprint_(mgr, &document.battleKey, &error)) {
        DiagEnd_("failed", error.c_str());
        return false;
    }
    LogDebug("[Archive op=%ld] battle=%s target=placeholder-zero", g_diag.id, document.battleKey.c_str());
    DiagStage_("save.encode");
    CodecInvalidateHover_(&capture);
    if (!CodecEncode(capture, &document.sections, &error)) {
        DiagEnd_("failed", error.c_str());
        return false;
    }
    DiagSections_(document.sections, "encoded");
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
    hbs::ArchiveRecord committed;
    DiagStage_("save.write");
    LogDebug("[Archive op=%ld] root=%s timestamp=%llu", g_diag.id,
        DiagUtf8_(ArchiveRoot_()).c_str(), document.timestampUtcMs);
    if (!store.Save(document, storeError, &committed)) {
        DiagEnd_("failed", DiagUtf8_(storeError).c_str());
        return false;
    }
    LogInfo("[Archive op=%ld] persisted=1 path=%s sequence=%u", g_diag.id,
        DiagUtf8_(committed.path).c_str(), committed.sequence);
    DiagStage_("save.readback");
    hbs::ArchiveDocument readback;
    if (!store.Load(committed, readback, storeError)) {
        DiagEnd_("persisted-unverified", DiagUtf8_(storeError).c_str());
        return false;
    }
    bool equal = readback.sections.size() == document.sections.size();
    for (size_t i = 0; i < document.sections.size(); ++i) {
        const hbs::ArchiveSection* actual = FindSection_(readback.sections, document.sections[i].id);
        equal = equal && actual && CodecSectionEqual_(document.sections[i], *actual, nullptr);
    }
    WriteLogLv(equal ? LOG_INFO : LOG_ERROR, "[Archive op=%ld] readback_crc=ok payload_equal=%d", g_diag.id, equal ? 1 : 0);
    equal = equal && readback.battleKey == committed.battleKey
        && readback.targetKey == committed.targetKey
        && readback.timestampUtcMs == committed.timestampUtcMs
        && readback.sequence == committed.sequence;
    if (!equal) { DiagEnd_("persisted-unverified", "readback payload mismatch"); return false; }
    std::unique_ptr<CodecCapture> decodedStorage(new CodecCapture{});
    CodecCapture& decoded = *decodedStorage;
    if (!CodecDecode(readback.sections, &decoded, &error)) {
        DiagEnd_("persisted-unverified", error.c_str()); return false;
    }
    DiagSummary_(decoded, "readback");
    g_uiPort->MarkSaved(document.timestampUtcMs);
    // 保存成功后刷新常驻列表，第一项就是刚存的档。
    //（列表按时间戳倒序，最新必在首位）；提示可有可无，列表不能少。

    g_uiPort->ReloadEntries(mgr);
    DiagEnd_("ok", "persisted and readback verified; runtime restore not verified");
    return true;
}



// 保存请求入口 StoreRequestSave_ 已随第3步迁入服务层；三个触发点
// （game-message / system-frame / system-key 投递）都在界面事件内。


static HHOOK g_combatKeyboardHook = nullptr;
static HHOOK g_combatMouseHook = nullptr;

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

// 翻译层（第3步断直连）：钩子事件转 POD 交界面实现，钩子层只做守卫与翻译。
static bool CombatKeyboardBody_(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION) {
        if (g_restoreBusy || g_restoreFatal || !BattleMainDialog_(H3CombatManager::Get())) return false;
        if (g_uiWaitSaveUntil) { ++g_waitKeyEvents; return true; }
        UiKeyEvent_ e = {};
        e.vk = (int)wParam;
        e.up = (lParam & 0x80000000) != 0;
        e.repeat = (lParam & 0x40000000) != 0;
        e.down = !e.up;
        e.source = 0;
        return g_uiPort->OnSystemKey(e);
    }
    return false;
}

static void DiagHookFault_()
{
    __try {
        if (g_diag.id || g_uiWaitSaveUntil) GuardLog_(
            "[Op %ld] outcome=%s stage=%s side=%d slot=%d writing=%d input_lock=0 no rollback",
            g_diag.id, g_diag.writing ? "partial-write" : "exception",
            g_diag.stage ? g_diag.stage : "none", g_diag.side, g_diag.slot, g_diag.writing ? 1 : 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (g_diag.writing) g_restoreFatal = true;
    g_restoreBusy = false;
    g_restoreRequest.pending = false;
    g_diag.id = 0;
    g_diag.writing = false;
    g_uiWaitSaveUntil = 0;
    g_uiPort->OnFaultCleanup();
}

static LRESULT CALLBACK CombatKeyboardHook_(int code, WPARAM wParam, LPARAM lParam)
{
    __try { if (CombatKeyboardBody_(code, wParam, lParam)) return 1; }
    __except (GuardCrashFilter_(GUARD_KEYBOARD, GetExceptionInformation())) { DiagHookFault_(); }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// 翻译层：事件种类识别 + 战场守卫 + 屏幕→游戏坐标，交界面实现。
static bool CombatMouseBody_(int code, WPARAM wParam, LPARAM lParam)
{
    if (code != HC_ACTION) return false;
    const bool leftDown = BattleUiLeftDown_(static_cast<unsigned>(wParam));
    const bool leftUp = wParam == WM_LBUTTONUP;
    const bool rightDown = BattleUiRightDown_(static_cast<unsigned>(wParam));
    const bool rightUp = wParam == WM_RBUTTONUP;
    const bool move = wParam == WM_MOUSEMOVE;
    const bool wheel = wParam == WM_MOUSEWHEEL;
    if (g_uiWaitSaveUntil) { ++g_waitMouseEvents; return true; }
    if (!leftDown && !leftUp && !rightDown && !rightUp && !move && !wheel) return false;
    H3CombatManager* combat = H3CombatManager::Get();
    const bool combatOpen = !(g_restoreBusy || !BattleMainDialog_(combat) || combat->finished || g_restoreFatal);
    UiMouseEvent_ e = {};
    e.kind = move ? 0 : leftDown ? 1 : leftUp ? 2 : rightDown ? 3 : rightUp ? 4 : 5;
    if (combatOpen) {
        const MOUSEHOOKSTRUCT* mouse = reinterpret_cast<const MOUSEHOOKSTRUCT*>(lParam);
        int gameX = 0, gameY = 0;
        if (!mouse || !UiGamePointFromScreen_(mouse->pt, &gameX, &gameY)) return false;
        e.gameX = gameX;
        e.gameY = gameY;
        if (wheel) {
            const auto extended = reinterpret_cast<const MOUSEHOOKSTRUCTEX*>(lParam);
            e.wheelDelta = static_cast<short>(HIWORD(extended->mouseData));
        }
    }
    return g_uiPort->OnSystemMouse(e, combatOpen);
}

static LRESULT CALLBACK CombatMouseHook_(int code, WPARAM wParam, LPARAM lParam)
{
    __try { if (CombatMouseBody_(code, wParam, lParam)) return 1; }
    __except (GuardCrashFilter_(GUARD_MOUSE, GetExceptionInformation())) { DiagHookFault_(); }
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
    const DWORD keyboardError = g_combatKeyboardHook ? ERROR_SUCCESS : GetLastError();
    DWORD mouseError = ERROR_SUCCESS;
    if (!g_combatMouseHook) {
        g_combatMouseHook = SetWindowsHookExA(WH_MOUSE, CombatMouseHook_, g_hModule, threadId);
        if (!g_combatMouseHook) mouseError = GetLastError();
    }
    static LogRepeatGate_ hookFailures;
    const bool installed = g_combatKeyboardHook && g_combatMouseHook;
    unsigned skipped = 0;
    if (installed || hookFailures.Admit(GetTickCount(), 30000, &skipped)) {
        if (installed) skipped = hookFailures.suppressed;
        WriteLogLv(installed ? LOG_INFO : LOG_ERROR,
            "[Hooks] keyboard=%d mouse=%d keyboard_error=%lu mouse_error=%lu hwnd=%p tid=%lu suppressed=%u",
            g_combatKeyboardHook ? 1 : 0, g_combatMouseHook ? 1 : 0,
            keyboardError, mouseError, gameWindow, threadId, skipped);
    }
    if (installed) hookFailures = {};
}

static bool CombatMessageBefore_(H3Msg* msg, int inputLevel)
{
    if (g_uiWaitSaveUntil && msg) {
        const int cmd = (int)msg->command;
        if (cmd == (int)eMsgCommand::KEY_DOWN || cmd == (int)eMsgCommand::KEY_UP
            || cmd == (int)eMsgCommand::MOUSE_BUTTON || cmd == (int)eMsgCommand::MOUSE_OVER) {
            ++g_waitGameEvents;
            return true;
        }
    }
    if (msg && (msg->command == eMsgCommand::KEY_DOWN || msg->command == eMsgCommand::KEY_UP)) {
        g_uiPort->OnGameKeyBefore(msg, inputLevel);
        return false;
    }
    // Native hotkeys become 0x200 item commands, not overlay mouse clicks.
    return g_uiPort->OnGameMouse(msg);
}

static void DiagCommand_(const H3CombatManager* mgr, const H3Msg& input,
    const H3Msg* translated, const char* phase, int result, int level)
{
    if (!LogEnabled_(LOG_DEBUG) || !CombatIsReadable_(mgr)) return;
    char text[512];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "[Command] phase=%s generation=%u depth=%d bdepth=%d cmd=%d subtype=%d item=%d pos=%d,%d translated=%d/%d/%d result=%d action=%d/%d/%d/%d current=%d:%d activeSide=%d rebind=%d control=%d",
        phase, g_battleGeneration, g_messageDepth, g_messageFrames.Depth(g_battleGeneration), (int)input.command, (int)input.subtype,
        input.itemId, input.position.x, input.position.y,
        translated ? (int)translated->command : -1, translated ? (int)translated->subtype : -1,
        translated ? translated->itemId : -1, result, (int)mgr->action,
        mgr->actionParameter, mgr->actionTarget, mgr->actionParameter2,
        mgr->currentMonSide, mgr->currentMonIndex, mgr->currentActiveSide,
        g_uiPort->IsRebindWaiting() ? 1 : 0, *((const int32_t*)((const uint8_t*)mgr + 0x132B4)));
    LogDetail_("%s", text);
    if (level == LOG_DEBUG) LogDebug("%s", text);
}

static int __stdcall Hook_CombatMessage_(HiHook* hook, H3CombatManager* mgr, H3Msg* msg)
{
    BattleMessageFrame_ frame = {};
    // 不在这里读游戏对象；身份捕获在下方自有逻辑的SEH内。
    const unsigned long long frameToken = g_messageFrames.Enter(frame, g_battleGeneration, mgr, nullptr);
    ++g_messageDepth;
    int result = 0;
    H3Msg keyboardInput = {};
    bool hasKeyboardInput = false;
    H3Msg commandInput = {};
    bool hasCommandInput = false;
    int inputLevel = LOG_TRACE;
    __try {
        bool failed = false, consumed = false;
        __try {
            if (msg && BattleIsKeyboardMessage_((int)msg->command)) {
                keyboardInput = *msg;
                hasKeyboardInput = true;
                const int edgeLevel = g_commandKeys.Level((int)keyboardInput.subtype,
                    keyboardInput.command == eMsgCommand::KEY_DOWN);
                const char letter = UiVirtualKeyToLetter_(keyboardInput.subtype, false);
                inputLevel = LogCommandLevel_(g_uiPort->IsRebindWaiting() || (letter && letter == g_store.saveKey)
                    || keyboardInput.subtype == h3::NH3VKey::H3VK_SPACEBAR, edgeLevel);
            }
            if (!g_restoreBusy && !g_restoreFatal && BattleMainDialog_(mgr)) {
                if (LogEnabled_(LOG_DEBUG) && msg
                    && (hasKeyboardInput || ((int)msg->command == 0x200
                        && ((int)msg->subtype == 0xC || (int)msg->subtype == 0xD)))) {
                    commandInput = *msg;
                    if (!hasKeyboardInput && (msg->itemId == 0x7D8 || msg->itemId == 0x7D9 || msg->itemId == 0x7DA))
                        inputLevel = LOG_DEBUG;
                    hasCommandInput = true;
                    DiagCommand_(mgr, commandInput, nullptr, "before", 0, inputLevel);
                }
                if (inputLevel == LOG_DEBUG && hasKeyboardInput && keyboardInput.subtype == h3::NH3VKey::H3VK_SPACEBAR)
                    DiagInputState_(mgr, keyboardInput.command == eMsgCommand::KEY_DOWN ? "space-before-down" : "space-before-up", 0);
                consumed = CombatMessageBefore_(msg, inputLevel);
            }
            // 帧身份补全：进入时未读游戏对象，首次确认可读时记录对话框；
            // 若原函数期间对话框被替换，返回后的边界核验会拒绝消费（保守）。
            // 登记制帧表：就地修改后必须 Update 同步（2026-10-10 崩溃修复）。
            if (frame.dialog == nullptr && CombatIsReadable_(mgr)) {
                frame.dialog = mgr->dlg;
                g_messageFrames.Update(frame, frameToken);
            }
        }
        __except (GuardCrashFilter_(GUARD_MESSAGE, GetExceptionInformation())) { failed = true; DiagHookFault_(); }
        result = consumed ? 1 : THISCALL_2(int, hook->GetDefaultFunc(), mgr, msg);
        if (!failed) {
            __try {
                frame.nativeReturned = true;
                g_messageFrames.Update(frame, frameToken);
                if (hasCommandInput) DiagCommand_(mgr, commandInput, msg, consumed ? "overlay-consumed" : "after", result, inputLevel);
                if (!g_restoreBusy && !g_restoreFatal
                    && g_messageFrames.Boundary(g_battleGeneration, mgr,
                        CombatIsReadable_(mgr) ? mgr->dlg : nullptr)) {
                    // The native dialog mutates msg into item commands in place.
                    if (hasKeyboardInput) g_uiPort->OnGameKeyAfter(mgr, &keyboardInput, result, GetTickCount(), g_uiWaitSaveUntil != 0);
                    if (inputLevel == LOG_DEBUG && hasKeyboardInput && keyboardInput.subtype == h3::NH3VKey::H3VK_SPACEBAR
                        && BattleMainDialog_(mgr))
                        DiagInputState_(mgr, keyboardInput.command == eMsgCommand::KEY_DOWN ? "space-after-down" : "space-after-up", result);
                    g_uiPort->ProcessRestore(mgr, result);
                }
            }
            __except (GuardCrashFilter_(GUARD_MESSAGE, GetExceptionInformation())) { DiagHookFault_(); }
        }
    }
    __finally {
        --g_messageDepth;
        g_messageFrames.Leave(frame, frameToken);
    }
    if (g_restoreFatal) {
        GuardLog_("[Restore] FATAL: unverified partial write; stopping instead of continuing battle");
        RaiseException(0xE0424842, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }
    return result;
}


static LogActivityWindow_ g_executeLog, g_spellLog;
static void DiagActivityFlush_(const char* reason, bool force)
{
    const DWORD now = GetTickCount();
    if (g_executeLog.Due(now, force)) {
        LogDebug("[Activity] kind=execute generation=%u reason=%s count=%u max_depth=%d last_action=%d last_result=%d",
            g_battleGeneration, reason, g_executeLog.count, g_executeLog.maxDepth, g_executeLog.lastId, g_executeLog.lastResult);
        g_executeLog.Reported(now, force);
    }
    if (g_spellLog.Due(now, force)) {
        LogDebug("[Activity] kind=spell generation=%u reason=%s count=%u max_depth=%d last_spell=%d",
            g_battleGeneration, reason, g_spellLog.count, g_spellLog.maxDepth, g_spellLog.lastId);
        g_spellLog.Reported(now, force);
    }
}

static int __stdcall Hook_BattleExecute_(HiHook* hook, H3CombatManager* mgr, int parameter)
{
    ++g_executorDepth;
    int result = 0;
    int actionBefore = -1;
    // finally only repairs depth; exceptions from the original still propagate.
    __try {
        __try {
            if (LogEnabled_(LOG_DEBUG) && CombatIsReadable_(mgr)) {
                actionBefore = (int)mgr->action;
                g_executeLog.Observe(GetTickCount(), actionBefore, g_executorDepth);
                LogDetail_("[Execute] begin generation=%u depth=%d action=%d/%d/%d/%d turn=%d current=%d:%d active=%p",
                    g_battleGeneration, g_executorDepth, actionBefore, mgr->actionParameter,
                    mgr->actionTarget, mgr->actionParameter2, mgr->turn,
                    mgr->currentMonSide, mgr->currentMonIndex, mgr->activeStack);
                if (g_battleInitialized) LogDebug("[Action] generation=%u depth=%d action=%d/%d/%d/%d turn=%d current=%d:%d active=%p",
                    g_battleGeneration, g_executorDepth, actionBefore, mgr->actionParameter,
                    mgr->actionTarget, mgr->actionParameter2, mgr->turn,
                    mgr->currentMonSide, mgr->currentMonIndex, mgr->activeStack);
            }
        }
        __except (GuardCrashFilter_(GUARD_EXECUTE, GetExceptionInformation())) {}
        result = THISCALL_2(int, hook->GetDefaultFunc(), mgr, parameter);
        __try {
            if (actionBefore >= 0 && CombatIsReadable_(mgr)) {
                g_executeLog.lastResult = result;
                LogDetail_("[Execute] end generation=%u depth=%d action_before=%d action_after=%d result=%d turn=%d current=%d:%d active=%p",
                    g_battleGeneration, g_executorDepth, actionBefore, (int)mgr->action,
                    result, mgr->turn, mgr->currentMonSide, mgr->currentMonIndex, mgr->activeStack);
                if (result == 2) LogDebug("[Execute] close generation=%u depth=%d action=%d current=%d:%d",
                    g_battleGeneration, g_executorDepth, actionBefore, mgr->currentMonSide, mgr->currentMonIndex);
            }
        }
        __except (GuardCrashFilter_(GUARD_EXECUTE, GetExceptionInformation())) {}
    }
    __finally { --g_executorDepth; }
    return result;
}

static void __stdcall Hook_BattleCastSpell_(HiHook* hook, H3CombatManager* mgr,
    int spell, int hex, int castType, int secondHex, int expertise, int power)
{
    ++g_spellDepth;
    // Finally repairs tracking only; native exceptions must propagate unchanged.
    __try {
        __try {
            if (LogEnabled_(LOG_DEBUG)) g_spellLog.Observe(GetTickCount(), spell, g_spellDepth);
            LogDetail_("[Spell] begin generation=%u depth=%d spell=%d hex=%d cast_type=%d",
                g_battleGeneration, g_spellDepth, spell, hex, castType);
            if (g_battleInitialized) LogDebug("[Cast] generation=%u depth=%d spell=%d hex=%d cast_type=%d",
                g_battleGeneration, g_spellDepth, spell, hex, castType);
        }
        __except (GuardCrashFilter_(GUARD_SPELL, GetExceptionInformation())) {}
        THISCALL_7(void, hook->GetDefaultFunc(), mgr, spell, hex, castType, secondHex, expertise, power);
        __try {
            LogDetail_("[Spell] end generation=%u depth=%d spell=%d", g_battleGeneration, g_spellDepth, spell);
        }
        __except (GuardCrashFilter_(GUARD_SPELL, GetExceptionInformation())) {}
    }
    __finally { --g_spellDepth; }
}

static void BattleReset_()
{
    GuardFlushFaults_("battle-reset", true);
    DiagActivityFlush_("battle-reset", true);
    ++g_battleGeneration;
    g_battleInitialized = false;
    g_preBattleKey_.clear();
    g_preBattleManager_ = nullptr;
    g_battleListDirty = true;
    g_restoreRequest.pending = false;
    g_restoreFatal = false;
    // 消息登记不追栈帧链；下一代 Enter 清理旧代元数据，旧调用迟到的
    // Update/Leave 用 token 核验，不会误操作同地址的新登记。
    g_store.entries.clear();
    g_store.battleKey.clear();
    if (g_storeListFailure.seen) LogWarn("[List] reset suppressed=%u", g_storeListFailure.suppressed);
    g_storeListFailure = {};
    g_commandKeys = {};
    g_uiPort->OnBattleReset();
}

static const int GUARD_LIFECYCLE = GuardRegisterHook_("BattleStore.Lifecycle");
static int __stdcall Hook_BattleStart_(HiHook* hook, H3CombatManager* mgr, int parameter)
{
    __try {
        BattleReset_();
        g_battleThread = GetCurrentThreadId();
        if (BattleInitialFingerprint_(mgr, &g_preBattleKey_, nullptr)) g_preBattleManager_ = mgr;
        else LogWarn("[Battle] fingerprint failed generation=%u mgr=%p", g_battleGeneration, mgr);
    }
    __except (GuardCrashFilter_(GUARD_LIFECYCLE, GetExceptionInformation())) { DiagHookFault_(); }
    const int result = THISCALL_2(int, hook->GetDefaultFunc(), mgr, parameter);
    __try {
        g_battleInitialized = CombatIsReadable_(mgr) && mgr->dlg && !mgr->finished;
        LogInfo("[Battle] start generation=%u mgr=%p initialized=%d result=%d key=%s",
            g_battleGeneration, mgr, g_battleInitialized ? 1 : 0, result, g_preBattleKey_.c_str());
    }
    __except (GuardCrashFilter_(GUARD_LIFECYCLE, GetExceptionInformation())) { DiagHookFault_(); }
    return result;
}

static void __stdcall Hook_BattleStop_(HiHook* hook, H3CombatManager* mgr)
{
    __try {
        LogInfo("[Battle] stop generation=%u mgr=%p pending_load=%d", g_battleGeneration, mgr, g_restoreRequest.pending ? 1 : 0);
        BattleReset_();
    }
    __except (GuardCrashFilter_(GUARD_LIFECYCLE, GetExceptionInformation())) { DiagHookFault_(); }
    THISCALL_1(void, hook->GetDefaultFunc(), mgr);
}
// Hook_AfterBlt @ 0x600430（H3BattleValueInfo 同款）：backbuffer Blt 完成后
// 补画悬浮条。只靠 CycleCombatScreen 画会被后续 Blt 覆盖，表现为战场框内
// 闪烁（2026-10-05 用户实测）。
static int __stdcall Hook_AfterBlt_(LoHook* h, HookContext* c)
{
    (void)h;
    (void)c;
    __try {
        H3CombatManager* mgr = H3CombatManager::Get();
        if (!g_restoreBusy && !g_restoreFatal && BattleMainDialog_(mgr) && !mgr->finished)
            g_uiPort->Draw(mgr);
    }
    __except (GuardCrashFilter_(GUARD_BLT, GetExceptionInformation())) {}
    return EXEC_DEFAULT;
}

// 悬停即时刷新日志等级行高亮：面板 mouse-over 事件频率不可靠（H3Auto 教训），
// 每帧按光标位置重算；Draw 每帧全量重画，无需额外失效。（实现在 HdNativeUi::PollHover）

static void CombatCycleAfter_(H3CombatManager* mgr, int result)
{
    GuardFlushFaults_("cycle", false);
    DiagActivityFlush_("cycle", false);
    static DWORD lastFrame = 0;
    static H3CombatManager* lastManager = nullptr;
    static H3CombatDlg* lastDialog = nullptr;
    static unsigned lastGeneration = ~0u;
    const DWORD now = GetTickCount();
    const bool readable = CombatIsReadable_(mgr);
    H3CombatDlg* dialog = readable ? mgr->dlg : nullptr;
    const bool changed = mgr != lastManager || dialog != lastDialog || g_battleGeneration != lastGeneration;
    if (changed || (LogEnabled_(LOG_TRACE) && now - lastFrame >= 30000)) {
        WriteLogLv(changed ? LOG_DEBUG : LOG_TRACE,
            "[Frame] generation=%u mgr=%p readable=%d finished=%d dlg=%p cycleResult=%d",
            g_battleGeneration, mgr, readable ? 1 : 0,
            readable ? (mgr->finished ? 1 : 0) : -1, dialog, result);
        lastFrame = now;
        lastManager = mgr;
        lastDialog = dialog;
        lastGeneration = g_battleGeneration;
    }
    // 读档请求独立维护：超时/换场/结束在这里取消，不受消息深度与恢复安全点
    // 门控（2026-10-10 玩家日志：消费入口被绝对深度阻断时请求挂起五分多秒）。
    g_uiPort->MaintainRestore(mgr);
    if (readable && !mgr->finished && mgr->dlg) {
        if (g_battleInitialized && g_battleListDirty && BattleMainDialog_(mgr)) {
            if (g_uiPort->ReloadEntries(mgr)) {
                g_battleListDirty = false;
                g_uiPort->OnListReloaded();

                LogInfo("[List] 战斗开始自动加载完成，常驻列表条数=%u generation=%u",
                    (unsigned)g_store.entries.size(), g_battleGeneration);
            }
        }
        if (g_restoreBusy || g_restoreFatal || !BattleMainDialog_(mgr)) return;
        EnsureCombatKeyboardHook_();
        g_uiPort->PollRebindKey();
        g_uiPort->PollHover();
        // 键轮询（改键保护窗、闩锁重 arm、存档键边沿触发）整体在界面事件内。
        g_uiPort->OnFrameKeyPoll(mgr, now, g_uiWaitSaveUntil != 0);
        g_uiPort->FrameClick();   // 消费系统钩子投递的挂起点击（列表/改键/日志等级）
        g_uiPort->Draw(mgr);
    }
    else {
        CancelSaveWait_("combat unavailable or finished");
    }
}

static void __stdcall Hook_CycleCombatScreen_(HiHook* hook, H3CombatManager* mgr)
{
    THISCALL_1(void, hook->GetDefaultFunc(), mgr);
    __try { CombatCycleAfter_(mgr, 0); }
    __except (GuardCrashFilter_(GUARD_CYCLE, GetExceptionInformation())) { DiagHookFault_(); }
}

static void StartPlugin()
{
    LogSelfVersion_();
    if (!GuardVerifySodBytes_()) {
        LogError("SoD 版本门卫失败：已停用全部钩子");
        return;
    }
    UiLoadBarPosition_();
    _PI->WriteHiHook(0x462600, SPLICE_, EXTENDED_, THISCALL_, Hook_BattleStart_);
    _PI->WriteHiHook(0x462E40, SPLICE_, EXTENDED_, THISCALL_, Hook_BattleStop_);
    _PI->WriteHiHook(0x4786B0, SPLICE_, EXTENDED_, THISCALL_, Hook_BattleExecute_);
    _PI->WriteHiHook(0x5A0140, SPLICE_, EXTENDED_, THISCALL_, Hook_BattleCastSpell_);
    _PI->WriteHiHook(0x473A00, SPLICE_, EXTENDED_, THISCALL_, Hook_CombatMessage_);
    _PI->WriteHiHook(0x495C50, SPLICE_, EXTENDED_, THISCALL_, Hook_CycleCombatScreen_);
    _PI->WriteLoHook(0x600430, Hook_AfterBlt_);
    LogInfo("战斗存档：codec=%u 可逆事务已启用，实机恢复验收尚未完成。", kCodecVersion);
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
        GuardSetLogPathW(g_disable_log ? nullptr : g_log_path_w);
        InstallCrashGuard();
        LogInfo("战斗存档 loading.");
        _P = GetPatcher();
        if (!_P) { LogError("GetPatcher failed."); return TRUE; }
        _PI = _P->CreateInstance("HD.Plugin.H3BattleStore");
        if (!_PI) { LogError("CreateInstance failed."); return TRUE; }
        ReadConfig();
        __try { StartPlugin(); }
        __except (GuardCrashFilter_(GUARD_INIT, GetExceptionInformation())) {}
    }
    if (reason == DLL_PROCESS_DETACH) {
        __try { DiagActivityFlush_("detach", true); LogRecentContext_("detach"); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (g_combatKeyboardHook) UnhookWindowsHookEx(g_combatKeyboardHook);
        if (g_combatMouseHook) UnhookWindowsHookEx(g_combatMouseHook);
        GuardShutdown();
    }
    return TRUE;
}

