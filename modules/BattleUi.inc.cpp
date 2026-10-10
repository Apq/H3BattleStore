// ========== BattleUi.inc.cpp ==========
// 战场悬浮条：可滚动存档列表、快捷键显示与读档确认。
// 绘制在 0x495C50 动画循环返回后，不占战场格子；交互在消息钩子内完成。

#include <ctime>
#include <string>
#include "UiLayout.hpp"
#include "../tests/UiLayoutRegression.hpp"

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

static hbs_ui::Layout g_uiLayout = hbs_ui::ForFont(0);
static int UiBandHeight_() { return g_uiLayout.bandHeight; }
static const int kUiBarWidth = 480;
// 常驻列表为时间和同秒文件编号留出空间，不跟悬浮条同宽。
// 正文168px，包含 "yyyymmdd-hhmmss_99" 的编号与左右边距。
static const int kUiListWidth = hbs_ui::ListWidth;
static int UiRowHeight_() { return g_uiLayout.rowHeight; }
static const int kUiListVisibleRows = hbs_ui::kUiListVisibleRows;
static const int kUiDefaultX = 16;
static const int kUiDefaultY = 4;
// HB_bg.pcx is one 480x48 image: log controls above, status and hotkey below.
static const int kUiLogPackX = 392;
static const int kUiLogPackWidth = 84;
static const int kUiLogLevelCellWidth = 76;
static const int kUiLogLevelPerRow = 5;
static const int kUiLogLevelRows = 5;
static const int kUiBgHeight = 48; // Source asset size, not the runtime two-band height.



static struct
{
    int x = kUiDefaultX;
    int y = kUiDefaultY;

    hbs_ui::ScrollList scroll;
    BattleUiPointerGesture_ listGesture;
    bool listRightHeld = false;
    bool awaitingRebind = false;
    char rebindKey = 0;          // 改键接受的键：必须先松开才允许触发存档
    char lastSavedStamp[32] = {};
    DWORD lastSavedUntil = 0;
    DWORD lastNoticeUntil = 0;    // 等待超时等结果提示，悬浮条短暂显示
    char lastNoticeGbk[96] = {};
    bool lastNoticeHighlight = false;  // 本条通知用醒目色（读档成功）；过期后自然回常规状态行
    DWORD rebindGuardUntil = 0;  // 改键生效后短窗内忽略该键，防误触发存档
    int hoverRow = -1;
    // 日志等级常驻单选列表：值即 g_log_level；点击行选中并写 user.ini。
    int logLevelHover = -1;
    int listR = 40, listG = 30, listB = 20;   // 下拉底色（背景图主色）
} g_ui;

// 输入管道（原 Entry，第3步随键盘/鼠标状态机迁入）：钩子事件写入、帧泵经
// 接口消费；跨线程可见用 Interlocked，语义与原实现逐行一致。
static volatile LONG g_pendingSaveKey = 0;
static volatile LONG g_pendingClickX = -1;
static volatile LONG g_pendingClickY = -1;

static void ClearBattleInputs_()
{
    InterlockedExchange(&g_pendingClickX, -1);
    InterlockedExchange(&g_pendingClickY, -1);
    InterlockedExchange(&g_pendingSaveKey, 0);
}


static int UiOriginY_() { return g_uiLayout.OriginY(g_ui.y); }
static int UiListRows_() { return g_ui.scroll.Rows(g_store.entries.size()); }
static int UiListDrawWidth_() {
    return kUiListWidth + (g_store.entries.size() > kUiListVisibleRows ? hbs_ui::ScrollbarWidth : 0);
}
static bool UiPointInList_(int px, int py) {
    return px >= g_ui.x && px < g_ui.x + UiListDrawWidth_()
        && py >= UiOriginY_() + g_uiLayout.ListTop()
        && py < UiOriginY_() + g_uiLayout.Height(UiListRows_());
}
static bool UiPointInScrollbar_(int px, int py) {
    return g_store.entries.size() > kUiListVisibleRows && px >= g_ui.x + kUiListWidth
        && UiPointInList_(px, py);
}
static void UiCancelReleasedPointer_() {
    g_ui.listGesture.StopReleasedDrag(g_ui.scroll.dragging,
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    if (!(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) g_ui.listRightHeld = false;
}
static void UiScrollBy_(int rows) {
    g_ui.scroll.Move(rows, g_store.entries.size());
    g_ui.hoverRow = -1;
}
static void UiScrollWheel_(int delta) {
    g_ui.scroll.Wheel(delta, g_store.entries.size());
    g_ui.hoverRow = -1;
}
static void UiScrollbarDown_(int py) {
    const int y = py - UiOriginY_() - g_uiLayout.ListTop();
    const auto bar = hbs_ui::ForList(g_uiLayout, g_store.entries.size(), g_ui.scroll.first);
    if (y < bar.buttonHeight) UiScrollBy_(-1);
    else if (y >= kUiListVisibleRows * UiRowHeight_() - bar.buttonHeight) UiScrollBy_(1);
    else if (y < bar.thumbTop) UiScrollBy_(-kUiListVisibleRows);
    else if (y >= bar.thumbTop + bar.thumbHeight) UiScrollBy_(kUiListVisibleRows);
    else {
        g_ui.scroll.dragging = true;
        g_ui.scroll.dragStartY = py;
        g_ui.scroll.dragFirst = g_ui.scroll.first;
    }
}
static void UiScrollbarDrag_(int py) {
    const auto bar = hbs_ui::ForList(g_uiLayout, g_store.entries.size(), g_ui.scroll.first);
    g_ui.scroll.first = bar.FirstAfterDrag(py - g_ui.scroll.dragStartY,
        g_ui.scroll.dragFirst, g_ui.scroll.LastFirst(g_store.entries.size()));
    g_ui.hoverRow = -1;
}

static const int GUARD_DRAW = GuardRegisterHook_("BattleStore.Draw");
static const int GUARD_COPY = GuardRegisterHook_("BattleStore.CopyPixels");


// 列表数据扫描在服务层 StoreReloadList_；这里清输入管道并重置界面滚动、悬停与手势。
static bool UiReloadEntries_(const H3CombatManager* mgr)
{
    ClearBattleInputs_();
    g_ui.scroll = {};
    g_ui.hoverRow = -1;
    g_ui.listGesture = {};
    g_ui.listRightHeld = false;
    return StoreReloadList_(mgr);
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

// BarX/BarY 是界面布局状态（裁定见 docs/09 第7节）；热键读取在服务层。
static void UiLoadBarPosition_()
{
    g_ui.x = IniReadIntUtf8(g_user_ini_path, "Ui", "BarX", kUiDefaultX);
    g_ui.y = IniReadIntUtf8(g_user_ini_path, "Ui", "BarY", kUiDefaultY);
    g_store.saveKey = StoreLoadHotkey_();
}



static void UiMarkSaved_(uint64_t timestampUtcMs)
{
    StoreEntry entry = {};
    entry.timestampUtcMs = timestampUtcMs;
    StoreFormatStamp_(entry, g_ui.lastSavedStamp, sizeof(g_ui.lastSavedStamp));
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

// 单张两行底图，沿用 RGB 三平面 PCX 解码与游戏位深转换路径。
static H3LoadedPcx16* UiLoadBg_(const wchar_t* filename, H3LoadedPcx16*& cached, bool& failed)
{
    if (cached || failed) return cached;
    failed = true;

    const size_t pathCap = kPathCap_ / 2;
    wchar_t* wpath = new(std::nothrow) wchar_t[pathCap]();
    if (!wpath) return nullptr;
    const DWORD length = GetModuleFileNameW(g_hModule, wpath, (DWORD)pathCap);
    if (!length || length >= pathCap) { delete[] wpath; return nullptr; }
    wchar_t* slash = wcsrchr(wpath, L'\\');
    if (!slash || wcscpy_s(slash + 1, pathCap - (slash + 1 - wpath), filename) != 0) {
        delete[] wpath;
        return nullptr;
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, wpath, L"rb") != 0 || !file) {
        LogWarn("背景图加载失败：%ls", filename);
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
        || width != kUiBarWidth || height != kUiBgHeight || bpl < width) {
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

    cached = H3LoadedPcx16::Create(width, height);
    if (!cached || !cached->buffer) {
        if (cached) { cached->Destroy(); cached = nullptr; }
        free(raw);
        return nullptr;
    }
    const bool out32 = H3BitMode::Get() == 4;
    for (int py = 0; py < height; ++py) {
        const BYTE* red = raw + (size_t)py * bpl * planes;
        const BYTE* green = red + bpl;
        const BYTE* blue = green + bpl;
        BYTE* row = cached->buffer + (size_t)py * cached->scanlineSize;
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
    failed = false;
    LogInfo("背景图已加载：%ls %dx%d", filename, width, height);
    return cached;
}

// HB_bg.pcx: centered HA_bg texture with baked gold border, one 480x48 image.
static H3LoadedPcx16* UiLoadBarBg_()
{
    return UiLoadBg_(L"img\\HB_bg.pcx", g_barBg, g_barBgFailed);
}

// 同位深 pcx16 区域复制（src/dst 均按当前游戏位深分配，行内逐像素等宽）。
static void UiCopyBgRegion_(H3LoadedPcx16* dst, const H3LoadedPcx16* src,
    int srcX, int srcY, int w, int h, int dstY)
{
    const size_t px = H3BitMode::Get() == 4 ? 4 : 2;
    for (int row = 0; row < h; ++row) {
        const BYTE* s = src->buffer + (size_t)(srcY + row) * src->scanlineSize + (size_t)srcX * px;
        BYTE* d = dst->buffer + (size_t)(dstY + row) * dst->scanlineSize;
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
    static bool redrawing = false;
    if (redrawing) return;
    __try {
        UiCancelReleasedPointer_();
        H3WindowManager* wnd = H3WindowManager::Get();
        H3Font* font = H3SmallFont::Get();
        if (!wnd || !font || !UiDDBackBuffer_()) return;
        const hbs_ui::Layout layout = hbs_ui::ForFont(font->height);
        g_uiLayout = layout;
        g_ui.x = g_ui.y = 8;
        const int y = UiOriginY_();
        const int compositeH = layout.Height(kUiListVisibleRows);
        if (!g_barComposite || !g_barComposite->buffer
            || g_barComposite->height != compositeH || g_barComposite->width != kUiBarWidth) {
            if (g_barComposite) g_barComposite->Destroy();
            g_barComposite = H3LoadedPcx16::Create(kUiBarWidth, compositeH);
            if (!g_barComposite || !g_barComposite->buffer) return;
        }
        static DWORD lastDrawInfo = 0;
        const DWORD infoNow = GetTickCount();
        const bool logInfo = LogEnabled_(LOG_TRACE) && infoNow - lastDrawInfo >= 30000;
        if (logInfo) lastDrawInfo = infoNow;
        H3LoadedPcx16* c = g_barComposite;
        // 固定左上角（2026-10-05 用户定稿：不可拖动，每帧同位置重画）。
        const int x = g_ui.x;
        g_ui.scroll.Clamp(g_store.entries.size());
        const int rows = UiListRows_();
        const int listWidth = UiListDrawWidth_();
        const H3POINT cursor = H3POINT::GetCursorPosition();
        g_ui.hoverRow = cursor.x >= x && cursor.x < x + kUiListWidth
            ? g_uiLayout.HitRow(cursor.y - y, rows) : -1;
        // 列表高度随实际行数自适应（2026-10-05 用户实测纠正：固定满高会显示
        // 一堆空行背景板）；成品图行分隔线在每行底部，任意行数展开底边闭合。
        const int usedH = UiBandHeight_() + UiBandHeight_() + rows * UiRowHeight_();
        // 残影跟踪：位置/高度变化时，本帧末尾把上一帧矩形从 screenPcx16 拷回
        // backbuffer（HD 增量呈现不会自动覆盖旧区域，2026-10-05 拖动实测残影）。
        static int lastX = -1;
        static int lastY = -1;
        static int lastH = -1;
        static int lastBlockH = -1;
        static int lastListWidth = -1;
        static H3CombatManager* lastMgr = nullptr;
        if (lastMgr != mgr) {
            lastX = lastY = lastH = lastBlockH = lastListWidth = -1;
            lastMgr = mgr;
        }
        // 两行悬浮框矩形恒定，残影跟踪只跟随存档列表行数。
        const int totalH = usedH;
        const bool rectChanged = lastX != x || lastY != y || lastH != totalH
            || lastBlockH != g_uiLayout.ListTop() || lastListWidth != listWidth;
        // 每帧清底；列表区只清列表宽度（列表窄于悬浮条）
        c->FillRectangle(0, 0, kUiBarWidth, UiBandHeight_(), 0, 0, 0);
        c->FillRectangle(0, UiBandHeight_(), kUiBarWidth, UiBandHeight_(), 0, 0, 0);
        c->FillRectangle(0, g_uiLayout.ListTop(), kUiListWidth + hbs_ui::ScrollbarWidth,
            kUiListVisibleRows * UiRowHeight_(), 0, 0, 0);
        H3LoadedPcx16* bg = UiLoadBarBg_();
        const bool bgOk = bg && bg->buffer
            && bg->width == kUiBarWidth && bg->height == kUiBgHeight;
        if (bgOk) {
            for (int py = 0; py < g_uiLayout.ListTop(); ++py)
                UiCopyBgRegion_(c, bg, 0, hbs_ui::TextureY(py, UiBandHeight_()),
                    kUiBarWidth, 1, py);
            // 下拉列表底色 = 背景图主色（2026-10-06 用户裁定：选项底色接近背景图，
            // 不要黑蓝对比）。稀疏采样均值做主色，两行图仍仅千余点。
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
            c->FillRectangle(0, 0, kUiBarWidth, g_uiLayout.ListTop(), 20, 20, 20);
            c->DrawFrame(0, 0, kUiBarWidth, g_uiLayout.ListTop(), 200, 180, 90);
            g_ui.listR = 20; g_ui.listG = 20; g_ui.listB = 20;
        }
        if (rows > 0) {
            c->FillRectangle(0, g_uiLayout.ListTop(), listWidth, rows * UiRowHeight_(),
                (BYTE)g_ui.listR, (BYTE)g_ui.listG, (BYTE)g_ui.listB);
            c->DrawFrame(0, g_uiLayout.ListTop(), listWidth,
                rows * UiRowHeight_(), 160, 140, 70);
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
        else if (g_store.entries.empty())
            UiToGbk_("[战场存档] 无存档", label, sizeof(label));
        else {
            char utf8[96] = {};
            _snprintf(utf8, sizeof(utf8), "[战场存档] %u 条，点击选择", (unsigned)g_store.entries.size());
            UiToGbk_(utf8, label, sizeof(label));
        }
        while (font->GetMaxLineWidth(label) > hbs_ui::StatusLabelWidth && label[0]) {
            char* end = label + strlen(label);
            *CharPrevExA(936, label, end, 0) = 0;
        }
        font->TextDraw(c, label, 6, UiBandHeight_(), hbs_ui::StatusLabelWidth, UiBandHeight_(),
            labelColor, eTextAlignment::MIDDLE_LEFT);
        const bool storageAllowed = !g_restoreBusy && !g_restoreRequest.pending
            && CombatStorageWindow_(mgr);
        static int lastStorageAllowed = -1;
        static unsigned lastStorageGeneration = ~0u;
        if (lastStorageAllowed != (int)storageAllowed || lastStorageGeneration != g_battleGeneration) {
            LogDebug("[StorageWindow] allowed=%d generation=%u initialized=%d human_side=%d action=%d executor=%d spell=%d busy=%d pending=%d",
                storageAllowed ? 1 : 0, g_battleGeneration, g_battleInitialized ? 1 : 0,
                mgr->currentActiveSide, (int)mgr->action, g_executorDepth, g_spellDepth,
                g_restoreBusy ? 1 : 0, g_restoreRequest.pending ? 1 : 0);
            lastStorageAllowed = (int)storageAllowed;
            lastStorageGeneration = g_battleGeneration;
        }
        const hbs_ui::LampColor lamp = hbs_ui::StatusLampColor(storageAllowed);
        const int lampX = hbs_ui::StatusLampX;
        const int lampY = hbs_ui::StatusLampY(g_uiLayout);
        const int radius = hbs_ui::StatusLampRadius;
        for (int dy = -radius - 1; dy <= radius + 1; ++dy) {
            const int half = hbs_ui::LampHalfWidth(dy, radius + 1);
            c->FillRectangle(lampX - half, lampY + dy, 2 * half + 1, 1, 30, 30, 30);
        }
        for (int dy = -radius; dy <= radius; ++dy) {
            const int half = hbs_ui::LampHalfWidth(dy, radius);
            c->FillRectangle(lampX - half, lampY + dy, 2 * half + 1, 1,
                lamp.r, lamp.g, lamp.b);
        }
        c->FillRectangle(lampX - 3, lampY - 3, 3, 2, 220, 245, 225);
        char key[16] = {};
        char keyUtf8[8] = {};
        _snprintf(keyUtf8, sizeof(keyUtf8), "键:%c", g_store.saveKey);
        UiToGbk_(keyUtf8, key, sizeof(key));
        font->TextDraw(c, key, hbs_ui::HotkeyX, UiBandHeight_(), 52, UiBandHeight_(),
            eTextColor::WHITE, eTextAlignment::MIDDLE_CENTER);
        c->DrawFrame(hbs_ui::HotkeyX, UiBandHeight_() + 2, 54, UiBandHeight_() - 4, 220, 200, 110);
        // 固定横排五级，不展开收起；选中与悬停只重绘同一矩形。
        const int levelNow = (g_log_level >= LOG_TRACE && g_log_level <= LOG_ERROR)
            ? g_log_level : LOG_DEBUG;
        for (int i = 0; i < kUiLogLevelRows; ++i) {
            const int cellX = (i % kUiLogLevelPerRow) * kUiLogLevelCellWidth;
            const int cellY = (i / kUiLogLevelPerRow) * UiBandHeight_();
            if (i == g_ui.logLevelHover || i == levelNow) {
                const int fillR = i == g_ui.logLevelHover ? 90 : 70;
                const int fillG = i == g_ui.logLevelHover ? 70 : 55;
                const int fillB = i == g_ui.logLevelHover ? 20 : 25;
                c->FillRectangle(cellX + 3, cellY + 3, kUiLogLevelCellWidth - 6,
                    UiBandHeight_() - 6, (BYTE)fillR, (BYTE)fillG, (BYTE)fillB);
            }
            char itemUtf8[24] = {};
            const char* mark = i == levelNow ? "(*) " : "( ) ";
            _snprintf(itemUtf8, sizeof(itemUtf8), "%s%s", mark, LogLevelDisplayName_(i));
            char itemGbk[32] = {};
            UiToGbk_(itemUtf8, itemGbk, sizeof(itemGbk));
            font->TextDraw(c, itemGbk, cellX + 4, cellY, kUiLogLevelCellWidth - 8,
                UiBandHeight_(),
                i == levelNow ? eTextColor::LIGHT_GREEN : eTextColor::WHITE,
                eTextAlignment::MIDDLE_CENTER);
        }
        char packLabel[32] = {};
        UiToGbk_("打包日志", packLabel, sizeof(packLabel));
        font->TextDraw(c, packLabel, kUiLogPackX + 2, 0, kUiLogPackWidth - 4,
            UiBandHeight_(), eTextColor::WHITE, eTextAlignment::MIDDLE_CENTER);
        c->DrawFrame(kUiLogPackX, 3, kUiLogPackWidth, UiBandHeight_() - 6, 220, 200, 110);
        if (rows > 0) {
            const int listY = UiBandHeight_() + UiBandHeight_();
            for (int row = 0; row < rows; ++row) {
                if (row == g_ui.hoverRow)
                    c->FillRectangle(2, listY + row * UiRowHeight_(),
                        kUiListWidth - 4, UiRowHeight_(), 90, 70, 20);
                char stamp[32] = {};
                StoreFormatStamp_(g_store.entries[g_ui.scroll.first + row], stamp, sizeof(stamp));
                font->TextDraw(c, stamp, 6, listY + row * UiRowHeight_(),
                    kUiListWidth - 12, UiRowHeight_(), eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
            }
        }
        if (g_store.entries.size() > kUiListVisibleRows) {
            const auto bar = hbs_ui::ForList(layout, g_store.entries.size(), g_ui.scroll.first);
            const int sx = kUiListWidth;
            const int sy = layout.ListTop();
            const int height = rows * UiRowHeight_();
            c->FillRectangle(sx, sy, hbs_ui::ScrollbarWidth, height, 45, 45, 36);
            c->DrawFrame(sx, sy, hbs_ui::ScrollbarWidth, height, 160, 140, 70);
            c->FillRectangle(sx + 2, sy + bar.thumbTop, hbs_ui::ScrollbarWidth - 4,
                bar.thumbHeight, 155, 137, 85);
            c->DrawFrame(sx + 2, sy + bar.thumbTop, hbs_ui::ScrollbarWidth - 4,
                bar.thumbHeight, 220, 200, 110);
            c->DrawFrame(sx, sy, hbs_ui::ScrollbarWidth, bar.buttonHeight, 160, 140, 70);
            c->DrawFrame(sx, sy + height - bar.buttonHeight, hbs_ui::ScrollbarWidth,
                bar.buttonHeight, 160, 140, 70);
            for (int i = 0; i < 4; ++i) {
                c->FillRectangle(sx + 6 - i, sy + 4 + i, 1 + 2 * i, 1, 230, 210, 130);
                c->FillRectangle(sx + 6 - i, sy + height - 5 - i, 1 + 2 * i, 1, 230, 210, 130);
            }
        }
        bool bltOk = false;
        redrawing = true;
        if (rectChanged && lastX >= 0 && lastY != y && wnd->screenPcx16) {
            if (UiBltPcx16Region_(wnd->screenPcx16, lastX, lastY, kUiBarWidth,
                lastBlockH, lastX, lastY)) wnd->H3Redraw(lastX, lastY, kUiBarWidth, lastBlockH);
            if (lastH > lastBlockH && UiBltPcx16Region_(wnd->screenPcx16,
                lastX, lastY + lastBlockH, lastListWidth, lastH - lastBlockH,
                lastX, lastY + lastBlockH))
                wnd->H3Redraw(lastX, lastY + lastBlockH, lastListWidth, lastH - lastBlockH);
        }
        if (rectChanged && lastX >= 0 && lastY == y && lastBlockH > g_uiLayout.ListTop() && wnd->screenPcx16) {
            const int tailY = lastY + g_uiLayout.ListTop();
            const int tailH = lastBlockH - g_uiLayout.ListTop();
            if (UiBltPcx16Region_(wnd->screenPcx16, lastX, tailY, kUiBarWidth,
                tailH, lastX, tailY)) wnd->H3Redraw(lastX, tailY, kUiBarWidth, tailH);
        }
        if (rectChanged && lastX >= 0 && lastY == y && lastH > totalH && wnd->screenPcx16) {
            const int tailY = lastY + totalH;
            if (UiBltPcx16Region_(wnd->screenPcx16, lastX, tailY, lastListWidth,
                lastH - totalH, lastX, tailY)) wnd->H3Redraw(lastX, tailY, lastListWidth, lastH - totalH);
        }
        if (rectChanged && lastX >= 0 && lastY == y && lastListWidth > listWidth && wnd->screenPcx16) {
            const int stripX = lastX + listWidth;
            const int stripY = lastY + g_uiLayout.ListTop();
            const int stripH = totalH - g_uiLayout.ListTop();
            if (stripH > 0 && UiBltPcx16Region_(wnd->screenPcx16, stripX, stripY,
                lastListWidth - listWidth, stripH, stripX, stripY))
                wnd->H3Redraw(stripX, stripY, lastListWidth - listWidth, stripH);
        }
        // 两行悬浮框整块呈现，存档列表仅覆盖自身窄矩形。
        bltOk = UiBltPcx16Region_(c, 0, 0, kUiBarWidth, g_uiLayout.ListTop(), x, y);
        if (rows > 0)
            bltOk = UiBltPcx16Region_(c, 0, g_uiLayout.ListTop(), listWidth,
                rows * UiRowHeight_(), x, y + g_uiLayout.ListTop()) && bltOk;
        wnd->H3Redraw(x, y, kUiBarWidth, g_uiLayout.ListTop());
        if (rows > 0) wnd->H3Redraw(x, y + UiBandHeight_() + UiBandHeight_(),
            listWidth, rows * UiRowHeight_());
        redrawing = false;
        lastX = x;
        lastY = y;
        lastH = totalH;
        lastBlockH = g_uiLayout.ListTop();
        lastListWidth = listWidth;
        static LogFailureWindow_ bltFailures;
        unsigned skipped = 0;
        const int bltReport = bltFailures.Observe(!bltOk, infoNow, 30000, &skipped);
        if (bltReport == 1)
            LogWarn("[Draw] blt failed pos=%d,%d rows=%d suppressed=%u", x, y, rows, skipped);
        else if (bltReport == 2)
            LogInfo("[Draw] blt recovered stable_ms=30000 suppressed=%u", skipped);
        if (rectChanged || logInfo)
            WriteLogLv(rectChanged ? LOG_DEBUG : LOG_TRACE,
                "[Draw] composite=%p backbuffer=%p pos=%d,%d rows=%d blt=%d",
                c, UiDDBackBuffer_(), x, y, rows, bltOk ? 1 : 0);
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
    if (dw < kUiBarWidth || dh < UiBandHeight_() + UiBandHeight_()) return;
    if (g_ui.x < dx) g_ui.x = dx;
    const int bleed = (UiBandHeight_() - 24) / 2;
    if (UiOriginY_() < dy) g_ui.y = dy + bleed;
    if (g_ui.x + kUiBarWidth > dx + dw)
        g_ui.x = dx + dw - kUiBarWidth;
    if (UiOriginY_() + UiBandHeight_() + UiBandHeight_() > dy + dh)
        g_ui.y = dy + dh - g_uiLayout.ListTop() + bleed;
}

static bool UiPointInBar_(int px, int py)
{
    return px >= g_ui.x && px < g_ui.x + kUiBarWidth
        && py >= UiOriginY_() + UiBandHeight_()
        && py < UiOriginY_() + UiBandHeight_() + UiBandHeight_();
}

// 悬浮框体系整块（等级行+条本体）区域，用于鼠标吞并与外点判定。
static bool UiPointInUiBlock_(int px, int py)
{
    return px >= g_ui.x && px < g_ui.x + kUiBarWidth
        && py >= UiOriginY_() && py < UiOriginY_() + UiBandHeight_() + UiBandHeight_();
}

static bool UiHitBar_(const H3Msg* msg, bool fullBlock = false)
{
    if (!msg) return false;
    const auto hit = fullBlock ? UiPointInUiBlock_ : UiPointInBar_;
    const H3POINT cursor = H3POINT::GetCursorPosition();
    if (hit(cursor.x, cursor.y)) return true;
    if (hit(msg->position.x, msg->position.y)) return true;
    const H3CombatManager* mgr = H3CombatManager::Get();
    const BYTE* dlgBytes = reinterpret_cast<const BYTE*>(mgr ? mgr->dlg : nullptr);
    if (!dlgBytes) return false;
    const int dlgX = *reinterpret_cast<const INT32*>(dlgBytes + 0x18);
    const int dlgY = *reinterpret_cast<const INT32*>(dlgBytes + 0x1C);
    return hit(dlgX + msg->position.x, dlgY + msg->position.y);
}

static ptrdiff_t UiHitRow_(int px, int py)
{
    if (px < g_ui.x || px >= g_ui.x + kUiListWidth) return -1;
    g_ui.scroll.Clamp(g_store.entries.size());
    return g_ui.scroll.Hit(g_uiLayout, py - UiOriginY_(), g_store.entries.size());
}

static int UiHitLogLevelItem_(int px, int py)
{
    const int dx = px - g_ui.x;
    const int dy = py - UiOriginY_();
    if (dx < 0 || dx >= kUiLogLevelRows * kUiLogLevelCellWidth
        || dy < 0 || dy >= UiBandHeight_()) return -1;
    return dx / kUiLogLevelCellWidth;
}

static bool UiHitLogPack_(int px, int py)
{
    return px >= g_ui.x + kUiLogPackX && px < g_ui.x + kUiLogPackX + kUiLogPackWidth
        && py >= UiOriginY_() && py < UiOriginY_() + UiBandHeight_();
}

static void UiPackLogs_()
{
    // Both input paths can report one click; do not create the same pack twice.
    static DWORD lastPack = 0;
    static bool packing = false;
    const DWORD now = GetTickCount();
    if (packing || (lastPack && now - lastPack < 1000)) return;
    lastPack = now;
    packing = true;
    GuardFlushFaults_("log-feedback", true);
    LogRecentContext_("log-feedback");
    char reason[192] = {};
    if (PackRecentLogs_(nullptr, 0, reason, sizeof(reason))) {
        UiMarkNoticeHighlight_("日志已打包并复制，可粘贴发送");
        wchar_t message[512] = {};
        Utf8ToWide_(kLogPackSuccess_, message, (int)_countof(message));
        MessageBoxW(nullptr, message, L"日志已打包", MB_OK | MB_ICONINFORMATION);
    } else {
        UiMarkNotice_("日志打包失败，详见提示");
        LogWarn("[LogPack] 打包失败：%s", reason);
        wchar_t message[256] = {};
        Utf8ToWide_(reason, message, (int)_countof(message));
        MessageBoxW(nullptr, message, L"日志打包", MB_OK | MB_ICONWARNING);
    }
    lastPack = GetTickCount();
    packing = false;
}


static void UiSelectLogLevel_(int level)
{
    if (level < LOG_TRACE || level > LOG_ERROR) return;
    const int previous = g_log_level;
    if (!SaveLogLevel_(level)) {
        UiMarkNotice_("日志配置保存失败，等级未改变");
        LogError("[Config] 保存日志等级失败：target=%s keep=%s",
            LogLevelName_(level), LogLevelName_(previous));
        return;
    }
    LogInfo("[Config] 日志等级已切换并写入用户配置：%s -> %s",
        LogLevelName_(previous), LogLevelName_(level));
    UiMarkNotice_("日志等级已保存");
}

static void UiCancelRebind_(const char* reason)
{
    if (!g_ui.awaitingRebind) return;
    g_ui.awaitingRebind = false;
    LogInfo("改键状态已取消：%s", reason ? reason : "unknown");
}

static void UiConfirmAndRestore_(const StoreEntry& entry)
{
    if (g_restoreBusy || g_restoreRequest.pending
        || !CombatStorageWindow_(H3CombatManager::Get())) return;
    UiCancelRebind_("读档请求");
    // Copy before any dialog or redraw can invalidate the entries vector.
    g_restoreRequest.entry = entry;
    g_restoreRequest.battleKey = g_store.battleKey;
    g_restoreRequest.generation = g_battleGeneration;
    g_restoreRequest.requested = GetTickCount();
    g_restoreRequest.pending = true;
    ClearBattleInputs_();
    LogInfo("[Load] queued generation=%u depth=%d battle_depth=%d path=%s", g_battleGeneration,
        g_messageDepth, g_messageFrames.Depth(g_battleGeneration), DiagUtf8_(entry.path).c_str());
}


static void UiRestoreFailure_(const char* outcome, const std::string& raw)
{
    const std::string reason = StorePlayerTextZh_(raw);
    WriteLogLv(g_restoreFatal ? LOG_ERROR : LOG_WARN, "[Load op=%ld] raw=%s reason_zh=%s fatal=%d", g_diag.id, raw.c_str(), reason.c_str(), g_restoreFatal ? 1 : 0);
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
    const StoreEntry entry = g_restoreRequest.entry;
    const unsigned generation = g_restoreRequest.generation;
    const std::string expectedKey = g_restoreRequest.battleKey;
    g_restoreRequest.pending = false;
    g_restoreBusy = true;
    struct BusyReset { ~BusyReset() { ClearBattleInputs_(); g_restoreBusy = false; } } busyReset;
    DiagBegin_("load", "outer-message", mgr);
    char stamp[32] = {};
    StoreFormatStamp_(entry, stamp, sizeof(stamp));
    char text[192] = {}, utf8[192] = {};
    _snprintf(utf8, sizeof(utf8), "读回 %s 这一档？当前未保存的进度会丢掉。", stamp);
    UiToGbk_(utf8, text, sizeof(text));
    DiagStage_("load.confirm");
    const bool confirmed = H3Messagebox::Choice(text);
    if (g_restoreFatal) return;
    if (!confirmed) { DiagEnd_("cancelled", "玩家取消读档确认"); return; }
    std::string key, error;
    if (generation != g_battleGeneration || !RestoreWindow_(mgr)) {
        DiagEnd_("cancelled", "restore timing no longer available");
        return;
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
        UiRestoreFailure_("failed", StoreArchiveReason_(storeError, code)); return;
    }
    if (!storeError.empty())
        LogWarn("[Load op=%ld] scan warning raw=%s reason_zh=%s", g_diag.id,
            DiagUtf8_(storeError).c_str(), StorePlayerTextZh_(DiagUtf8_(storeError)).c_str());
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
            UiRestoreFailure_("failed", StoreArchiveReason_(selectedError, code)); return;
        }
        const DWORD attributes = GetFileAttributesW(entry.path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD code = GetLastError();
            UiRestoreFailure_("failed", StoreArchiveReason_(L"cannot open archive", code)); return;
        }
        UiRestoreFailure_("rejected", "record metadata does not match archive"); return;
    }
    hbs::ArchiveDocument document;
    storeError.clear();
    SetLastError(ERROR_SUCCESS);
    if (!store.Load(*found, document, storeError)) {
        const DWORD code = GetLastError();
        UiRestoreFailure_("failed", StoreArchiveReason_(storeError, code)); return;
    }
    if (document.battleKey != key) {
        LogWarn("[Load op=%ld] archive_battle=%s current_battle=%s", g_diag.id, document.battleKey.c_str(), key.c_str());
        UiRestoreFailure_("rejected", "archive battle key mismatch"); return;
    }
    std::unique_ptr<CodecCapture> captureStorage(new CodecCapture{});
    CodecCapture& capture = *captureStorage;
    if (!CodecDecode(document.sections, &capture, &error)) {
        UiRestoreFailure_("rejected", error);
        return;
    }
    // 修复前存量档可能带悬挂瞬态链接（保存于换阵重打的新战斗），解码后同样规范
    // 化，避免 RestorePolicy_ 用"存档中的电脑行动目标无效"误拒整档（2026-10-07
    // 玩家日志 Op17-20）。丢弃量进 debug 日志留证。
    const CodecLinkDropReport_ droppedLinks = CodecNormalizeStaleLinks_(capture);
    if (droppedLinks.aiTargets || droppedLinks.relationEntries)
        LogDebug("[Load op=%ld] stale links dropped from archive ai_targets=%u relation_entries=%u",
            g_diag.id, droppedLinks.aiTargets, droppedLinks.relationEntries);
    if (!RestoreSameBattle_(mgr, capture, key, &error)) {
        UiRestoreFailure_("rejected", error);
        return;
    }
    UiCancelRebind_("读档完成");
    DiagEnd_("serialized-equal", "快照已恢复并通过白名单数据校验；实机轨迹验收仍待验证");
    // Success notice (2026-10-07 用户裁定)：本插件悬浮框醒目色显示几秒，
    // 到时自动回常规状态行；模态弹窗只留给失败。
    char done[96] = {};
    _snprintf(done, sizeof(done), "已读档：第 %d 回合", capture.turn);
    UiMarkNoticeHighlight_(done);
    // No rendering after the final RNG commit in this handler.
}

// 独立的读档请求维护（2026-10-10 修复）：每帧在绘制循环调用，不受恢复安全点
// 与消息深度门控。只取消排队状态（超时/换场/战斗结束），绝不写战斗内存。
// 之前超时检查埋在 UiProcessRestore_ 里，消费入口被绝对深度阻断时连取消
// 都不运行，玩家侧表现为点击读档后红灯五分多钟。
static void UiMaintainRestore_(H3CombatManager* mgr)
{
    if (!g_restoreRequest.pending || g_restoreBusy || g_restoreFatal) return;
    const bool battleAvailable = CombatIsReadable_(mgr) && !mgr->finished;
    const BattleRestoreMaintainState_ state = {true, battleAvailable,
        g_restoreRequest.generation, g_battleGeneration,
        (unsigned long)(GetTickCount() - g_restoreRequest.requested)};
    switch (BattleRestoreMaintainDecision_(state)) {
    case BattleRestoreMaintain_::Keep:
        return;
    case BattleRestoreMaintain_::CancelBattleChanged:
        LogInfo("[Load] cancelled reason=battle-changed entry_generation=%u current_generation=%u age_ms=%lu",
            g_restoreRequest.generation, g_battleGeneration, state.ageMs);
        g_restoreRequest.pending = false;
        return;
    case BattleRestoreMaintain_::CancelTimeout:
    default:
        LogInfo("[Load] cancelled reason=timeout age_ms=%lu generation=%u depth=%d battle_depth=%d window=%d",
            state.ageMs, g_battleGeneration, g_messageDepth,
            g_messageFrames.Depth(g_battleGeneration), CombatStorageWindow_(mgr) ? 1 : 0);
        g_restoreRequest.pending = false;
        UiMarkNotice_("读档等待超时，已取消；请重试");
        return;
    }
}

static void UiProcessRestore_(H3CombatManager* mgr, int result)
{
    if (!g_restoreRequest.pending || g_restoreBusy) return;
    if (result == 2 || g_restoreRequest.generation != g_battleGeneration
        || GetTickCount() - g_restoreRequest.requested > kRestoreQueueTimeoutMs_) {
        g_restoreRequest.pending = false;
        return;
    }
    if (!RestoreWindow_(mgr)) {
        g_restoreRequest.pending = false;
        return;
    }
    UiExecuteRestore_(mgr);
}
static void UiHandleMouse_(H3Msg* msg)
{
    if (!msg) return;
    const H3POINT cursor = H3POINT::GetCursorPosition();
    const int px = cursor.x;
    const int py = cursor.y;
    if (msg->command == eMsgCommand::MOUSE_OVER) {
        g_ui.hoverRow = px >= g_ui.x && px < g_ui.x + kUiListWidth
            ? g_uiLayout.HitRow(py - UiOriginY_(), UiListRows_()) : -1;
        g_ui.logLevelHover = UiHitLogLevelItem_(px, py);
        return;
    }
    // Do not dispatch 0x200 here: native button hotkeys share that command.
    // Actual clicks are handled exclusively by the system mouse hook.
}


static void UiHandleRebindKey_(char key, bool escape)
{
    if (escape) {
        UiCancelRebind_("escape");
        g_ui.rebindGuardUntil = GetTickCount() + 400;
        return;
    }
    if (StoreKeyIsFree_(key)) {
        g_store.saveKey = (char)key;
        StoreSaveHotkey_();
        g_ui.awaitingRebind = false;
        g_ui.rebindKey = g_store.saveKey;  // 该键松开前不触发存档
        g_ui.rebindGuardUntil = GetTickCount() + 400;
        LogInfo("改键完成：%c source=game", g_store.saveKey);
    }
}

// 改键确认不依赖战斗消息钩子：等待期间在绘制帧轮询全部允许字母与 Esc。
static void UiPollRebindKey_()
{
    if (!g_ui.awaitingRebind) return;
    static bool prevDown[16] = {};
    const int escIndex = (int)strlen(kStoreFreeKeys_);
    const bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escDown && !prevDown[escIndex]) {
        UiCancelRebind_("escape-poll");
        g_ui.rebindGuardUntil = GetTickCount() + 400;
        prevDown[escIndex] = escDown;
        return;
    }
    prevDown[escIndex] = escDown;
    for (int i = 0; kStoreFreeKeys_[i]; ++i) {
        const bool down = (GetAsyncKeyState((int)kStoreFreeKeys_[i]) & 0x8000) != 0;
        if (down && !prevDown[i]) {
            g_store.saveKey = kStoreFreeKeys_[i];
            StoreSaveHotkey_();
            g_ui.awaitingRebind = false;
            g_ui.rebindKey = g_store.saveKey;  // 该键松开前不触发存档
            g_ui.rebindGuardUntil = GetTickCount() + 400;
            LogInfo("改键完成：%c", g_store.saveKey);
        }
        prevDown[i] = down;
    }
}

// 系统鼠标钩子路径的统一点击入口：gameX/gameY 为游戏逻辑坐标。
static void UiHandleFrameClick_(int gameX, int gameY)
{
    if (UiHitLogPack_(gameX, gameY)) {
        UiPackLogs_();
        return;
    }
    // 日志等级横向单选列表：点单元格即选中保存；等级块其余空位只吞并不动作。
    const int levelItem = UiHitLogLevelItem_(gameX, gameY);
    if (levelItem >= 0) {
        UiSelectLogLevel_(levelItem);
        LogDebug("点击日志等级行：%d", levelItem);
        return;
    }
    if (UiPointInUiBlock_(gameX, gameY) && !UiPointInBar_(gameX, gameY)) {
        LogDebug("点击日志等级块空位（无动作）：(%d,%d)", gameX, gameY);
        return;
    }
    if (UiPointInBar_(gameX, gameY)) {
        if (gameX >= g_ui.x + hbs_ui::HotkeyX) {
            g_ui.awaitingRebind = true;
            CancelSaveWait_("rebind entered");
            LogInfo("点击快捷键区域：(%d,%d)", gameX, gameY);
        } else {
            UiReloadEntries_(H3CombatManager::Get());
            LogDebug("点击存档列表区域：(%d,%d)", gameX, gameY);
        }
        return;
    }
    const ptrdiff_t row = UiHitRow_(gameX, gameY);
    if (row >= 0 && static_cast<size_t>(row) < g_store.entries.size()) {
        UiConfirmAndRestore_(g_store.entries[row]);
        return;
    }
    if (g_ui.awaitingRebind) g_ui.awaitingRebind = false;
}

// ---- 界面实现装配（docs/09 第2步立契约）：HdNativeUi 转发现有入口，行为零变化。 ----

// 换场清理：手势、悬停与改键交互状态；服务数据由 BattleReset_ 负责。
static void UiResetForBattle_()
{
    g_ui.scroll = {};
    g_ui.listGesture = {};
    g_ui.listRightHeld = false;
    g_ui.hoverRow = -1;
    g_ui.logLevelHover = -1;
    UiCancelRebind_("battle reset");
    g_ui.rebindKey = 0;
    g_ui.rebindGuardUntil = 0;
    ClearBattleInputs_();
}

// ---- 键盘/鼠标状态机（原 Entry 钩子体，第3步迁入；仅改调用通道，逻辑逐行等价） ----

// 系统键盘钩子：letter 翻译、改键/存档键日志与 pending 投递（闩锁占用）。
static bool UiOnSystemKey_(const UiKeyEvent_& e)
{
    const char letter = UiVirtualKeyToLetter_(e.vk, e.source == 0);
    if (letter && (letter == g_store.saveKey || g_ui.awaitingRebind))
        WriteLogLv(e.repeat && !e.up ? LOG_TRACE : LOG_DEBUG, "[Input] source=system event=%s repeat=%d vk=%d letter=%c save=%c rebind=%d latch=%c guard_remaining=%ld pending=%ld",
            e.up ? "up" : "down", e.repeat ? 1 : 0, e.vk, letter, g_store.saveKey,
            g_ui.awaitingRebind ? 1 : 0, g_ui.rebindKey ? g_ui.rebindKey : '-',
            (LONG)(g_ui.rebindGuardUntil - GetTickCount()), g_pendingSaveKey);
    if (letter && !e.up && !e.repeat) {
        if (g_ui.awaitingRebind || letter != g_store.saveKey
            || StorePlayerWindow_(H3CombatManager::Get(), 0, nullptr))
            InterlockedExchange(&g_pendingSaveKey, letter);
        else
            InterlockedExchange(&g_saveEdgeConsumed, 1);
    }
    return false;
}

// 系统鼠标钩子：命中判定、拖动/滚轮/手势状态机与点击投递（combatOpen=false 时清手势放行）。
static bool UiOnSystemMouse_(const UiMouseEvent_& e, bool combatOpen)
{
    if (!combatOpen) {
        g_ui.scroll.dragging = false;
        g_ui.listGesture = {};
        g_ui.listRightHeld = false;
        return false;
    }
    const bool move = e.kind == 0, leftDown = e.kind == 1, leftUp = e.kind == 2,
        rightDown = e.kind == 3, rightUp = e.kind == 4, wheel = e.kind == 5;
    const bool hitList = UiPointInList_(e.gameX, e.gameY);
    const bool hitScroll = UiPointInScrollbar_(e.gameX, e.gameY);
    const bool swallow = hitList || UiPointInUiBlock_(e.gameX, e.gameY);
    if (move || leftDown || rightDown || wheel) UiCancelReleasedPointer_();
    if (g_ui.scroll.dragging && (move || leftUp)) {
        UiScrollbarDrag_(e.gameY);
        if (leftUp) {
            g_ui.scroll.dragging = false;
            g_ui.listGesture = {};
        }
        return true;
    }
    if (wheel) {
        if (g_ui.scroll.dragging) return true;
        if (!hitList) return false;
        UiScrollWheel_((short)e.wheelDelta);
        g_ui.listGesture.CancelClick();
        InterlockedExchange(&g_pendingClickX, -1);
        InterlockedExchange(&g_pendingClickY, -1);
        LogDetail_("[List] wheel first=%u count=%u", (unsigned)g_ui.scroll.first, (unsigned)g_store.entries.size());
        return true;
    }
    if (move) return hitList;
    if (leftDown) {
        g_ui.listGesture = {};
        g_ui.scroll.dragging = false;
        if (!swallow) { UiCancelRebind_("outside mouse click"); return false; }
        g_ui.listGesture.Begin(hitScroll);
        if (hitScroll) {
            UiScrollbarDown_(e.gameY);
            InterlockedExchange(&g_pendingClickX, -1);
            InterlockedExchange(&g_pendingClickY, -1);
        }
        return true;
    }
    if (leftUp) {
        const auto release = g_ui.listGesture.Release(swallow, hitScroll);
        if (release == BattleUiRelease_::Pass) return false;
        if (release == BattleUiRelease_::Swallow) return true;
        if (hitList && !StorePlayerWindow_(H3CombatManager::Get(), 0, nullptr)) return true;
        InterlockedExchange(&g_pendingClickX, e.gameX);
        InterlockedExchange(&g_pendingClickY, e.gameY);
        LogDetail_("点击已吞并：game=(%d,%d)", e.gameX, e.gameY);
        return true;
    }
    if (rightDown) { g_ui.listRightHeld = swallow; return swallow; }
    if (rightUp) {
        const bool held = g_ui.listRightHeld;
        g_ui.listRightHeld = false;
        return swallow || held;
    }
    return false;
}

// 游戏键盘消息（原生前）：改键/存档键日志。
static void UiOnGameKeyBefore_(const H3Msg* msg, int level)
{
    const char pressed = UiVirtualKeyToLetter_(msg->subtype, false);
    if (pressed == g_store.saveKey || g_ui.awaitingRebind)
        WriteLogLv(level, "[Input] source=game event=%s cmd=%d subtype=%d letter=%c save=%c physical_down=%d rebind=%d latch=%c guard_remaining=%ld pending=%ld",
            msg->command == eMsgCommand::KEY_DOWN ? "down" : "up", (int)msg->command,
            (int)msg->subtype, pressed ? pressed : '?', g_store.saveKey,
            (GetAsyncKeyState(g_store.saveKey) & 0x8000) ? 1 : 0, g_ui.awaitingRebind ? 1 : 0,
            g_ui.rebindKey ? g_ui.rebindKey : '-', (LONG)(g_ui.rebindGuardUntil - GetTickCount()), g_pendingSaveKey);
}

// 游戏鼠标消息：吞并判定（条/列表/拖动中 → 手势处理并吞）。
static bool UiOnGameMouse_(H3Msg* msg)
{
    if (!msg || !BattleUiMayConsumeMessage_((int)msg->command)) return false;
    const bool onBar = UiHitBar_(msg, true);
    const bool onList = UiPointInList_(msg->position.x, msg->position.y);
    if (onBar || onList || g_ui.scroll.dragging) {
        UiHandleMouse_(msg);
        return true;
    }
    return false;
}

// 游戏键盘消息（原生后）：改键捕获与存档边沿触发。
static void UiOnGameKeyAfter_(H3CombatManager* mgr, const H3Msg* msg, int result, DWORD now, bool waitBlocked)
{
    const char pressed = UiVirtualKeyToLetter_(msg->subtype, false);
    if (g_ui.awaitingRebind) {
        UiHandleRebindKey_(pressed, msg->subtype == h3::NH3VKey::H3VK_ESCAPE);
        InterlockedExchange(&g_pendingSaveKey, 0);
        return;
    }
    if (pressed && pressed == g_store.saveKey) {
        static bool msgKeyWasDown = false;
        const bool downNow = (GetAsyncKeyState(g_store.saveKey) & 0x8000) != 0;
        if (g_ui.rebindKey && pressed == g_ui.rebindKey) {
            if (!downNow) g_ui.rebindKey = 0;
            LogTrace("[Input] save suppressed source=game reason=rebind-latch");
        } else if (downNow && !msgKeyWasDown && !waitBlocked
            && now >= g_ui.rebindGuardUntil) {
            if (!StoreRequestSave_(mgr, result, "game-message", g_ui.awaitingRebind, g_ui.rebindKey))
                UiMarkNotice_("保存失败：详见日志");
        } else {
            LogTrace("[Input] save suppressed source=game down=%d previous_down=%d wait=%d guard=%d",
                downNow ? 1 : 0, msgKeyWasDown ? 1 : 0, waitBlocked ? 1 : 0,
                now < g_ui.rebindGuardUntil ? 1 : 0);
        }
        msgKeyWasDown = downNow;
    }
}

// 帧内键轮询（原 CombatCycleAfter_ 中段）：改键保护窗、闩锁重 arm、
// 存档键边沿触发（2026-10-05 用户实测：改键单次短按立即松开也会穿透触发
// 存档——残留 hook 事件在改键完成后仍被消费；边沿要求"上一帧该键是松开的"）。
static void UiOnFrameKeyPoll_(H3CombatManager* mgr, DWORD now, bool waitBlocked)
{
    if (now < g_ui.rebindGuardUntil)
        InterlockedExchange(&g_pendingSaveKey, 0);  // 改键残留不触发存档
    // 存档键松开后重新武装单次边沿（配合 StoreRequestSave_ 的闩锁）
    if (!(GetAsyncKeyState(g_store.saveKey) & 0x8000))
        InterlockedExchange(&g_saveEdgeConsumed, 0);
    static bool keyWasDown = false;
    const char pressed = (char)InterlockedExchange(&g_pendingSaveKey, 0);
    if (pressed) LogDebug("[Input] source=frame letter=%c save=%c physical_down=%d previous_down=%d rebind=%d latch=%c wait=%d",
        pressed, g_store.saveKey, (GetAsyncKeyState(g_store.saveKey) & 0x8000) ? 1 : 0,
        keyWasDown ? 1 : 0, g_ui.awaitingRebind ? 1 : 0, g_ui.rebindKey ? g_ui.rebindKey : '-',
        waitBlocked ? 1 : 0);
    if (pressed == g_store.saveKey && !g_ui.awaitingRebind) {
        const bool downNow = (GetAsyncKeyState(g_store.saveKey) & 0x8000) != 0;
        if (g_ui.rebindKey && pressed == g_ui.rebindKey) {
            if (!downNow) g_ui.rebindKey = 0;  // 改键那次按键松开后才解锁
        } else if (downNow && !keyWasDown && now >= g_ui.rebindGuardUntil) {
            if (!StoreRequestSave_(mgr, 0, "system-frame", g_ui.awaitingRebind, g_ui.rebindKey))
                UiMarkNotice_("保存失败：详见日志");
        }
        keyWasDown = downNow;
    } else {
        keyWasDown = !g_ui.awaitingRebind
            && (GetAsyncKeyState(g_store.saveKey) & 0x8000) != 0;
    }
}

// 钩子故障清理：输入管道与手势状态（DiagHookFault_ 的界面部分）。
static void UiOnFaultCleanup_()
{
    InterlockedExchange(&g_pendingClickX, -1);
    InterlockedExchange(&g_pendingClickY, -1);
    InterlockedExchange(&g_pendingSaveKey, 0);
    g_ui.scroll.dragging = false;
    g_ui.listGesture = {};
    g_ui.listRightHeld = false;
}

class HdNativeUi final : public IBattleStoreUi {
public:
    void Draw(H3CombatManager* mgr) override { UiDrawBar_(mgr); }
    void PollHover() override
    {
        const H3POINT cursor = H3POINT::GetCursorPosition();
        g_ui.logLevelHover = UiHitLogLevelItem_(cursor.x, cursor.y);
    }
    void FrameClick() override
    {
        const LONG clickX = InterlockedExchange(&g_pendingClickX, -1);
        const LONG clickY = InterlockedExchange(&g_pendingClickY, -1);
        if (clickX >= 0 && clickY >= 0)
            UiHandleFrameClick_((int)clickX, (int)clickY);
    }
    void PollRebindKey() override { UiPollRebindKey_(); }
    bool ReloadEntries(const H3CombatManager* mgr) override { return UiReloadEntries_(mgr); }
    bool HitBar(H3Msg* msg, bool fullBlock) override { return UiHitBar_(msg, fullBlock); }
    void HandleMouse(H3Msg* msg) override { UiHandleMouse_(msg); }
    void MaintainRestore(H3CombatManager* mgr) override { UiMaintainRestore_(mgr); }
    void ProcessRestore(H3CombatManager* mgr, int result) override { UiProcessRestore_(mgr, result); }
    void CancelRebind(const char* reason) override { UiCancelRebind_(reason); }
    void MarkSaved(uint64_t timestampUtcMs) override { UiMarkSaved_(timestampUtcMs); }
    void MarkNotice(const char* utf8Text) override { UiMarkNotice_(utf8Text); }
    void OnBattleReset() override { UiResetForBattle_(); }
    bool OnSystemKey(const UiKeyEvent_& e) override { return UiOnSystemKey_(e); }
    bool OnSystemMouse(const UiMouseEvent_& e, bool combatOpen) override { return UiOnSystemMouse_(e, combatOpen); }
    void OnGameKeyBefore(const H3Msg* msg, int level) override { UiOnGameKeyBefore_(msg, level); }
    bool OnGameMouse(H3Msg* msg) override { return UiOnGameMouse_(msg); }
    void OnGameKeyAfter(H3CombatManager* mgr, const H3Msg* msg, int result, unsigned now, bool waitBlocked) override { UiOnGameKeyAfter_(mgr, msg, result, now, waitBlocked); }
    void OnFrameKeyPoll(H3CombatManager* mgr, unsigned now, bool waitBlocked) override { UiOnFrameKeyPoll_(mgr, now, waitBlocked); }
    void OnListReloaded() override { g_ui.hoverRow = -1; }
    void OnFaultCleanup() override { UiOnFaultCleanup_(); }
    bool IsRebindWaiting() const override { return g_ui.awaitingRebind; }
    char RebindLatchKey() const override { return g_ui.rebindKey; }
};

static HdNativeUi g_hdNativeUi;
// 钩子层唯一界面入口；Entry 不得再触碰 Ui* 内部符号与 g_ui 字段。
static IBattleStoreUi* const g_uiPort = &g_hdNativeUi;
