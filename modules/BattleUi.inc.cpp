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

// ---------- DirectDraw backbuffer 直绘（方案移植自 H3BattleValueInfo，2026-10-05） ----------
// 21:32 实测教训：TextDraw 直接画 screenPcx16 大缓冲会与 ZCN2 中文渲染钩子互踩崩溃
// （EXCEPTION_ACCESS_VIOLATION 于 ZCN2.dll+0x1192C，栈经 UiDrawBar_→TextDraw）。
// BVI 定稿方案：所有绘制画到自建小合成图，Lock backbuffer 手工逐像素 blt，H3Redraw 刷新。

static LPDIRECTDRAWSURFACE UiDDBackBuffer_()
{
    return *reinterpret_cast<LPDIRECTDRAWSURFACE*>(0x6AAD28);
}

static int UiBackBufferBpp_(LPDIRECTDRAWSURFACE surface)
{
    if (!surface) return H3BitMode::Get() == 4 ? 32 : 16;
    DDPIXELFORMAT pf;
    memset(&pf, 0, sizeof(pf));
    pf.dwSize = sizeof(pf);
    if (SUCCEEDED(surface->GetPixelFormat(&pf))
        && (pf.dwRGBBitCount == 16 || pf.dwRGBBitCount == 32))
        return (int)pf.dwRGBBitCount;
    return H3BitMode::Get() == 4 ? 32 : 16;
}

static WORD UiRgb888To565_(int r, int g, int b)
{
    return (WORD)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

static WORD UiRgb8888To565_(DWORD c)
{
    return UiRgb888To565_((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

static H3LoadedPcx16* g_barBg = nullptr;
static bool g_barBgFailed = false;

// 加载 DLL 同目录 img\HB_bg.pcx（成品图 360x24，24 位 3 平面 PCX）：
// 仅悬浮条本体一行；金框/键位小框已离线烘焙进图，运行时整图粘贴。
// 下拉列表超出悬浮框，不使用背景图（2026-10-05 用户明确）。
static H3LoadedPcx16* UiLoadBarBg_()
{
    if (g_barBg || g_barBgFailed)
        return g_barBg;
    g_barBgFailed = true;  // 失败只试一次；成功路径最后复位

    wchar_t* wpath = new(std::nothrow) wchar_t[MAX_PATH + 32]();
    if (!wpath) return nullptr;
    GetModuleFileNameW(g_hModule, wpath, MAX_PATH + 30);
    wchar_t* slash = wcsrchr(wpath, L'\\');
    if (!slash) { delete[] wpath; return nullptr; }
    wcscpy_s(slash + 1, 24, L"img\\HB_bg.pcx");

    FILE* file = nullptr;
    if (_wfopen_s(&file, wpath, L"rb") != 0 || !file) {
        LogWarn("背景图加载失败：img\\HA_bg.pcx");
        delete[] wpath;
        return nullptr;
    }
    delete[] wpath;
    fseek(file, 0, SEEK_END);
    const long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (fileSize < 128) { fclose(file); return nullptr; }
    BYTE* encoded = (BYTE*)malloc((size_t)fileSize);
    if (!encoded || fread(encoded, 1, (size_t)fileSize, file) != (size_t)fileSize) {
        if (encoded) free(encoded);
        fclose(file);
        return nullptr;
    }
    fclose(file);

    const int bpp = encoded[3];
    const int xmin = *(WORD*)(encoded + 4);
    const int ymin = *(WORD*)(encoded + 6);
    const int xmax = *(WORD*)(encoded + 8);
    const int ymax = *(WORD*)(encoded + 10);
    const int planes = encoded[65];
    const int bpl = *(WORD*)(encoded + 66);
    const int width = xmax - xmin + 1;
    const int height = ymax - ymin + 1;
    if (encoded[0] != 0x0A || encoded[2] != 1 || bpp != 8 || planes != 3
        || width < kUiBarWidth || height < kUiBarHeight || bpl < width) {
        LogWarn("背景图格式不符：w=%d h=%d bpp=%d planes=%d", width, height, bpp, planes);
        free(encoded);
        return nullptr;
    }

    const size_t rawSize = (size_t)bpl * planes * height;
    BYTE* raw = (BYTE*)malloc(rawSize);
    if (!raw) { free(encoded); return nullptr; }
    size_t srcPos = 128;
    size_t outPos = 0;
    while (outPos < rawSize && srcPos < (size_t)fileSize) {
        const BYTE marker = encoded[srcPos++];
        if ((marker & 0xC0) == 0xC0) {
            const int count = marker & 0x3F;
            if (srcPos >= (size_t)fileSize) break;
            const BYTE value = encoded[srcPos++];
            for (int i = 0; i < count && outPos < rawSize; ++i)
                raw[outPos++] = value;
        } else {
            raw[outPos++] = marker;
        }
    }
    free(encoded);
    if (outPos != rawSize) { free(raw); return nullptr; }

    g_barBg = H3LoadedPcx16::Create(width, height);
    if (!g_barBg || !g_barBg->buffer) {
        if (g_barBg) { g_barBg->Destroy(); g_barBg = nullptr; }
        free(raw);
        return nullptr;
    }
    const bool out32 = H3BitMode::Get() == 4;
    for (int py = 0; py < height; ++py) {
        const BYTE* red = raw + (size_t)py * bpl * planes;
        const BYTE* green = red + bpl;
        const BYTE* blue = green + bpl;
        BYTE* row = g_barBg->buffer + (size_t)py * g_barBg->scanlineSize;
        if (out32) {
            DWORD* pixels = (DWORD*)row;
            for (int px = 0; px < width; ++px)
                pixels[px] = 0xFF000000u | (red[px] << 16) | (green[px] << 8) | blue[px];
        } else {
            WORD* pixels = (WORD*)row;
            for (int px = 0; px < width; ++px)
                pixels[px] = UiRgb888To565_(red[px], green[px], blue[px]);
        }
    }
    free(raw);
    g_barBgFailed = false;
    LogInfo("背景图已加载：%dx%d", width, height);
    return g_barBg;
}

// 同位深 pcx16 区域复制（src/dst 均按当前游戏位深分配，行内逐像素等宽）。
static void UiCopyBgRegion_(H3LoadedPcx16* dst, const H3LoadedPcx16* src,
    int srcX, int srcY, int w, int h)
{
    const size_t px = H3BitMode::Get() == 4 ? 4 : 2;
    for (int row = 0; row < h; ++row) {
        const BYTE* s = src->buffer + (size_t)(srcY + row) * src->scanlineSize + (size_t)srcX * px;
        BYTE* d = dst->buffer + (size_t)row * dst->scanlineSize;
        memcpy(d, s, (size_t)w * px);
    }
}

// 把 pcx16 指定区域逐像素写入 backbuffer。禁止 DD Blt（HD 下触发崩溃）。
// 残影恢复：把 screenPcx16 对应区域原样拷回 backbuffer（源就是战场干净画面）。
static bool UiBltPcx16Region_(H3LoadedPcx16* src, int srcX, int srcY,
    int copyW, int copyH, int dstX, int dstY)
{
    if (!src || !src->buffer) return false;
    LPDIRECTDRAWSURFACE bb = UiDDBackBuffer_();
    if (!bb) return false;
    __try {
        DDSURFACEDESC desc;
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        if (FAILED(bb->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, nullptr))
            || !desc.lpSurface)
            return false;
        H3WindowManager* wnd = H3WindowManager::Get();
        int dstW = (int)desc.dwWidth;
        int dstH = (int)desc.dwHeight;
        if (dstW <= 0 && wnd && wnd->screenPcx16) dstW = wnd->screenPcx16->width;
        if (dstH <= 0 && wnd && wnd->screenPcx16) dstH = wnd->screenPcx16->height;
        const int bpp = UiBackBufferBpp_(bb);
        const bool src32 = H3BitMode::Get() == 4;
        // 源区域边界裁剪
        if (srcX < 0) { copyW += srcX; dstX -= srcX; srcX = 0; }
        if (srcY < 0) { copyH += srcY; dstY -= srcY; srcY = 0; }
        if (srcX + copyW > src->width) copyW = src->width - srcX;
        if (srcY + copyH > src->height) copyH = src->height - srcY;
        int srcX0 = srcX;
        int srcY0 = srcY;
        if (dstX < 0) { srcX0 -= dstX; copyW += dstX; dstX = 0; }
        if (dstY < 0) { srcY0 -= dstY; copyH += dstY; dstY = 0; }
        if (dstX + copyW > dstW) copyW = dstW - dstX;
        if (dstY + copyH > dstH) copyH = dstH - dstY;
        if (copyW > 0 && copyH > 0) {
            for (int row = 0; row < copyH; ++row) {
                BYTE* srcRow = src->buffer + (srcY0 + row) * src->scanlineSize;
                if (bpp == 32) {
                    // backbuffer 是 BGRX：只写 B/G/R，X 字节保持原值。
                    BYTE* d = (BYTE*)desc.lpSurface + (dstY + row) * (int)desc.lPitch + dstX * 4;
                    if (src32) {
                        DWORD* s = (DWORD*)srcRow + srcX0;
                        for (int i = 0; i < copyW; ++i) {
                            const DWORD c = s[i];
                            d[i * 4 + 0] = (BYTE)c;
                            d[i * 4 + 1] = (BYTE)(c >> 8);
                            d[i * 4 + 2] = (BYTE)(c >> 16);
                        }
                    } else {
                        WORD* s = (WORD*)srcRow + srcX0;
                        for (int i = 0; i < copyW; ++i) {
                            const WORD c16 = s[i];
                            d[i * 4 + 0] = (BYTE)((c16 & 0x1F) << 3);
                            d[i * 4 + 1] = (BYTE)(((c16 >> 5) & 0x3F) << 2);
                            d[i * 4 + 2] = (BYTE)(((c16 >> 11) & 0x1F) << 3);
                        }
                    }
                } else {
                    WORD* d = (WORD*)((BYTE*)desc.lpSurface + (dstY + row) * (int)desc.lPitch) + dstX;
                    if (src32) {
                        DWORD* s = (DWORD*)srcRow + srcX0;
                        for (int i = 0; i < copyW; ++i)
                            d[i] = UiRgb8888To565_(s[i]);
                    } else {
                        WORD* s = (WORD*)srcRow + srcX0;
                        for (int i = 0; i < copyW; ++i)
                            d[i] = s[i];
                    }
                }
            }
        }
        const bool ok = copyW > 0 && copyH > 0;
        bb->Unlock(nullptr);
        return ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static H3LoadedPcx16* g_barComposite = nullptr;

// 等待静止帧存档的截止时刻（Entry 置位/消费；0=无等待）。绘制层用它显示提示。
static DWORD g_uiWaitSaveUntil = 0;

static void UiDrawBar_(H3CombatManager* mgr)
{
    (void)mgr;
    static bool broken = false;
    static bool redrawing = false;
    if (broken || redrawing) return;
    __try {
        H3WindowManager* wnd = H3WindowManager::Get();
        H3Font* font = H3SmallFont::Get();
        if (!wnd || !font || !UiDDBackBuffer_()) return;
        const int compositeH = kUiBarHeight + kUiListMaxRows * kUiRowHeight;
        if (!g_barComposite || !g_barComposite->buffer) {
            if (g_barComposite) g_barComposite->Destroy();
            g_barComposite = H3LoadedPcx16::Create(kUiBarWidth, compositeH);
            if (!g_barComposite || !g_barComposite->buffer) return;
        }
        static DWORD lastDrawInfo = 0;
        const DWORD infoNow = GetTickCount();
        const bool logInfo = infoNow - lastDrawInfo > 5000;
        if (logInfo) lastDrawInfo = infoNow;
        H3LoadedPcx16* c = g_barComposite;
        const int x = g_ui.x;
        const int y = g_ui.y;
        const int rows = (g_ui.listOpen && !g_ui.entries.empty())
            ? (g_ui.entries.size() < (size_t)kUiListMaxRows
                ? (int)g_ui.entries.size() : kUiListMaxRows)
            : 0;
        // 列表高度随实际行数自适应（2026-10-05 用户实测纠正：固定满高会显示
        // 一堆空行背景板）；成品图行分隔线在每行底部，任意行数展开底边闭合。
        const int usedH = kUiBarHeight + rows * kUiRowHeight;
        // 残影跟踪：位置/高度变化时，本帧末尾把上一帧矩形从 screenPcx16 拷回
        // backbuffer（HD 增量呈现不会自动覆盖旧区域，2026-10-05 拖动实测残影）。
        static int lastX = -1;
        static int lastY = -1;
        static int lastH = -1;
        static H3CombatManager* lastMgr = nullptr;
        if (lastMgr != mgr) {
            lastX = lastY = lastH = -1;
            lastMgr = mgr;
        }
        const bool rectChanged = lastX != x || lastY != y || lastH != usedH;
        // 每帧整图清底，防列表收起后残留旧像素
        c->FillRectangle(0, 0, kUiBarWidth, compositeH, 0, 0, 0);
        // 背景：成品图 HB_bg.pcx 只贴悬浮条本体一行（金框已烘焙）；
        // 下拉列表超出悬浮框，不用背景图（2026-10-05 用户明确），纯色+代码框。
        H3LoadedPcx16* bg = UiLoadBarBg_();
        const bool bgOk = bg && bg->buffer
            && bg->width >= kUiBarWidth && bg->height >= kUiBarHeight;
        if (bgOk) {
            UiCopyBgRegion_(c, bg, 0, 0, kUiBarWidth, kUiBarHeight);
        }
        else {
            c->FillRectangle(0, 0, kUiBarWidth, kUiBarHeight, 20, 20, 20);
            c->DrawFrame(0, 0, kUiBarWidth, kUiBarHeight, 200, 180, 90);
        }
        if (rows > 0) {
            c->FillRectangle(0, kUiBarHeight, kUiBarWidth, rows * kUiRowHeight, 10, 10, 30);
            c->DrawFrame(0, kUiBarHeight, kUiBarWidth, rows * kUiRowHeight, 160, 140, 70);
        }
        char label[128] = {};
        if (g_ui.awaitingRebind)
            UiToGbk_("请按新的存档键（Esc 取消）", label, sizeof(label));
        else if (g_uiWaitSaveUntil)
            UiToGbk_("等待动画结束…", label, sizeof(label));
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
        font->TextDraw(c, label, 6, 0, kUiBarWidth - 60, kUiBarHeight,
            eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
        char key[16] = {};
        char keyUtf8[8] = {};
        _snprintf(keyUtf8, sizeof(keyUtf8), "键:%c", g_ui.saveKey);
        UiToGbk_(keyUtf8, key, sizeof(key));
        font->TextDraw(c, key, kUiBarWidth - 58, 0, 52, kUiBarHeight,
            eTextColor::WHITE, eTextAlignment::MIDDLE_CENTER);
        c->DrawFrame(kUiBarWidth - 58, 2, 54, kUiBarHeight - 4, 220, 200, 110);
        if (rows > 0) {
            const int listY = kUiBarHeight;
            for (int row = 0; row < rows; ++row) {
                if (row == g_ui.hoverRow)
                    c->FillRectangle(2, listY + row * kUiRowHeight,
                        kUiBarWidth - 4, kUiRowHeight, 90, 70, 20);
                char stamp[32] = {};
                UiFormatStamp_(g_ui.entries[row], stamp, sizeof(stamp));
                font->TextDraw(c, stamp, 6, listY + row * kUiRowHeight,
                    kUiBarWidth - 12, kUiRowHeight, eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
            }
        }
        bool bltOk = false;
        redrawing = true;
        if (rectChanged && lastX >= 0 && wnd->screenPcx16
            && UiBltPcx16Region_(wnd->screenPcx16, lastX, lastY, kUiBarWidth, lastH, lastX, lastY)) {
            wnd->H3Redraw(lastX, lastY, kUiBarWidth, lastH);
        }
        bltOk = UiBltPcx16Region_(c, 0, 0, kUiBarWidth, usedH, x, y);
        wnd->H3Redraw(x, y, kUiBarWidth, usedH);
        redrawing = false;
        lastX = x;
        lastY = y;
        lastH = usedH;
        if (logInfo)
            LogInfo("悬浮条绘制：合成图=%p backbuffer=%p pos=(%d,%d) blt=%d",
                c, UiDDBackBuffer_(), x, y, bltOk ? 1 : 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        broken = true;
        LogError("悬浮条绘制异常(code=0x%08X)，本会话停画防崩", GetExceptionCode());
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

// 改键确认不依赖战斗消息钩子：等待期间在绘制帧轮询全部允许字母与 Esc。
static void UiPollRebindKey_()
{
    if (!g_ui.awaitingRebind) return;
    static bool prevDown[16] = {};
    const int escIndex = (int)strlen(kUiFreeKeys_);
    const bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escDown && !prevDown[escIndex]) {
        g_ui.awaitingRebind = false;
        prevDown[escIndex] = escDown;
        return;
    }
    prevDown[escIndex] = escDown;
    for (int i = 0; kUiFreeKeys_[i]; ++i) {
        const bool down = (GetAsyncKeyState((int)kUiFreeKeys_[i]) & 0x8000) != 0;
        if (down && !prevDown[i]) {
            g_ui.saveKey = kUiFreeKeys_[i];
            UiSaveHotkey_();
            g_ui.awaitingRebind = false;
            LogInfo("改键完成：%c", g_ui.saveKey);
        }
        prevDown[i] = down;
    }
}

// 系统鼠标钩子路径的统一点击入口：gameX/gameY 为游戏逻辑坐标。
static void UiHandleFrameClick_(int gameX, int gameY)
{
    if (UiPointInBar_(gameX, gameY)) {
        if (gameX >= g_ui.x + kUiBarWidth - 58) {
            g_ui.awaitingRebind = true;
            LogInfo("点击快捷键区域：(%d,%d)", gameX, gameY);
        } else {
            g_ui.listOpen = !g_ui.listOpen;
            if (g_ui.listOpen) UiReloadEntries_(H3CombatManager::Get());
            LogInfo("点击存档列表区域：(%d,%d)", gameX, gameY);
        }
        return;
    }
    const int row = UiHitRow_(gameX, gameY);
    if (row >= 0 && row < (int)g_ui.entries.size()) {
        UiConfirmAndRestore_(g_ui.entries[row]);
        return;
    }
    if (g_ui.listOpen) g_ui.listOpen = false;
    if (g_ui.awaitingRebind) g_ui.awaitingRebind = false;
}

// 系统鼠标钩子路径的右键删除：点击列表行删除对应存档。
static void UiHandleFrameRightClick_(int gameX, int gameY)
{
    const int row = UiHitRow_(gameX, gameY);
    if (row < 0 || row >= (int)g_ui.entries.size()) return;
    hbs::ArchiveStore store(ArchiveRoot_());
    hbs::ArchiveRecord record;
    record.path = g_ui.entries[row].path;
    std::wstring storeError;
    if (store.Delete(record, storeError)) {
        UiReloadEntries_(H3CombatManager::Get());
        LogInfo("已删除存档行：%d", row);
    } else {
        LogError("删除存档失败：行 %d", row);
    }
}
