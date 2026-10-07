// ========== BattleUi.inc.cpp ==========
// 战场悬浮条：常驻存档列表、快捷键显示、读档确认与右键删除。
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
static const int kUiBarWidth = 480;
// 常驻列表宽度：能显示完时间即可，不跟悬浮条同宽。
// 内容只有 "yyyymmdd-hhmmss" 15 字符，小字体约 6px/字符，136px 含边距足够。
static const int kUiListWidth = 136;
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

    bool dragging = false;
    int dragOffX = 0;
    int dragOffY = 0;
    int dragDownX = 0;
    int dragDownY = 0;
    char saveKey = 'G';
    bool awaitingRebind = false;
    char rebindKey = 0;          // 改键接受的键：必须先松开才允许触发存档
    char lastSavedStamp[32] = {};
    DWORD lastSavedUntil = 0;
    DWORD lastNoticeUntil = 0;    // 等待超时等结果提示，悬浮条短暂显示
    char lastNoticeGbk[96] = {};
    bool lastNoticeHighlight = false;  // 本条通知用醒目色（读档成功）；过期后自然回常规状态行
    DWORD rebindGuardUntil = 0;  // 改键生效后短窗内忽略该键，防误触发存档
    std::string battleKey;
    std::vector<UiSaveEntry> entries;
    int hoverRow = -1;
    int listR = 40, listG = 30, listB = 20;   // 下拉底色（背景图主色）
} g_ui;

static const int GUARD_DRAW = GuardRegisterHook_("BattleStore.Draw");
static const int GUARD_COPY = GuardRegisterHook_("BattleStore.CopyPixels");

// 返回 true = 指纹与扫描都成功（entries 可信）；false = 本次加载失败（调用方可重试）。
static bool UiReloadEntries_(const H3CombatManager* mgr)
{
    g_ui.entries.clear();
    std::string battleKey;
    std::string error;
    if (!BattleFingerprint_(mgr, &battleKey, &error)) { LogWarn("[List op=%ld] fingerprint failed: %s", g_diag.id, error.c_str()); return false; }
    g_ui.battleKey = battleKey;
    hbs::ArchiveStore store(ArchiveRoot_());
    std::vector<hbs::ArchiveRecord> records;
    std::wstring storeError;
    if (!store.List(battleKey, "", records, storeError)) { LogWarn("[List op=%ld] scan failed: %s", g_diag.id, DiagUtf8_(storeError).c_str()); return false; }
    if (!storeError.empty()) LogWarn("[List op=%ld] scan warning: %s", g_diag.id, DiagUtf8_(storeError).c_str());
    LogInfo("[List op=%ld] battle=%s records=%u", g_diag.id, battleKey.c_str(), (unsigned)records.size());
    g_ui.entries.reserve(records.size());
    for (size_t i = 0; i < records.size(); ++i) {
        UiSaveEntry entry;
        entry.timestampUtcMs = records[i].timestampUtcMs;
        entry.sequence = records[i].sequence;
        entry.path = records[i].path;
        g_ui.entries.push_back(entry);
    }
    return true;
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

// 2026-10-06 用户裁定：保存窗口=轮到该玩家且尚未下令；窗口外按键立即提示拒绝。
// 不等待动画、不加输入锁——提示只用于告知拒绝原因，短暂显示后恢复常规状态。
static void UiMarkNotice_(const char* utf8Text)
{
    if (!utf8Text || !utf8Text[0]) return;
    UiToGbk_(utf8Text, g_ui.lastNoticeGbk, sizeof(g_ui.lastNoticeGbk));
    g_ui.lastNoticeUntil = GetTickCount() + 4000;
    g_ui.lastNoticeHighlight = false;
}

// 读档成功等强结果：同一悬浮通道，但整段用醒目色显示，倒计时结束自动回常规状态行。
static void UiMarkNoticeHighlight_(const char* utf8Text)
{
    UiMarkNotice_(utf8Text);
    g_ui.lastNoticeHighlight = true;
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

// 加载 DLL 同目录 img\HB_bg.pcx（成品图 480x24，24 位 3 平面 PCX）：
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
        if (FAILED(bb->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, nullptr))) return false;
        __try {
        if (!desc.lpSurface) return false;
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
        return copyW > 0 && copyH > 0;
        } __finally {
            bb->Unlock(nullptr);
        }
    } __except (GuardCrashFilter_(GUARD_COPY, GetExceptionInformation())) {
        return false;
    }
}

static H3LoadedPcx16* g_barComposite = nullptr;

// 等待静止帧存档的截止时刻（Entry 置位/消费；0=无等待）。绘制层用它显示提示。
static DWORD g_uiWaitSaveUntil = 0;

static void CancelSaveWait_(const char* reason)
{
    if (!g_uiWaitSaveUntil) return;
    g_uiWaitSaveUntil = 0;
    LogInfo("[Wait op=%ld] cancelled input_lock=0 reason=%s", g_diag.id, reason);
    DiagEnd_("cancelled", reason);
}


static void UiDrawBar_(H3CombatManager* mgr)
{
    (void)mgr;
    static bool redrawing = false;
    if (redrawing) return;
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
        // 固定左上角（2026-10-05 用户定稿：不可拖动，每帧同位置重画）。
        g_ui.x = 8;
        g_ui.y = 8;
        const int x = g_ui.x;
        const int y = g_ui.y;
        const int rows = (!g_ui.entries.empty())
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
        // 每帧清底；列表区只清列表宽度（列表窄于悬浮条）
        c->FillRectangle(0, 0, kUiBarWidth, kUiBarHeight, 0, 0, 0);
        c->FillRectangle(0, kUiBarHeight, kUiListWidth, kUiListMaxRows * kUiRowHeight, 0, 0, 0);
        // 背景：成品图 HB_bg.pcx 只贴悬浮条本体一行（金框已烘焙）；
        // 下拉列表超出悬浮框，不用背景图（2026-10-05 用户明确），纯色+代码框。
        H3LoadedPcx16* bg = UiLoadBarBg_();
        const bool bgOk = bg && bg->buffer
            && bg->width >= kUiBarWidth && bg->height >= kUiBarHeight;
        if (bgOk) {
            UiCopyBgRegion_(c, bg, 0, 0, kUiBarWidth, kUiBarHeight);
            // 下拉列表底色 = 背景图主色（2026-10-06 用户裁定：选项底色接近背景图，
            // 不要黑蓝对比）。稀疏采样均值做主色；480x24 图 /3 /8 仅数百点，每帧可算。
            long sumR = 0, sumG = 0, sumB = 0;
            long count = 0;
            const bool src32 = H3BitMode::Get() == 4;
            for (int py = 0; py < bg->height; py += 3) {
                for (int px = 0; px < bg->width; px += 8) {
                    BYTE* pix = bg->buffer + (size_t)py * bg->scanlineSize + (size_t)px * (src32 ? 4 : 2);
                    const int r = src32 ? (int)(pix[2]) : (int)(((*(WORD*)pix >> 11) & 0x1F) << 3);
                    const int g = src32 ? (int)(pix[1]) : (int)(((*(WORD*)pix >> 5) & 0x3F) << 2);
                    const int b = src32 ? (int)(pix[0]) : (int)((*(WORD*)pix & 0x1F) << 3);
                    sumR += r; sumG += g; sumB += b; ++count;
                }
            }
            if (count > 0) {
                g_ui.listR = (int)(sumR / count);
                g_ui.listG = (int)(sumG / count);
                g_ui.listB = (int)(sumB / count);
            }
        }
        else {
            c->FillRectangle(0, 0, kUiBarWidth, kUiBarHeight, 20, 20, 20);
            c->DrawFrame(0, 0, kUiBarWidth, kUiBarHeight, 200, 180, 90);
            // 无背景图时退回悬浮条暗色系
            g_ui.listR = 20; g_ui.listG = 20; g_ui.listB = 20;
        }
        if (rows > 0) {
            c->FillRectangle(0, kUiBarHeight, kUiListWidth, rows * kUiRowHeight,
                (BYTE)g_ui.listR, (BYTE)g_ui.listG, (BYTE)g_ui.listB);
            c->DrawFrame(0, kUiBarHeight, kUiListWidth, rows * kUiRowHeight, 160, 140, 70);
        }
        char label[128] = {};
        eTextColor labelColor = eTextColor::WHITE;
        if (g_ui.awaitingRebind) {
            char utf8[96] = {};
            _snprintf(utf8, sizeof(utf8), "可用：B F G K M N U V X Y，Esc 取消");
            UiToGbk_(utf8, label, sizeof(label));
        }
        else if (g_ui.lastNoticeGbk[0] && GetTickCount() < g_ui.lastNoticeUntil) {
            _snprintf(label, sizeof(label), "%s", g_ui.lastNoticeGbk);
            // 醒目色（亮绿）：深棕底上与常规白字区分开，倒计时结束回常规状态行。
            if (g_ui.lastNoticeHighlight) labelColor = eTextColor::LIGHT_GREEN;
        }
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
            labelColor, eTextAlignment::MIDDLE_LEFT);
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
                        kUiListWidth - 4, kUiRowHeight, 90, 70, 20);
                char stamp[32] = {};
                UiFormatStamp_(g_ui.entries[row], stamp, sizeof(stamp));
                font->TextDraw(c, stamp, 6, listY + row * kUiRowHeight,
                    kUiListWidth - 12, kUiRowHeight, eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
            }
        }
        bool bltOk = false;
        redrawing = true;
        if (rectChanged && lastX >= 0 && lastH > usedH && wnd->screenPcx16) {
            const int tailY = lastY + usedH;
            if (UiBltPcx16Region_(wnd->screenPcx16, lastX, tailY, kUiListWidth,
                lastH - usedH, lastX, tailY)) wnd->H3Redraw(lastX, tailY, kUiListWidth, lastH - usedH);
        }
        // 悬浮条(480 宽)与下拉列表(136 宽)分开 blt：列表右侧不清底、不覆盖战场
        bltOk = UiBltPcx16Region_(c, 0, 0, kUiBarWidth, kUiBarHeight, x, y);
        if (rows > 0)
            bltOk = UiBltPcx16Region_(c, 0, kUiBarHeight, kUiListWidth, rows * kUiRowHeight,
                x, y + kUiBarHeight) && bltOk;
        wnd->H3Redraw(x, y, kUiBarWidth, kUiBarHeight);
        if (rows > 0) wnd->H3Redraw(x, y + kUiBarHeight, kUiListWidth, rows * kUiRowHeight);
        redrawing = false;
        lastX = x;
        lastY = y;
        lastH = usedH;
        if (logInfo)
            LogDebug("悬浮条绘制：合成图=%p backbuffer=%p pos=(%d,%d) blt=%d",
                c, UiDDBackBuffer_(), x, y, bltOk ? 1 : 0);
    } __except (GuardCrashFilter_(GUARD_DRAW, GetExceptionInformation())) {
        redrawing = false;
    }
}

// 悬浮条位置夹在战场对话框矩形内（2026-10-05 用户实测：战场框外的呈现/
// 坐标是另一套方法，screenPcx16 恢复源不可靠，拖出去会产生残影）。
static void UiClampBarToBattleDlg_(const H3CombatManager* mgr)
{
    const H3CombatDlg* dlg = mgr ? mgr->dlg : nullptr;
    if (!dlg) return;
    const int dx = dlg->GetX();
    const int dy = dlg->GetY();
    const int dw = dlg->GetWidth();
    const int dh = dlg->GetHeight();
    if (dw < kUiBarWidth || dh < kUiBarHeight) return;
    if (g_ui.x < dx) g_ui.x = dx;
    if (g_ui.y < dy) g_ui.y = dy;
    if (g_ui.x + kUiBarWidth > dx + dw)
        g_ui.x = dx + dw - kUiBarWidth;
    if (g_ui.y + kUiBarHeight > dy + dh)
        g_ui.y = dy + dh - kUiBarHeight;
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
    const int top = g_ui.y + kUiBarHeight;
    if (px < g_ui.x || px >= g_ui.x + kUiListWidth || py < top) return -1;
    int row = (py - top) / kUiRowHeight;
    const int rows = g_ui.entries.size() < (size_t)kUiListMaxRows
        ? (int)g_ui.entries.size() : kUiListMaxRows;
    if (row >= rows) return -1;
    return row;
}

static struct {
    bool pending = false;
    unsigned generation = 0;
    DWORD requested = 0;
    std::string battleKey;
    UiSaveEntry entry;
} g_restoreRequest;

static void UiConfirmAndRestore_(const UiSaveEntry& entry)
{
    if (g_restoreBusy || g_restoreFatal || g_restoreRequest.pending || !g_battleInitialized) return;
    // Copy before any dialog or redraw can invalidate the entries vector.
    g_restoreRequest.entry = entry;
    g_restoreRequest.battleKey = g_ui.battleKey;
    g_restoreRequest.generation = g_battleGeneration;
    g_restoreRequest.requested = GetTickCount();
    g_restoreRequest.pending = true;
    ClearBattleInputs_();
    LogInfo("[Load] queued generation=%u path=%s", g_battleGeneration, DiagUtf8_(entry.path).c_str());
}

// Player text never includes raw diagnostics, even for a future unknown error.
static std::string UiRestoreReasonZh_(const std::string& reason)
{
    static const struct { const char* raw; const char* zh; } reasons[] = {
        {"battle generation changed", "确认期间战斗已切换，请重新选择存档。"},
        {"restore window changed", "当前不在玩家等待下令的时刻，请回到战场后重试。"},
        {"battle fingerprint changed", "当前战斗与所选存档不一致，请重新选择存档。"},
        {"battle fingerprint failed", "无法识别当前战斗，请重新进入战斗。"},
        {"未取得本场战斗的战前指纹，请重新进入战斗", "未取得本场战斗的战前指纹，请重新进入战斗。"},
        {"archive battle key mismatch", "存档所属战斗与当前战斗不一致，不能读取。"},
        {"archive not found", "所选存档已不存在，可能已被删除或移动。"},
        {"unsupported capture version; create a new v4 save", "存档快照版本不兼容，请使用当前版本重新存档。"},
        {"unsupported capture version", "存档快照版本不兼容，请使用当前版本重新存档。"},
        {"capture version", "存档快照版本不兼容，请使用当前版本重新存档。"},
        {"saved state is not a player waiting turn", "该存档不是玩家等待下令时的状态，不能恢复。"},
        {"live active stack uses reserved slot", "存档快照版本不兼容，请使用当前版本重新存档。"},
        {"battle participant count exceeds supported slots", "参战部队数量超出当前支持范围。"},
        {"siege restoration is not supported yet", "暂不支持读取攻城战存档。"},
        {"reserved slot scalar outside valid range", "存档中的保留位数值超出有效范围。"},
        {"reserved slot AI target invalid", "存档中的保留位行动目标无效。"},
        {"arrow tower restoration is not supported yet", "暂不支持恢复箭塔状态。"},
        {"saved stack slot reference invalid", "存档中的部队位置引用无效。"},
        {"saved stack scalar outside valid range", "存档中的部队数值超出有效范围。"},
        {"non-finite spell effect", "存档中的法术效果数值无效。"},
        {"saved AI target invalid", "存档中的电脑行动目标无效。"},
        {"saved spell deque too large", "存档中的部队法术记录过多。"},
        {"saved spell id invalid", "存档中存在无效的法术编号。"},
        {"saved relation vector too large", "存档中的部队关联记录过多。"},
        {"saved relation target invalid", "存档中的部队关联目标无效。"},
        {"saved active stack is not alive", "存档中的行动部队已无存活单位。"},
        {"saved obstacle count exceeds 4096", "存档中的障碍物数量超出支持上限。"},
        {"saved obstacle kind outside known table", "存档中存在无法识别的障碍物类型。"},
        {"saved obstacle geometry invalid", "存档中的障碍物位置或占格数据无效。"},
        {"saved obstacle owner side invalid", "存档中的障碍物所属阵营无效。"},
        {"saved obstacle def name invalid", "存档中的障碍物图像资源名称无效。"},
        {"saved obstacle cell off board", "存档中的障碍物占格超出战场。"},
        {"saved obstacles overlap on one hex", "存档中的障碍物占格重叠，无法安全恢复。"},
        {"battlefield references reserved slot", "战场格子引用了不支持的部队保留位置。"},
        {"live corpse count invalid", "当前战场的尸体数量数据无效。"},
        {"live corpse references reserved slot", "当前战场尸体引用了不支持的部队保留位置。"},
        {"corpse count invalid", "存档中的尸体数量数据无效。"},
        {"square references absent stack", "存档中的战场格子引用了不存在的部队。"},
        {"square position or second hex inconsistent", "存档中的部队占格与位置数据不一致。"},
        {"corpse identity invalid", "存档中的尸体所属部队无效。"},
        {"stack and battlefield position disagree", "存档中的部队位置与战场格子不一致。"},
        {"double-wide second hex missing", "存档中双格部队的第二个占格缺失。"},
        {"battle section overflow", "战斗数据段超出存档容量限制。"},
        {"capture exceeds an exact-save limit", "战斗快照超出精确存档的支持范围。"},
        {"null capture output", "无法创建战斗快照，请重新尝试。"},
        {"capture is missing a required section", "存档缺少必要的战斗数据段。"},
        {"battle wall arrays are truncated", "存档中的城墙数据不完整。"},
        {"battle section is corrupt", "存档中的战斗数据段已损坏。"},
        {"stack section version mismatch", "存档中的部队数据段版本不兼容。"},
        {"stack section is corrupt", "存档中的部队数据段已损坏。"},
        {"square section header mismatch", "存档中的战场格子数据头不匹配。"},
        {"square section is corrupt", "存档中的战场格子数据段已损坏。"},
        {"obstacle section version mismatch", "存档中的障碍物数据段版本不兼容。"},
        {"obstacle count exceeds 4096", "存档中的障碍物数量超出支持上限。"},
        {"obstacle section is corrupt", "存档中的障碍物数据段已损坏。"},
        {"log section version mismatch", "存档中的战斗日志数据段版本不兼容。"},
        {"log count exceeds 100000", "存档中的战斗日志条数超出支持上限。"},
        {"log section is corrupt", "存档中的战斗日志数据段已损坏。"},
        {"hero section version mismatch", "存档中的英雄数据段版本不兼容。"},
        {"hero section is corrupt or mana differs from battle section", "存档中的英雄数据损坏，或魔法值与战斗数据不一致。"},
        {"relation section version mismatch", "存档中的部队关联数据段版本不兼容。"},
        {"relation section is corrupt", "存档中的部队关联数据段已损坏。"},
        {"spell section version mismatch", "存档中的法术数据段版本不兼容。"},
        {"spell section is corrupt", "存档中的法术数据段已损坏。"},
        {"combat manager is not readable", "无法读取当前战斗状态，请回到战场后重试。"},
        {"eagle-eye set is invalid", "当前战斗的鹰眼术记录无效。"},
        {"square corpse count outside 0..14", "当前战场格子的尸体数量超出有效范围。"},
        {"obstacle container is not readable", "无法读取当前战斗的障碍物记录。"},
        {"obstacle info pointer is not a recognized kind", "当前战斗存在无法识别的障碍物。"},
        {"obstacle blocked count outside 0..8", "当前障碍物的占格数量超出有效范围。"},
        {"obstacle def name is not readable", "无法读取当前障碍物的图像资源名称。"},
        {"obstacle anchor hex off board", "当前障碍物的起始格超出战场。"},
        {"obstacle cell off board", "当前障碍物的占格超出战场。"},
        {"obstacle anchor square linkage is broken", "当前障碍物与起始格的关联已失效。"},
        {"obstacle cell square linkage is broken", "当前障碍物与战场占格的关联已失效。"},
        {"combat log container is not readable", "无法读取当前战斗的日志记录。"},
        {"combat log line is not readable", "当前战斗日志中存在无法读取的记录。"},
        {"AI目标指针不属于本战场", "当前电脑行动目标不属于本战场。"},
        {"stack relation points outside the combat manager", "当前部队的关联目标不属于本战场。"},
        {"spell deque is not readable", "无法读取当前部队的法术记录。"},
        {"active stack index", "存档中的行动部队编号无效。"},
        {"stack identity", "存档中的部队类型或位置无效。"},
        {"relation target", "存档中的部队关联目标无效。"},
        {"empty capture", "存档中没有可恢复的部队。"},
        {"obstacle vector is not accessible", "当前障碍物记录不可访问，无法安全恢复。"},
        {"live obstacle resource is unavailable", "当前障碍物的图像资源不可用。"},
        {"live obstacle release slot is unavailable", "当前障碍物的资源释放接口不可用。"},
        {"saved obstacle payload is invalid", "存档中的障碍物数据无效。"},
        {"saved obstacle kind is not present in this game build", "存档中的障碍物类型与当前游戏版本不兼容。"},
        {"saved obstacle def name does not match the live table", "存档中的障碍物图像资源与当前游戏不一致。"},
        {"saved obstacle cell count does not match the live table", "存档中的障碍物占格数量与当前游戏不一致。"},
        {"saved obstacle cell layout does not match the live table", "存档中的障碍物占格布局与当前游戏不一致。"},
        {"obstacle vector would exceed the supported size", "恢复后的障碍物记录数量将超出支持上限。"},
        {"resource donor failed validation", "用于恢复的障碍物图像资源未通过校验。"},
        {"obstacle def could not be loaded", "无法加载存档所需的障碍物图像资源。"},
        {"obstacle def failed validation", "存档所需的障碍物图像资源未通过校验。"},
        {"live obstacle kind degraded since capture", "当前障碍物类型已发生变化，请重新尝试。"},
        {"not at outer player message boundary", "当前不在玩家等待下令的安全时刻，请稍后重试。"},
        {"hero mana not writable", "当前英雄魔法值不可写入，无法安全恢复。"},
        {"battle memory not writable", "当前战斗状态不可写入，无法安全恢复。"},
        {"log dialog not writable", "当前战斗日志窗口不可写入，无法安全恢复。"},
        {"log preallocation failed", "为战斗日志分配内存失败，请稍后重试。"},
        {"object preallocation failed", "为战斗对象分配内存失败，请稍后重试。"},
        {"prepared eagle-eye set mismatch", "准备恢复的鹰眼术记录未通过一致性校验。"},
        {"saved creature DEF frame unavailable", "存档所需的部队动画帧不可用。"},
        {"rollback creature DEF frame unavailable", "当前部队的回滚动画帧不可用，无法安全恢复。"},
        {"obstacle rebuild failed", "障碍物重建失败，未完成读档。"},
        {"restore mismatch; rolled back", "恢复结果与存档不一致，已回滚到读档前状态。"},
        {"resource pool release faulted", "释放障碍物资源时发生异常，已停止战斗。"},
        {"resource pool pin faulted", "保留障碍物资源时发生异常，已停止战斗。"},
        {"resource pool load faulted", "加载障碍物资源时发生异常，已停止战斗。"},
        {"obstacle scratch capacity was not prepared", "障碍物恢复工作区未准备完成，已停止战斗。"},
        {"prepared obstacle resource is missing", "已准备的障碍物资源丢失，已停止战斗。"},
        {"obstacle zombie cleanup faulted", "清理已失效的障碍物时发生异常，已停止战斗。"},
        {"obstacle removal faulted", "移除障碍物时发生异常，已停止战斗。"},
        {"obstacle entry reference faulted", "引用障碍物资源时发生异常，已停止战斗。"},
        {"obstacle vector insert faulted", "添加障碍物时发生异常，已停止战斗。"},
        {"obstacle square placement faulted", "恢复障碍物占格时发生异常，已停止战斗。"},
        {"rebuilt obstacle set lost saved identity", "重建后的障碍物与存档不一致，已停止战斗。"},
        {"obstacle rebuild fault; stopping with partial write", "障碍物重建发生异常，状态可能仅部分恢复，已停止战斗。"},
        {"rollback fault; stopping with unverified state", "回滚发生异常，战斗状态无法确认，已停止战斗。"},
        {"rollback verification failed", "回滚状态未通过校验，已停止战斗。"},
        {"native object allocator fault", "分配战斗对象时发生异常，已停止战斗。"},
        {"deque allocation cleanup fault", "清理法术记录内存时发生异常，已停止战斗。"},
        {"deque map cleanup fault", "清理法术记录索引时发生异常，已停止战斗。"},
        {"eagle-eye set constructor fault", "创建鹰眼术记录时发生异常，已停止战斗。"},
        {"eagle-eye set insertion fault", "恢复鹰眼术记录时发生异常，已停止战斗。"},
        {"native stack preparation fault", "准备部队对象时发生异常，已停止战斗。"},
        {"native stack release fault", "释放部队对象时发生异常，已停止战斗。"},
        {"eagle-eye set release fault", "释放鹰眼术记录时发生异常，已停止战斗。"},
        {"恢复随机数状态时发生异常，已停止战斗", "恢复随机数状态时发生异常，已停止战斗。"},
        {"恢复后刷新战场发生异常，已停止战斗", "恢复后刷新战场发生异常，已停止战斗。"},
        {"回滚后刷新战场发生异常，已停止战斗", "回滚后刷新战场发生异常，已停止战斗。"},
        {"提交随机数状态时发生异常，已停止战斗", "提交随机数状态时发生异常，已停止战斗。"},
        {"archive CRC mismatch", "存档文件校验失败，文件已损坏或被修改。"},
        {"section CRC mismatch", "存档数据段校验失败，文件已损坏或被修改。"},
        {"unsupported archive version", "存档文件版本不兼容，请重新存档。"},
        {"bad archive magic", "所选文件不是有效的战场存档。"},
        {"archive shorter than minimum frame", "存档文件不完整，无法读取。"},
        {"archive exceeds 64MB", "存档文件大小超出支持上限。"},
        {"filename does not match archive identity", "存档文件名与内容不一致，无法安全读取。"},
        {"record metadata does not match archive", "存档内容已改变，请刷新列表后重新选择。"},
        {"archive root is empty", "存档目录未设置，请检查配置。"},
        {"archive root is not a directory", "存档目录路径不是文件夹，请检查配置。"},
        {"archive path has no directory", "存档文件路径缺少目录信息。"},
        {"archive path is outside the store root", "存档文件路径不在允许的存档目录内。"},
        {"refusing archive path outside the store root", "存档文件路径不在允许的存档目录内。"},
        {"GetFinalPathNameByHandleW failed", "无法确认存档文件的实际路径。"},
        {"GetFinalPathNameByHandleW truncated", "存档文件的实际路径不完整。"},
        {"cannot create archive root", "无法创建存档目录，请检查目录路径和访问权限。"},
        {"cannot scan archive root", "无法扫描存档目录，请检查目录访问权限。"},
        {"archive scan ended early", "扫描存档目录时中断，请重新尝试。"},
        {"CreateFileW failed", "无法打开存档，请检查文件访问权限或是否被占用。"},
        {"cannot open archive", "无法打开存档，请检查文件访问权限或是否被占用。"},
        {"cannot read archive size", "无法读取存档文件大小。"},
        {"ReadFile failed", "读取存档文件失败，请检查磁盘或文件占用情况。"},
        {"short archive read", "存档文件未完整读出，可能已被截断。"},
        {"utf8 conversion failed", "无法解析读档失败原因，请查看插件日志。"}
    };
    for (size_t i = 0; i < sizeof(reasons) / sizeof(reasons[0]); ++i)
        if (reason == reasons[i].raw) return reasons[i].zh;

    if (std::any_of(reason.begin(), reason.end(), [](unsigned char c) { return c >= 0x80; })) return reason;
    std::string lower = reason;
    for (size_t i = 0; i < lower.size(); ++i)
        if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] += 'a' - 'A';
    auto has = [&](const char* token) { return lower.find(token) != std::string::npos; };
    if (has("crc") || has("checksum")) return "存档校验失败，文件已损坏或被修改。";
    if (has("access denied") || has("permission")) return "没有访问存档文件或目录的权限，请检查访问权限。";
    if (has("path not found") || has("invalid path") || has("invalid name") || has("directory"))
        return "存档目录或文件路径无效，请检查路径配置。";
    if (has("file not found") || has("not exist") || has("vanished")) return "所选存档已不存在，可能已被删除或移动。";
    if (has("sharing violation") || has("lock violation")) return "存档文件正被其他程序占用，请稍后重试。";
    if (has("outside") || has("path") || has("filename")) return "存档路径或文件名不符合要求，无法安全读取。";
    if (has("version")) return "存档版本与当前插件不兼容，请重新存档。";
    if (has("fingerprint") || has("key") || has("metadata")) return "存档标识与当前战斗或列表记录不一致。";
    if (has("truncated") || has("corrupt") || has("header") || has("section") || has("trailing bytes"))
        return "存档数据不完整或已损坏，无法读取。";
    if (has("allocation") || has("preallocation") || has("overflow") || has("too large") || has("exceeds"))
        return "读档所需内存或数据规模超出支持范围。";
    if (has("rollback")) return "战斗恢复或回滚未通过校验，请查看插件日志。";
    if (has("fault") || has("exception")) return "恢复战斗时发生内部异常，请查看插件日志。";
    if (has("obstacle") || has("resource") || has("def")) return "障碍物或图像资源不满足恢复条件。";
    if (has("stack") || has("corpse") || has("square") || has("relation")) return "部队或战场格子数据不满足恢复条件。";
    if (has("spell") || has("eagle-eye") || has("mana")) return "法术或英雄数据不满足恢复条件。";
    if (has("read") || has("open") || has("scan")) return "无法读取存档或当前战斗数据，请稍后重试。";
    return "无法完成读档，具体原因请查看插件日志。";
}

// Archive errors omit Win32 codes; only attach a code to generic I/O failures.
static std::string UiArchiveRestoreReason_(const std::wstring& error, DWORD code)
{
    std::string raw = DiagUtf8_(error);
    if (raw != "CreateFileW failed" && raw != "cannot open archive" && raw != "cannot scan archive root"
        && raw != "cannot create archive root" && raw != "ReadFile failed") return raw;
    switch (code) {
    case ERROR_ACCESS_DENIED: raw += "; access denied"; break;
    case ERROR_FILE_NOT_FOUND: raw += "; file not found"; break;
    case ERROR_PATH_NOT_FOUND: raw += "; path not found"; break;
    case ERROR_INVALID_NAME: raw += "; invalid name"; break;
    case ERROR_BAD_PATHNAME:
    case ERROR_FILENAME_EXCED_RANGE:
    case ERROR_INVALID_DRIVE:
    case ERROR_DIRECTORY: raw += "; invalid path"; break;
    case ERROR_SHARING_VIOLATION: raw += "; sharing violation"; break;
    case ERROR_LOCK_VIOLATION: raw += "; lock violation"; break;
    default: break;
    }
    LogWarn("[Load op=%ld] archive raw=%s win32=%lu", g_diag.id, DiagUtf8_(error).c_str(), code);
    return raw;
}

static void UiRestoreFailure_(const char* outcome, const std::string& raw)
{
    const std::string reason = UiRestoreReasonZh_(raw);
    LogWarn("[Load op=%ld] raw=%s reason_zh=%s fatal=%d", g_diag.id, raw.c_str(), reason.c_str(), g_restoreFatal ? 1 : 0);
    // Keep the fatal diagnostic write context; never show a dialog in that state.
    if (g_restoreFatal) return;
    DiagEnd_(outcome, reason.c_str());
    const std::string message = std::string("读档未完成：") + reason;
    char notice[512] = {};
    UiToGbk_(message.c_str(), notice, sizeof(notice));
    H3Messagebox::Show(notice);
}

static void UiExecuteRestore_(H3CombatManager* mgr)
{
    if (g_restoreFatal) { g_restoreRequest.pending = false; return; }
    const UiSaveEntry entry = g_restoreRequest.entry;
    const unsigned generation = g_restoreRequest.generation;
    const std::string expectedKey = g_restoreRequest.battleKey;
    g_restoreRequest.pending = false;
    g_restoreBusy = true;
    struct BusyReset { ~BusyReset() { ClearBattleInputs_(); g_restoreBusy = false; } } busyReset;
    DiagBegin_("load", "outer-message", mgr);
    char stamp[32] = {};
    UiFormatStamp_(entry, stamp, sizeof(stamp));
    char text[192] = {}, utf8[192] = {};
    _snprintf(utf8, sizeof(utf8), "读回 %s 这一档？当前未保存的进度会丢掉。", stamp);
    UiToGbk_(utf8, text, sizeof(text));
    DiagStage_("load.confirm");
    const bool confirmed = H3Messagebox::Choice(text);
    if (g_restoreFatal) return;
    if (!confirmed) { DiagEnd_("cancelled", "玩家取消读档确认"); return; }
    std::string key, error;
    if (generation != g_battleGeneration) {
        UiRestoreFailure_("rejected", "battle generation changed"); return;
    }
    if (!RestoreWindow_(mgr)) {
        UiRestoreFailure_("rejected", "restore window changed"); return;
    }
    if (!BattleFingerprint_(mgr, &key, &error)) {
        UiRestoreFailure_("rejected", error.empty() ? "battle fingerprint failed" : error); return;
    }
    if (key != expectedKey) {
        LogWarn("[Load op=%ld] expected_battle=%s current_battle=%s", g_diag.id, expectedKey.c_str(), key.c_str());
        UiRestoreFailure_("rejected", "battle fingerprint changed"); return;
    }
    hbs::ArchiveStore store(ArchiveRoot_());
    std::vector<hbs::ArchiveRecord> records;
    std::wstring storeError;
    SetLastError(ERROR_SUCCESS);
    if (!store.List(key, "", records, storeError)) {
        const DWORD code = GetLastError();
        UiRestoreFailure_("failed", UiArchiveRestoreReason_(storeError, code)); return;
    }
    if (!storeError.empty())
        LogWarn("[Load op=%ld] scan warning raw=%s reason_zh=%s", g_diag.id,
            DiagUtf8_(storeError).c_str(), UiRestoreReasonZh_(DiagUtf8_(storeError)).c_str());
    const hbs::ArchiveRecord* found = nullptr;
    for (size_t i = 0; i < records.size(); ++i)
        if (records[i].path == entry.path && records[i].timestampUtcMs == entry.timestampUtcMs
            && records[i].sequence == entry.sequence) { found = &records[i]; break; }
    if (!found) {
        // Scan skips corrupt files; preserve the selected file's reason, not another file's warning.
        const std::wstring marker = hbs::detail::FileNameOf(entry.path) + L": ";
        const size_t warning = storeError.find(marker);
        if (warning != std::wstring::npos) {
            const size_t start = warning + marker.size();
            const size_t end = storeError.find(L"; ", start);
            const std::wstring selectedError = storeError.substr(start, end == std::wstring::npos ? end : end - start);
            // Reopen only for generic I/O diagnostics, since Scan lost the original OS code.
            DWORD code = ERROR_SUCCESS;
            if (selectedError == L"CreateFileW failed" || selectedError == L"cannot open archive") {
                HANDLE file = CreateFileW(entry.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE) code = GetLastError();
                else CloseHandle(file);
            }
            UiRestoreFailure_("failed", UiArchiveRestoreReason_(selectedError, code)); return;
        }
        const DWORD attributes = GetFileAttributesW(entry.path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD code = GetLastError();
            UiRestoreFailure_("failed", UiArchiveRestoreReason_(L"cannot open archive", code)); return;
        }
        UiRestoreFailure_("rejected", "record metadata does not match archive"); return;
    }
    hbs::ArchiveDocument document;
    storeError.clear();
    SetLastError(ERROR_SUCCESS);
    if (!store.Load(*found, document, storeError)) {
        const DWORD code = GetLastError();
        UiRestoreFailure_("failed", UiArchiveRestoreReason_(storeError, code)); return;
    }
    if (document.battleKey != key) {
        LogWarn("[Load op=%ld] archive_battle=%s current_battle=%s", g_diag.id, document.battleKey.c_str(), key.c_str());
        UiRestoreFailure_("rejected", "archive battle key mismatch"); return;
    }
    std::unique_ptr<CodecCapture> captureStorage(new CodecCapture{});
    CodecCapture& capture = *captureStorage;
    if (!CodecDecode(document.sections, &capture, &error) || !RestoreSameBattle_(mgr, capture, key, &error)) {
        UiRestoreFailure_("rejected", error);
        return;
    }
    DiagEnd_("serialized-equal", "第五版快照已恢复并通过数据一致性校验；实机轨迹验收仍待验证");
    // Success notice (2026-10-07 用户裁定)：本插件悬浮框醒目色显示几秒，
    // 到时自动回常规状态行；模态弹窗只留给失败。
    char done[96] = {};
    _snprintf(done, sizeof(done), "已读档：第 %d 回合", capture.turn);
    UiMarkNoticeHighlight_(done);
    // No rendering after the final RNG commit in this handler.
}

static void UiProcessRestore_(H3CombatManager* mgr, int result)
{
    if (!g_restoreRequest.pending || g_restoreBusy) return;
    if (result == 2 || g_restoreRequest.generation != g_battleGeneration
        || GetTickCount() - g_restoreRequest.requested > 5000) {
        g_restoreRequest.pending = false;
        LogWarn("[Load] cancelled: battle changed, closing or wait timeout"); return;
    }
    if (RestoreWindow_(mgr)) UiExecuteRestore_(mgr);
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
    // 兜底路径与系统钩子并存（游戏轮询合成消息吞不掉）：不再置拖动，
    // 常驻列表不切换显隐；点击处理统一由系统钩子 → UiHandleFrameClick_ 完成。
    if (msg->subtype == eMsgSubtype::LBUTTON_DOWN && UiHitBar_(msg)) {
        return;
    }
    if (msg->subtype == eMsgSubtype::LBUTTON_CLICK) {
        if (UiHitBar_(msg)) {
            if (px >= g_ui.x + kUiBarWidth - 52) {
                g_ui.awaitingRebind = true;
                CancelSaveWait_("rebind entered");
                return;
            }
            UiReloadEntries_(H3CombatManager::Get());
            return;
        }
        const int row = UiHitRow_(px, py);
        if (row >= 0 && row < (int)g_ui.entries.size()) {
            UiConfirmAndRestore_(g_ui.entries[row]);
            return;
        }
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
        g_ui.rebindGuardUntil = GetTickCount() + 400;
        return;
    }
    if (UiKeyIsFree_(key)) {
        g_ui.saveKey = (char)key;
        UiSaveHotkey_();
        g_ui.awaitingRebind = false;
        g_ui.rebindKey = g_ui.saveKey;  // 该键松开前不触发存档
        g_ui.rebindGuardUntil = GetTickCount() + 400;
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
        g_ui.rebindGuardUntil = GetTickCount() + 400;
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
            g_ui.rebindKey = g_ui.saveKey;  // 该键松开前不触发存档
            g_ui.rebindGuardUntil = GetTickCount() + 400;
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
            CancelSaveWait_("rebind entered");
            LogInfo("点击快捷键区域：(%d,%d)", gameX, gameY);
        } else {
            UiReloadEntries_(H3CombatManager::Get());
            LogInfo("点击存档列表区域：(%d,%d)", gameX, gameY);
        }
        return;
    }
    const int row = UiHitRow_(gameX, gameY);
    if (row >= 0 && row < (int)g_ui.entries.size()) {
        UiConfirmAndRestore_(g_ui.entries[row]);
        return;
    }
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
