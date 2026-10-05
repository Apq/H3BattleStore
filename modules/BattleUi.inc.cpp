// ========== BattleUi.inc.cpp ==========
// 战场悬浮条：存档下拉列表、快捷键显示、点击展开/收起、拖动与右键删除。
// 绘制在 0x495C50 动画循环返回后，不占战场格子；交互在消息钩子内完成。

#include <ctime>
#include <string>

static void UiToGbk_(const char* utf8, char* out, int outCap)
{
    if (!utf8 || !out || outCap <= 0) return;
    out[0] = 0;
    const int wideCap = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
    if (wideCap <= 0) return;
    WCHAR* wide = new (std::nothrow) WCHAR[wideCap]();
    if (!wide) return;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide, wideCap) > 0)
        WideCharToMultiByte(936, 0, wide, -1, out, outCap, nullptr, nullptr);
    delete[] wide;
    out[outCap - 1] = 0;
}

static char UiVirtualKeyToLetter_(int virtualKey, bool windowsVk)
{
    if (windowsVk) {
        switch (virtualKey) {
        case 'B': return 'B';
        case 'F': return 'F';
        case 'G': return 'G';
        case 'K': return 'K';
        case 'M': return 'M';
        case 'N': return 'N';
        case 'U': return 'U';
        case 'V': return 'V';
        case 'X': return 'X';
        case 'Y': return 'Y';
        default: return 0;
        }
    }
    switch (virtualKey) {
    case h3::NH3VKey::H3VK_B: return 'B';
    case h3::NH3VKey::H3VK_F: return 'F';
    case h3::NH3VKey::H3VK_G: return 'G';
    case h3::NH3VKey::H3VK_K: return 'K';
    case h3::NH3VKey::H3VK_M: return 'M';
    case h3::NH3VKey::H3VK_N: return 'N';
    case h3::NH3VKey::H3VK_U: return 'U';
    case h3::NH3VKey::H3VK_V: return 'V';
    case h3::NH3VKey::H3VK_X: return 'X';
    case h3::NH3VKey::H3VK_Y: return 'Y';
    default: return 0;
    }
}

static const int kUiBarHeight = 24;
static const int kUiBarWidth = 360;
static const int kUiRowHeight = 18;
static const int kUiListMaxRows = 10;
static const int kUiDefaultX = 16;
static const int kUiDefaultY = 4;

struct UiSaveEntry
{
    uint64_t timestampUtcMs;
    uint32_t sequence;
    std::wstring path;
};

static struct
{
    int x = kUiDefaultX;
    int y = kUiDefaultY;
    bool listOpen = false;
    bool dragging = false;
    int dragOffX = 0;
    int dragOffY = 0;
    char saveKey = 'G';
    bool awaitingRebind = false;
    char lastSavedStamp[32] = {};
    DWORD lastSavedUntil = 0;
    std::string battleKey;
    std::vector<UiSaveEntry> entries;
    int hoverRow = -1;
} g_ui;

static void UiReloadEntries_(const H3CombatManager* mgr)
{
    g_ui.entries.clear();
    std::string battleKey;
    std::string error;
    if (!BattleFingerprint_(mgr, &battleKey, &error)) return;
    g_ui.battleKey = battleKey;
    hbs::ArchiveStore store(ArchiveRoot_());
    std::vector<hbs::ArchiveRecord> records;
    std::wstring storeError;
    if (!store.List(battleKey, "", records, storeError)) return;
    g_ui.entries.reserve(records.size());
    for (size_t i = 0; i < records.size(); ++i) {
        UiSaveEntry entry;
        entry.timestampUtcMs = records[i].timestampUtcMs;
        entry.sequence = records[i].sequence;
        entry.path = records[i].path;
        g_ui.entries.push_back(entry);
    }
}

static void UiSaveBarPosition_()
{
    char xText[16] = {};
    char yText[16] = {};
    _snprintf(xText, sizeof(xText), "%d", g_ui.x);
    _snprintf(yText, sizeof(yText), "%d", g_ui.y);
    IniWriteKeyUtf8(g_user_ini_path, "Ui", "BarX", xText);
    IniWriteKeyUtf8(g_user_ini_path, "Ui", "BarY", yText);
}

static bool UiKeyIsFree_(char key);
static void UiLoadBarPosition_()
{
    g_ui.x = IniReadIntUtf8(g_user_ini_path, "Ui", "BarX", kUiDefaultX);
    g_ui.y = IniReadIntUtf8(g_user_ini_path, "Ui", "BarY", kUiDefaultY);
    char keyText[16] = {};
    IniReadUtf8(g_user_ini_path, "Hotkeys", "SaveKey", "G", keyText, sizeof(keyText));
    if (UiKeyIsFree_(keyText[0])) g_ui.saveKey = keyText[0];
    else g_ui.saveKey = 'G';
}

static void UiSaveHotkey_()
{
    char keyText[8] = {};
    _snprintf(keyText, sizeof(keyText), "%c", g_ui.saveKey);
    IniWriteKeyUtf8(g_user_ini_path, "Hotkeys", "SaveKey", keyText);
}

static void UiFormatStamp_(const UiSaveEntry& entry, char* out, size_t cap)
{
    const uint64_t ms = entry.timestampUtcMs;
    const uint64_t seconds = ms / 1000;
    struct tm value = {};
    time_t clock = (time_t)seconds;
    value = *gmtime(&clock);
    _snprintf(out, cap, "%04d%02d%02d-%02d%02d%02d",
        value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
        value.tm_hour, value.tm_min, value.tm_sec);
}

static void UiMarkSaved_(uint64_t timestampUtcMs)
{
    UiSaveEntry entry = {};
    entry.timestampUtcMs = timestampUtcMs;
    UiFormatStamp_(entry, g_ui.lastSavedStamp, sizeof(g_ui.lastSavedStamp));
    g_ui.lastSavedUntil = GetTickCount() + 3000;
}

static void UiDrawBar_(H3CombatManager* mgr)
{
    H3WindowManager* wnd = H3WindowManager::Get();
    static DWORD lastMissing = 0;
    const BYTE* dlgBytes = reinterpret_cast<const BYTE*>(mgr ? mgr->dlg : nullptr);
    H3LoadedPcx16* dlgScreen = dlgBytes
        ? *reinterpret_cast<H3LoadedPcx16* const*>(dlgBytes + 0x44)
        : nullptr;
    H3LoadedPcx16* screen = dlgScreen ? dlgScreen : (wnd ? wnd->screenPcx16 : nullptr);
    if (!screen) {
        const DWORD now = GetTickCount();
        if (now - lastMissing > 1000) {
            LogWarn("悬浮条未绘制：战斗对话框和窗口屏幕缓冲都不可用");
            lastMissing = now;
        }
        return;
    }
    H3Font* font = H3SmallFont::Get();
    if (!font) {
        LogWarn("悬浮条未绘制：小字体不可用");
        return;
    }
    const int x = g_ui.x;
    const int y = g_ui.y;
    screen->FillRectangle(x, y, kUiBarWidth, kUiBarHeight, 20, 20, 20);
    screen->DrawFrame(x, y, kUiBarWidth, kUiBarHeight, 200, 180, 90);
    char label[128] = {};
    if (g_ui.awaitingRebind)
        UiToGbk_("请按新的存档键（Esc 取消）", label, sizeof(label));
    else if (g_ui.lastSavedStamp[0] && GetTickCount() < g_ui.lastSavedUntil) {
        char utf8[96] = {};
        _snprintf(utf8, sizeof(utf8), "已存档 %s", g_ui.lastSavedStamp);
        UiToGbk_(utf8, label, sizeof(label));
    }
    else if (g_ui.entries.empty())
        UiToGbk_("[战场存档] 无存档", label, sizeof(label));
    else {
        char utf8[96] = {};
        _snprintf(utf8, sizeof(utf8), "[战场存档] %u 条，点击选择", (unsigned)g_ui.entries.size());
        UiToGbk_(utf8, label, sizeof(label));
    }
    font->TextDraw(screen, label, x + 6, y, kUiBarWidth - 60, kUiBarHeight,
        eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
    char key[16] = {};
    char keyUtf8[8] = {};
    _snprintf(keyUtf8, sizeof(keyUtf8), "键:%c", g_ui.saveKey);
    UiToGbk_(keyUtf8, key, sizeof(key));
    font->TextDraw(screen, key, x + kUiBarWidth - 58, y, 52, kUiBarHeight,
        eTextColor::WHITE, eTextAlignment::MIDDLE_CENTER);
    screen->DrawFrame(x + kUiBarWidth - 58, y + 2, 54, kUiBarHeight - 4, 220, 200, 110);
    if (!g_ui.listOpen || g_ui.entries.empty()) return;
    const int rows = g_ui.entries.size() < (size_t)kUiListMaxRows
        ? (int)g_ui.entries.size() : kUiListMaxRows;
    const int listY = y + kUiBarHeight;
    screen->FillRectangle(x, listY, kUiBarWidth, rows * kUiRowHeight, 10, 10, 30);
    screen->DrawFrame(x, listY, kUiBarWidth, rows * kUiRowHeight, 160, 140, 70);
    for (int row = 0; row < rows; ++row) {
        if (row == g_ui.hoverRow)
            screen->FillRectangle(x + 2, listY + row * kUiRowHeight,
                kUiBarWidth - 4, kUiRowHeight, 90, 70, 20);
        char stamp[32] = {};
        UiFormatStamp_(g_ui.entries[row], stamp, sizeof(stamp));
        font->TextDraw(screen, stamp, x + 6, listY + row * kUiRowHeight,
            kUiBarWidth - 12, kUiRowHeight, eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
    }
}

static bool UiPointInBar_(int px, int py)
{
    return px >= g_ui.x && px < g_ui.x + kUiBarWidth && py >= g_ui.y && py < g_ui.y + kUiBarHeight;
}

static bool UiHitBar_(const H3Msg* msg)
{
    if (!msg) return false;
    const H3POINT cursor = H3POINT::GetCursorPosition();
    if (UiPointInBar_(cursor.x, cursor.y)) return true;
    if (UiPointInBar_(msg->position.x, msg->position.y)) return true;
    const H3CombatManager* mgr = H3CombatManager::Get();
    const BYTE* dlgBytes = reinterpret_cast<const BYTE*>(mgr ? mgr->dlg : nullptr);
    if (!dlgBytes) return false;
    const int dlgX = *reinterpret_cast<const INT32*>(dlgBytes + 0x18);
    const int dlgY = *reinterpret_cast<const INT32*>(dlgBytes + 0x1C);
    return UiPointInBar_(dlgX + msg->position.x, dlgY + msg->position.y);
}

static int UiHitRow_(int px, int py)
{
    if (!g_ui.listOpen) return -1;
    const int top = g_ui.y + kUiBarHeight;
    if (px < g_ui.x || px >= g_ui.x + kUiBarWidth || py < top) return -1;
    int row = (py - top) / kUiRowHeight;
    const int rows = g_ui.entries.size() < (size_t)kUiListMaxRows
        ? (int)g_ui.entries.size() : kUiListMaxRows;
    if (row >= rows) return -1;
    return row;
}

static void UiConfirmAndRestore_(const UiSaveEntry& entry)
{
    char stamp[32] = {};
    UiFormatStamp_(entry, stamp, sizeof(stamp));
    char text[160] = {};
    char textUtf8[160] = {};
    _snprintf(textUtf8, sizeof(textUtf8), "读回 %s 这一档？当前未保存的进度会丢掉。", stamp);
    UiToGbk_(textUtf8, text, sizeof(text));
    if (!H3Messagebox::Choice(text))
        return;
    H3CombatManager* mgr = H3CombatManager::Get();
    const char* reason = nullptr;
    if (!CombatCanCapture_(mgr, &reason)) {
        LogWarn("读档被拒绝：%s", reason ? reason : "unsafe");
        return;
    }
    std::string battleKey;
    std::string error;
    if (!BattleFingerprint_(mgr, &battleKey, &error)) {
        LogError("读档失败：fingerprint");
        return;
    }
    hbs::ArchiveStore store(ArchiveRoot_());
    std::vector<hbs::ArchiveRecord> records;
    std::wstring storeError;
    if (!store.List(battleKey, "", records, storeError)) {
        LogError("读档失败：archive list failed");
        return;
    }
    const hbs::ArchiveRecord* found = nullptr;
    for (size_t i = 0; i < records.size(); ++i) {
        if (records[i].path == entry.path
            && records[i].timestampUtcMs == entry.timestampUtcMs
            && records[i].sequence == entry.sequence) {
            found = &records[i];
            break;
        }
    }
    if (!found) {
        LogError("读档失败：record vanished");
        return;
    }
    hbs::ArchiveDocument document;
    CodecCapture capture;
    if (!store.Load(*found, document, storeError)
        || !CodecDecode(document.sections, &capture, &error)) {
        LogError("读档失败：%s", error.empty() ? "archive load failed" : error.c_str());
        return;
    }
    if (!RestoreSameBattle_(mgr, capture, &error)) {
        LogError("战斗恢复失败：%s", error.c_str());
        return;
    }
    UiReloadEntries_(mgr);
    g_ui.listOpen = false;
    LogInfo("战斗时刻已恢复：%s", stamp);
}

static void UiHandleMouse_(H3Msg* msg)
{
    if (!msg) return;
    const H3POINT cursor = H3POINT::GetCursorPosition();
    const int px = cursor.x;
    const int py = cursor.y;
    if (msg->command == eMsgCommand::MOUSE_OVER) {
        g_ui.hoverRow = UiHitRow_(px, py);
        return;
    }
    if (msg->command != eMsgCommand::MOUSE_BUTTON) return;
    if (msg->subtype == eMsgSubtype::LBUTTON_DOWN && UiHitBar_(msg)) {
        g_ui.dragging = true;
        g_ui.dragOffX = px - g_ui.x;
        g_ui.dragOffY = py - g_ui.y;
        g_ui.listOpen = false;
        return;
    }
    if (msg->subtype == eMsgSubtype::LBUTTON_DOWN && g_ui.dragging) return;
    if (msg->subtype == eMsgSubtype::LBUTTON_CLICK) {
        if (g_ui.dragging) {
            g_ui.dragging = false;
            H3LoadedPcx16* screen = H3WindowManager::Get()->screenPcx16;
            if (screen) {
                if (g_ui.x < 0) g_ui.x = 0;
                if (g_ui.y < 0) g_ui.y = 0;
                if (g_ui.x + kUiBarWidth > screen->width) g_ui.x = screen->width - kUiBarWidth;
                if (g_ui.y + kUiBarHeight > screen->height) g_ui.y = screen->height - kUiBarHeight;
            }
            UiSaveBarPosition_();
            return;
        }
        if (UiHitBar_(msg)) {
            if (px >= g_ui.x + kUiBarWidth - 52) {
                g_ui.awaitingRebind = true;
                return;
            }
            g_ui.listOpen = !g_ui.listOpen;
            if (g_ui.listOpen) UiReloadEntries_(H3CombatManager::Get());
            return;
        }
        const int row = UiHitRow_(px, py);
        if (row >= 0 && row < (int)g_ui.entries.size()) {
            UiConfirmAndRestore_(g_ui.entries[row]);
            return;
        }
        if (g_ui.listOpen) g_ui.listOpen = false;
        return;
    }
    if (msg->subtype == eMsgSubtype::RBUTTON_DOWN) {
        const int row = UiHitRow_(px, py);
        if (row >= 0 && row < (int)g_ui.entries.size()) {
            hbs::ArchiveStore store(ArchiveRoot_());
            hbs::ArchiveRecord record;
            record.path = g_ui.entries[row].path;
            std::wstring storeError;
            if (store.Delete(record, storeError))
                UiReloadEntries_(H3CombatManager::Get());
        }
    }
}

static void UiHandleDragMove_(H3Msg* msg)
{
    if (!g_ui.dragging || !msg) return;
    g_ui.x = msg->position.x - g_ui.dragOffX;
    g_ui.y = msg->position.y - g_ui.dragOffY;
}

static const char* const kUiFreeKeys_ = "BFGKMNUVXY";

static bool UiKeyIsFree_(char key)
{
    return key >= 'A' && key <= 'Z' && strchr(kUiFreeKeys_, key) != nullptr;
}

static void UiHandleRebindKey_(char key, bool escape)
{
    if (escape) {
        g_ui.awaitingRebind = false;
        return;
    }
    if (UiKeyIsFree_(key)) {
        g_ui.saveKey = (char)key;
        UiSaveHotkey_();
        g_ui.awaitingRebind = false;
    }
}
