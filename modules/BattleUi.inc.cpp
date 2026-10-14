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
// 界面选择（2026-10-14 用户裁定）：两套界面同在一个 DLL 里，由玩家配置
// [Ui] OutsideBattlefield 决定用哪套——面板第一行的「战场外」复选框即时改写
// 该键值；不再用编译宏切。false=战场内右上角折叠灯（默认，勾选前的"当前这套"），
// true=战场外左上角固定悬浮条（旧界面，左上角那套）。
static bool g_uiOutsideBar = false;
// 战场外固定条锚点（2026-10-05 定稿：不可拖动，恒左上角）。
static const int kUiOutsideBarX = 8;
static const int kUiOutsideBarY = 8;
// 悬浮条锚点（左上角游戏坐标）。战场外界面恒 (8,8)；战场内折叠界面由
// FoldSyncAnchor_ 每帧改为战斗对话框内右上角。
static int g_uiBarAnchorX = kUiOutsideBarX;
static int g_uiBarAnchorY = kUiOutsideBarY;
// 折叠版把状态灯移出面板：面板右缘与灯左缘之间留间隙（灯不被面板压住），
// 灯由 BattleFoldBar 在面板绘制之后单独画在原位，收起/展开同一位置。
static bool g_uiFoldLayout = false;
// 第一行（日志等级项 +「战场外」复选框 + 打包日志）布局：项宽按字体实测，
// 每帧绘制前刷新；命中判定与绘制共用同一份（见 UiUpdateLogRow_）。
static hbs_ui::LogRow g_logRow = {};
static int g_logRowFontHeight = -1;
// 等级项最小宽度：过窄会让点击热区难以命中（正常字体下实测约 48px）。
static const int kUiLogRowMinLevelWidth = 40;
// HB_bg.pcx is one 480x72 image: log controls, status/hotkey, archive titles.
static const int kUiBgHeight = 72; // Three 24px source bands, adapted to runtime font heights.


static struct
{
    int x = kUiDefaultX;
    int y = kUiDefaultY;

    hbs_ui::ScrollList scroll;
    BattleUiPointerGesture_ listGesture;
    unsigned gestureListRevision = 0;
    unsigned pendingListRevision = 0;
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
    // 自动存档列表（docs/02 §4）：滚动/悬停与手动列表各自独立。
    hbs_ui::ScrollList autoScroll;
    int autoHoverRow = -1;
    // 日志等级常驻单选列表：值即 g_log_level；点击行选中并写 user.ini。
    int logLevelHover = -1;
    // 「战场外」复选框悬停（0/-1）：勾选即切到战场外固定条界面并写 user.ini。
    int outsideHover = -1;
    int listR = 40, listG = 30, listB = 20;   // 下拉底色（背景图主色）
} g_ui;

// 两套界面实例的访问器与折叠层清场函数：TU 内后置定义（BattleFoldBar.inc.cpp
// 必须排在 BattleUi.inc.cpp 之后），此处只需声明即可被分发器与切换逻辑引用。
static IBattleStoreUi* UiNativePort_();
static IBattleStoreUi* UiFoldPort_();
static void UiFoldLeaveUi_();

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
// ---- 双列表参数化（docs/02 §4）：which 0=手动（左侧原位）、1=自动（右移） ----
static const int kUiAutoListX = hbs_ui::AutoListX;
static const std::vector<StoreEntry>& UiEntriesOf_(int which)
{
    return which ? g_store.autoEntries : g_store.entries;
}
static hbs_ui::ScrollList& UiScrollOf_(int which)
{
    return which ? g_ui.autoScroll : g_ui.scroll;
}
static int& UiHoverOf_(int which)
{
    return which ? g_ui.autoHoverRow : g_ui.hoverRow;
}
static int UiListX_(int which) { return which ? kUiAutoListX : 0; }
static int UiListRows_(int which) { return UiScrollOf_(which).Rows(UiEntriesOf_(which).size()); }
static int UiPanelRows_() { return hbs_ui::Max(UiListRows_(0), UiListRows_(1)); }
static int UiListDrawWidth_(int which) {
    return kUiListWidth + (UiEntriesOf_(which).size() > kUiListVisibleRows ? hbs_ui::ScrollbarWidth : 0);
}
static bool UiPointInList_(int which, int px, int py) {
    const int lx = g_ui.x + UiListX_(which);
    return px >= lx && px < lx + UiListDrawWidth_(which)
        && py >= UiOriginY_() + g_uiLayout.ListTop()
        && py < UiOriginY_() + g_uiLayout.Height(UiListRows_(which));
}
static bool UiPointInAnyList_(int px, int py) {
    return UiPointInList_(0, px, py) || UiPointInList_(1, px, py);
}
static bool UiPointInScrollbar_(int which, int px, int py) {
    return UiEntriesOf_(which).size() > kUiListVisibleRows
        && px >= g_ui.x + UiListX_(which) + kUiListWidth
        && UiPointInList_(which, px, py);
}
static int UiListAt_(int px, int py) {
    for (int which = 0; which < 2; ++which)
        if (UiPointInList_(which, px, py)) return which;
    return -1;
}
static void UiCancelReleasedPointer_() {
    const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    g_ui.listGesture.StopReleasedDrag(g_ui.scroll.dragging, leftDown);
    g_ui.listGesture.StopReleasedDrag(g_ui.autoScroll.dragging, leftDown);
    if (!(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) g_ui.listRightHeld = false;
}
static void UiScrollBy_(int which, int rows) {
    UiScrollOf_(which).Move(rows, UiEntriesOf_(which).size());
    UiHoverOf_(which) = -1;
}
static void UiScrollWheel_(int which, int delta) {
    UiScrollOf_(which).Wheel(delta, UiEntriesOf_(which).size());
    UiHoverOf_(which) = -1;
}
static void UiScrollbarDown_(int which, int py) {
    const int y = py - UiOriginY_() - g_uiLayout.ListTop();
    const auto bar = hbs_ui::ForList(g_uiLayout, UiEntriesOf_(which).size(), UiScrollOf_(which).first);
    if (y < bar.buttonHeight) UiScrollBy_(which, -1);
    else if (y >= kUiListVisibleRows * UiRowHeight_() - bar.buttonHeight) UiScrollBy_(which, 1);
    else if (y < bar.thumbTop) UiScrollBy_(which, -kUiListVisibleRows);
    else if (y >= bar.thumbTop + bar.thumbHeight) UiScrollBy_(which, kUiListVisibleRows);
    else {
        UiScrollOf_(which).dragging = true;
        UiScrollOf_(which).dragStartY = py;
        UiScrollOf_(which).dragFirst = UiScrollOf_(which).first;
    }
}
static void UiScrollbarDrag_(int which, int py) {
    hbs_ui::ScrollList& scroll = UiScrollOf_(which);
    const auto bar = hbs_ui::ForList(g_uiLayout, UiEntriesOf_(which).size(), scroll.first);
    scroll.first = bar.FirstAfterDrag(py - scroll.dragStartY,
        scroll.dragFirst, scroll.LastFirst(UiEntriesOf_(which).size()));
    UiHoverOf_(which) = -1;
}

static const int GUARD_DRAW = GuardRegisterHook_("BattleStore.Draw");
static const int GUARD_COPY = GuardRegisterHook_("BattleStore.CopyPixels");

// UiDrawBar_ 的上一帧呈现矩形（残影跟踪）。收起态切换（第二套折叠界面）时
// 据此把旧区域从 screenPcx16 拷回，避免展开块留下残影。
static int uiTailX = -1;
static int uiTailY = -1;
static int uiTailH = -1;
static int uiTailBlockH = -1;
static int uiTailListWidth = -1;
static int uiTailRowHeight = 0;
static int uiTailColumnRows[2] = {-1, -1};
static int uiTailColumnWidths[2] = {-1, -1};
static H3CombatManager* uiTailMgr = nullptr;

// 存读档窗口判定 + 变化 debug 日志（状态灯与提示共用；两类界面一致口径）。
static bool UiStorageAllowed_(H3CombatManager* mgr)
{
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
    return storageAllowed;
}

// 状态灯（圆）绘制到合成图；(centerX, centerY) 为合成图内灯中心。
// 抽取供第二套折叠界面在收起态复用同一外观。
static void UiDrawStatusLamp_(H3LoadedPcx16* c, int centerX, int centerY, bool storageAllowed)
{
    const hbs_ui::LampColor lamp = hbs_ui::StatusLampColor(storageAllowed);
    const int radius = hbs_ui::StatusLampRadius;
    for (int dy = -radius - 1; dy <= radius + 1; ++dy) {
        const int half = hbs_ui::LampHalfWidth(dy, radius + 1);
        c->FillRectangle(centerX - half, centerY + dy, 2 * half + 1, 1, 30, 30, 30);
    }
    for (int dy = -radius; dy <= radius; ++dy) {
        const int half = hbs_ui::LampHalfWidth(dy, radius);
        c->FillRectangle(centerX - half, centerY + dy, 2 * half + 1, 1,
            lamp.r, lamp.g, lamp.b);
    }
    c->FillRectangle(centerX - 3, centerY - 3, 3, 2, 220, 245, 225);
}


// 列表数据扫描在服务层 StoreReloadList_；这里清输入管道并重置界面滚动、悬停与手势。
static bool UiReloadEntries_(const H3CombatManager* mgr)
{
    ClearBattleInputs_();
    g_ui.scroll = {};
    g_ui.hoverRow = -1;
    g_ui.autoScroll = {};
    g_ui.autoHoverRow = -1;
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

// 界面实现必须自己解码 [Ui] BarX/BarY；服务层只负责 [Hotkeys] SaveKey。
// 2026-10-14：两套界面的位置都由界面自己决定（战场外固定左上角、战场内由战斗
// 对话框反推），BarX/BarY 自 2026-10-05 起就不再影响绘制，这两个函数保留仅作
// 历史键值的读写工具，不再被任何绘制路径调用。
static void UiLoadBarPosition_()
{
    g_ui.x = IniReadIntUtf8(g_user_ini_path, "Ui", "BarX", kUiDefaultX);
    g_ui.y = IniReadIntUtf8(g_user_ini_path, "Ui", "BarY", kUiDefaultY);
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

// 单张三行底图，沿用 RGB 三平面 PCX 解码与游戏位深转换路径。
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

// HB_bg.pcx: centered HA_bg texture with baked gold border, one 480x72 image.
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

// ---- 面板下方战场像素的自存快照（save-under，仅折叠界面使用） ----
// 2026-10-11 04:3x：收起/收缩时不能从 screenPcx16 拷回战场——screenPcx16
// 是 H3WindowManager 的绘制缓冲，只在**战场外**区域是不含插件绘制的干净源
// （照抄 H3BattleValueInfo 的战场外方案，用户在战内实测条纹复现）。战场内
// 的呈现走 DD backbuffer，插件把自己画进 backbuffer 后 H3Redraw 只刷新该
// 矩形；backbuffer 的旧像素不会被覆盖，这才是残留来源。
// 决定方案：画面板之前先 Lock backbuffer 把将要覆盖的矩形读回自持缓冲，
// 收起/收缩时原样写回同一矩形。与 screenPcx16 语义完全无关，闭合回路。
// 读/写严格同源同矩形，不涉及任何颜色转换（16bpp 按 16bpp 整体拷贝）。
// 注意：save-under 只捕获内容切换帧（rectChanged）——capture 位于 blt
// 之前，连续展开帧的 backbuffer 已被上一帧的面板像素填满，拍到的是面板
// 自己，写回即一整块彩条（2026-10-11 05:2x 玩家实测）。
// 2026-10-14 起两份快照共用同一实现：面板覆盖区（折叠界面展开时遮挡的战场）
// 与状态灯区（切到战场外固定条时擦掉灯，见 BattleFoldBar 的 DrawFoldLamp_）。
struct UiUnderSlot_
{
    BYTE* buf = nullptr;
    int w = 0;
    int h = 0;
    int x = 0;
    int y = 0;
    int bpp = 0;     // 记录快照时的位深，位深变化（HD 切换）直接作废
    int pitch = 0;
    bool Valid() const { return buf && w > 0 && h > 0; }
};
static UiUnderSlot_ g_underPanel;
static UiUnderSlot_ g_underLamp;

// 释放快照（换场 / 位深变化 / 已写回后）。
static void UiUnderReleaseSlot_(UiUnderSlot_& slot)
{
    if (slot.buf) free(slot.buf);
    slot = UiUnderSlot_();
}

// 兼容既有调用名：以下三个作用于面板槽。
static void UiUnderRelease_() { UiUnderReleaseSlot_(g_underPanel); }

// 供 BattleFoldBar 收起分支查询是否存在可用快照（同 TU 后置定义可见）。
static bool UiUnderBufValid_() { return g_underPanel.Valid(); }

// 作废快照（不写回）。读档会 kRefreshField 重绘战场，backbuffer 已换成
// 读档后的场景；旧快照是读档前的战场像素，写回新战场就是彩条
// （2026-10-11 05:2x 玩家实测）。读档成功后调用，下一次展开重新捕获。
// 同时作废 uiTail 残影跟踪：读档不改变面板位置，rectChanged 不会为真，
// 不置 -1 的话下一帧仍用旧快照。
static void UiUnderInvalidate_()
{
    UiUnderRelease_();
    uiTailX = -1;
}

// 记录 backbuffer 中矩形 (x,y,w,h) 的像素到指定槽（该槽旧内容先释放）。
static bool UiUnderCaptureSlot_(UiUnderSlot_& slot, int x, int y, int w, int h)
{
    UiUnderReleaseSlot_(slot);
    if (w <= 0 || h <= 0) return false;
    LPDIRECTDRAWSURFACE bb = UiDDBackBuffer_();
    if (!bb) return false;
    bool captured = false;
    __try {
        DDSURFACEDESC desc;
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        if (FAILED(bb->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, nullptr))) return false;
        __try {
            if (!desc.lpSurface) return false;
            const int bpp = UiBackBufferBpp_(bb);
            const int px = bpp == 32 ? 4 : 2;
            const int pitch = (int)desc.lPitch;
            if (pitch <= 0) return false;
            // 矩形必须完整落在 backbuffer 内，否则来源不完整、写回会缺边。
            if (x < 0 || y < 0 || x + w > (int)desc.dwWidth || y + h > (int)desc.dwHeight) return false;
            BYTE* buf = (BYTE*)malloc((size_t)w * h * px);
            if (!buf) return false;
            const BYTE* base = (const BYTE*)desc.lpSurface + (size_t)y * pitch + (size_t)x * px;
            for (int row = 0; row < h; ++row)
                memcpy(buf + (size_t)row * w * px, base + (size_t)row * pitch, (size_t)w * px);
            slot.buf = buf;
            slot.w = w; slot.h = h; slot.x = x; slot.y = y;
            slot.bpp = bpp; slot.pitch = pitch;
            captured = true;
        } __finally {
            bb->Unlock(nullptr);
        }
    } __except (GuardCrashFilter_(GUARD_COPY, GetExceptionInformation())) {
        captured = false;
    }
    if (!captured) UiUnderReleaseSlot_(slot);
    return captured;
}

// 把快照写回 backbuffer 的同一矩形并刷新。快照与当前 backbuffer 位深不一致
// （切换 HD/窗口模式）时放弃，保留原状让下一帧清理。
static bool UiUnderRestoreSlot_(UiUnderSlot_& slot)
{
    if (!slot.Valid()) return false;
    LPDIRECTDRAWSURFACE bb = UiDDBackBuffer_();
    if (!bb) return false;
    bool restored = false;
    __try {
        DDSURFACEDESC desc;
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        if (FAILED(bb->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, nullptr))) return false;
        __try {
            if (!desc.lpSurface) return false;
            const int bpp = UiBackBufferBpp_(bb);
            const int px = bpp == 32 ? 4 : 2;
            const int pitch = (int)desc.lPitch;
            if (pitch <= 0 || bpp != slot.bpp) return false;
            const int x = slot.x, y = slot.y, w = slot.w, h = slot.h;
            if (x < 0 || y < 0 || x + w > (int)desc.dwWidth || y + h > (int)desc.dwHeight) return false;
            BYTE* base = (BYTE*)desc.lpSurface + (size_t)y * pitch + (size_t)x * px;
            for (int row = 0; row < h; ++row)
                memcpy(base + (size_t)row * pitch, slot.buf + (size_t)row * w * px, (size_t)w * px);
            restored = true;
        } __finally {
            bb->Unlock(nullptr);
        }
    } __except (GuardCrashFilter_(GUARD_COPY, GetExceptionInformation())) {
        restored = false;
    }
    if (restored) {
        H3WindowManager* wnd = H3WindowManager::Get();
        if (wnd) wnd->H3Redraw(slot.x, slot.y, slot.w, slot.h);
        UiUnderReleaseSlot_(slot);
    }
    return restored;
}

static bool UiUnderCapture_(int x, int y, int w, int h)
{
    return UiUnderCaptureSlot_(g_underPanel, x, y, w, h);
}

static bool UiUnderRestore_() { return UiUnderRestoreSlot_(g_underPanel); }

// ---- 界面选择与切换（2026-10-14 用户裁定：两套界面同 DLL，配置/复选框选择） ----
// 战场外固定条：左上角 (8,8)，不读 BarX/BarY（位置由界面决定，2026-10-05 定稿）。
static void UiResetOutsideAnchor_()
{
    g_uiBarAnchorX = kUiOutsideBarX;
    g_uiBarAnchorY = kUiOutsideBarY;
    g_ui.x = kUiOutsideBarX;
    g_ui.y = kUiOutsideBarY;
}

// 只切换选择本身（启动时读配置后调用；运行期切换走 UiApplyOutsideBar_）。
static void UiSelectInterface_(bool outside)
{
    g_uiOutsideBar = outside;
    g_uiFoldLayout = !outside;
    if (outside) UiResetOutsideAnchor_();
}

// 离开战场外固定条：整块旧矩形按该界面自己的恢复源（screenPcx16，战场外是
// 干净源，见 UiClampBarToBattleDlg_ 注释）原样写回，避免切到折叠界面后左上角
// 留下一条残影。
static void UiNativeTailErase_()
{
    H3WindowManager* wnd = H3WindowManager::Get();
    if (!wnd || !wnd->screenPcx16 || uiTailX < 0 || uiTailY < 0 || uiTailH <= 0) return;
    const int w = uiTailListWidth > kUiBarWidth ? uiTailListWidth : kUiBarWidth;
    if (UiBltPcx16Region_(wnd->screenPcx16, uiTailX, uiTailY, w, uiTailH, uiTailX, uiTailY))
        wnd->H3Redraw(uiTailX, uiTailY, w, uiTailH);
    LogDebug("[Ui] native bar erased x=%d y=%d w=%d h=%d", uiTailX, uiTailY, w, uiTailH);
}

// 运行期切换界面：先把旧界面的像素清干净（两套界面的恢复源不同，必须各清各的），
// 再切换选择并作废两套残影跟踪，使新界面下一帧按新矩形重新呈现。
static void UiApplyOutsideBar_(bool outside, const char* reason)
{
    if (outside == g_uiOutsideBar) return;
    if (g_uiOutsideBar) UiNativeTailErase_();   // 旧界面=战场外固定条
    else UiFoldLeaveUi_();                      // 旧界面=战场内折叠灯（面板 + 灯）
    UiUnderRelease_();                          // 面板快照随界面切换作废
    uiTailX = -1;
    g_ui.listGesture = {};
    g_ui.scroll.dragging = false;
    g_ui.autoScroll.dragging = false;
    g_ui.listRightHeld = false;
    g_ui.hoverRow = -1;
    g_ui.autoHoverRow = -1;
    g_ui.logLevelHover = -1;
    g_ui.outsideHover = -1;
    ClearBattleInputs_();
    UiSelectInterface_(outside);
    LogInfo("[Ui] 界面已切换为%s（%s；配置键 [Ui] OutsideBattlefield=%d）",
        outside ? "战场外左上角固定条" : "战场内右上角折叠灯",
        reason ? reason : "unknown", outside ? 1 : 0);
}

static H3LoadedPcx16* g_barComposite = nullptr;

// ---- 第一行布局：日志等级项 +「战场外」复选框（2026-10-14 紧凑化） ----
// 项宽 = 字体实测文本宽 + 左右内边距，随字体高度变化重算（同字体只算一次）。
// 绘制与命中判定共用 g_logRow，点击热区与画面严格一致。
static int UiMeasureRowText_(H3Font* font, const char* utf8)
{
    if (!font || !utf8) return 0;
    char gbk[48] = {};
    UiToGbk_(utf8, gbk, sizeof(gbk));
    const int width = font->GetMaxLineWidth(gbk);
    return width > 0 ? width : 0;
}

static void UiUpdateLogRow_(H3Font* font)
{
    if (!font || font->height == g_logRowFontHeight) return;
    const int pad = 2 * hbs_ui::LogRowPadX;
    int levelsWidth = kUiLogRowMinLevelWidth;
    for (int i = 0; i < hbs_ui::LogRowLevelCount; ++i) {
        // 标记槽恒为 1 字符（选中 "*"、未选中空格），按选中形态量宽即所有项的上界。
        char utf8[24] = {};
        _snprintf(utf8, sizeof(utf8), "*%s", LogLevelDisplayName_(i));
        const int width = UiMeasureRowText_(font, utf8) + pad;
        if (width > levelsWidth) levelsWidth = width;
    }
    int outsideWidth = 0;
    for (int i = 0; i < hbs_ui::LogRowOutsideTagCount; ++i) {
        const int width = UiMeasureRowText_(font, hbs_ui::LogRowOutsideTag(i)) + pad;
        if (width > outsideWidth) outsideWidth = width;
    }
    g_logRow = hbs_ui::PlaceLogRow(kUiBarWidth, levelsWidth, outsideWidth);
    g_logRowFontHeight = font->height;
    LogDebug("[Ui] 第一行布局 font=%d levelW=%d outside=x%d w%d packX=%d spare=%d",
        font->height, levelsWidth, g_logRow.outside.x, g_logRow.outside.width,
        g_logRow.packX, g_logRow.spare);
    if (g_logRow.spare < 0)
        LogWarn("[Ui] 第一行超出条宽：levelW=%d outsideW=%d tail=%d packX=%d",
            levelsWidth, outsideWidth, g_logRow.tailX, g_logRow.packX);
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
        UiUpdateLogRow_(font);   // 第一行项宽随字体实测刷新（绘制与命中共用）
        g_ui.x = g_uiBarAnchorX;
        g_ui.y = g_uiBarAnchorY;
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
        g_ui.autoScroll.Clamp(g_store.autoEntries.size());
        const int rows = UiListRows_(0);
        const int autoRows = UiListRows_(1);
        const int listWidth = UiListDrawWidth_(0);
        const int autoListWidth = UiListDrawWidth_(1);
        // 列表区总宽（手动列 + 间隙 + 自动列）：残影跟踪与清底用它。
        const int listsWidth = kUiAutoListX + autoListWidth;
        const H3POINT cursor = H3POINT::GetCursorPosition();
        g_ui.hoverRow = cursor.x >= x && cursor.x < x + kUiListWidth
            ? g_uiLayout.HitRow(cursor.y - y, rows) : -1;
        g_ui.autoHoverRow = cursor.x >= x + kUiAutoListX && cursor.x < x + kUiAutoListX + kUiListWidth
            ? g_uiLayout.HitRow(cursor.y - y, autoRows) : -1;
        // 列表高度随实际行数自适应（2026-10-05 用户实测纠正：固定满高会显示
        // 一堆空行背景板）；成品图行分隔线在每行底部，任意行数展开底边闭合。
        // 双列取较高一列（标题行恒在）。
        const int listRowsShown = rows > autoRows ? rows : autoRows;
        const int usedH = g_uiLayout.Height(listRowsShown);
        // 残影跟踪：位置/高度变化时，本帧末尾把上一帧矩形从 screenPcx16 拷回
        // backbuffer（HD 增量呈现不会自动覆盖旧区域，2026-10-05 拖动实测残影）。
        // 跟踪变量为 file scope（uiTail*），供第二套折叠界面收起时清场。
        if (uiTailMgr != mgr) {
            uiTailX = uiTailY = uiTailH = uiTailBlockH = uiTailListWidth = -1;
            uiTailMgr = mgr;
        }
        // 两行悬浮框矩形恒定，残影跟踪只跟随存档列表行数。
        const int totalH = usedH;
        const bool rectChanged = uiTailX != x || uiTailY != y || uiTailH != totalH
            || uiTailBlockH != g_uiLayout.ListTop() || uiTailListWidth != listsWidth
            || uiTailColumnRows[0] != rows || uiTailColumnRows[1] != autoRows
            || uiTailColumnWidths[0] != listWidth || uiTailColumnWidths[1] != autoListWidth;
        // 每帧清底；列表区只清双列总宽（列表窄于悬浮条）
        c->FillRectangle(0, 0, kUiBarWidth, UiBandHeight_(), 0, 0, 0);
        c->FillRectangle(0, UiBandHeight_(), kUiBarWidth, UiBandHeight_(), 0, 0, 0);
        c->FillRectangle(0, g_uiLayout.ListTop(), listsWidth,
            kUiListVisibleRows * UiRowHeight_(), 0, 0, 0);
        H3LoadedPcx16* bg = UiLoadBarBg_();
        const bool bgOk = bg && bg->buffer
            && bg->width == kUiBarWidth && bg->height == kUiBgHeight;
        if (bgOk) {
            for (int py = 0; py < g_uiLayout.TitleTop(); ++py)
                UiCopyBgRegion_(c, bg, 0, hbs_ui::TextureY(py, UiBandHeight_()),
                    kUiBarWidth, 1, py);
            // 标题读取独立第三带；内纹理适配行高，金框与分隔线保持原厚度。
            for (int py = 0; py < UiRowHeight_(); ++py)
                UiCopyBgRegion_(c, bg, 0,
                    hbs_ui::TextureBandY(py, UiRowHeight_(), 2),
                    kUiBarWidth, 1, g_uiLayout.TitleTop() + py);
            // 下拉列表底色 = 背景图主色（2026-10-06 用户裁定：选项底色接近背景图，
            // 不要黑蓝对比）。稀疏采样均值做主色，三行图仍仅千余点。
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
        // 标题行（docs/02 §4，2026-10-11 21:3x 用户裁定）：左「手动存档」
        // 右「自动存档」，各对齐其下方列表；行高 = 列表行高。
        char titleManual[16] = {};
        UiToGbk_("手动存档", titleManual, sizeof(titleManual));
        font->TextDraw(c, titleManual, 6, g_uiLayout.TitleTop(), kUiListWidth - 12,
            UiRowHeight_(), eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
        char titleAuto[16] = {};
        UiToGbk_("自动存档", titleAuto, sizeof(titleAuto));
        font->TextDraw(c, titleAuto, kUiAutoListX + 6, g_uiLayout.TitleTop(),
            kUiListWidth - 12, UiRowHeight_(), eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
        if (rows > 0) {
            c->FillRectangle(0, g_uiLayout.ListTop(), listWidth, rows * UiRowHeight_(),
                (BYTE)g_ui.listR, (BYTE)g_ui.listG, (BYTE)g_ui.listB);
            c->DrawFrame(0, g_uiLayout.ListTop(), listWidth,
                rows * UiRowHeight_(), 160, 140, 70);
        }
        if (autoRows > 0) {
            c->FillRectangle(kUiAutoListX, g_uiLayout.ListTop(), autoListWidth,
                autoRows * UiRowHeight_(), (BYTE)g_ui.listR, (BYTE)g_ui.listG, (BYTE)g_ui.listB);
            c->DrawFrame(kUiAutoListX, g_uiLayout.ListTop(), autoListWidth,
                autoRows * UiRowHeight_(), 160, 140, 70);
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
        const bool storageAllowed = UiStorageAllowed_(mgr);
        // 状态灯：旧界面画在合成图原位（第二行带内）；折叠版布局下灯移出
        // 面板外（见 g_uiFoldLayout 注释），这里不再往面板里画，由
        // BattleFoldBar 在面板之后单独画在原位。
        if (!g_uiFoldLayout)
            UiDrawStatusLamp_(c, hbs_ui::StatusLampX,
                hbs_ui::StatusLampY(g_uiLayout), storageAllowed);
        char key[16] = {};
        char keyUtf8[8] = {};
        _snprintf(keyUtf8, sizeof(keyUtf8), "键:%c", g_store.saveKey);
        UiToGbk_(keyUtf8, key, sizeof(key));
        font->TextDraw(c, key, hbs_ui::HotkeyX, UiBandHeight_(), 52, UiBandHeight_(),
            eTextColor::WHITE, eTextAlignment::MIDDLE_CENTER);
        c->DrawFrame(hbs_ui::HotkeyX, UiBandHeight_() + 2, 54, UiBandHeight_() - 4, 220, 200, 110);
        // 紧凑横排五级（不展开收起）：标记槽 1 字符 + 高亮底/亮绿字标状态，
        // 项宽按字体实测，省下的空间给右侧「战场外」复选框（2026-10-14 用户要求）。
        const int levelNow = (g_log_level >= LOG_TRACE && g_log_level <= LOG_ERROR)
            ? g_log_level : LOG_DEBUG;
        for (int i = 0; i < hbs_ui::LogRowLevelCount; ++i) {
            const int cellX = g_logRow.level[i].x;
            const int cellW = g_logRow.level[i].width;
            if (i == g_ui.logLevelHover || i == levelNow) {
                const int fillR = i == g_ui.logLevelHover ? 90 : 70;
                const int fillG = i == g_ui.logLevelHover ? 70 : 55;
                const int fillB = i == g_ui.logLevelHover ? 20 : 25;
                c->FillRectangle(cellX + 1, 3, cellW - 2,
                    UiBandHeight_() - 6, (BYTE)fillR, (BYTE)fillG, (BYTE)fillB);
            }
            char itemUtf8[24] = {};
            _snprintf(itemUtf8, sizeof(itemUtf8), "%c%s",
                i == levelNow ? '*' : ' ', LogLevelDisplayName_(i));
            char itemGbk[32] = {};
            UiToGbk_(itemUtf8, itemGbk, sizeof(itemGbk));
            font->TextDraw(c, itemGbk, cellX + hbs_ui::LogRowPadX, 0,
                cellW - 2 * hbs_ui::LogRowPadX, UiBandHeight_(),
                i == levelNow ? eTextColor::LIGHT_GREEN : eTextColor::WHITE,
                eTextAlignment::MIDDLE_CENTER);
        }
        {
            // 「战场外」复选框：勾选=左上角固定条界面（旧界面），默认不勾=
            // 当前这套战场内折叠灯界面；点击即写 user.ini 并即时切换。
            const int cellX = g_logRow.outside.x;
            const int cellW = g_logRow.outside.width;
            if (g_ui.outsideHover >= 0) {
                const int fillR = g_uiOutsideBar ? 70 : 90;
                const int fillG = g_uiOutsideBar ? 55 : 70;
                const int fillB = g_uiOutsideBar ? 25 : 20;
                c->FillRectangle(cellX + 1, 3, cellW - 2,
                    UiBandHeight_() - 6, (BYTE)fillR, (BYTE)fillG, (BYTE)fillB);
            }
            char outsideGbk[48] = {};
            UiToGbk_(hbs_ui::LogRowOutsideTag(g_uiOutsideBar ? 1 : 0), outsideGbk,
                sizeof(outsideGbk));
            font->TextDraw(c, outsideGbk, cellX + hbs_ui::LogRowPadX, 0,
                cellW - 2 * hbs_ui::LogRowPadX, UiBandHeight_(),
                g_uiOutsideBar ? eTextColor::LIGHT_GREEN : eTextColor::WHITE,
                eTextAlignment::MIDDLE_CENTER);
            c->DrawFrame(cellX + 1, 2, cellW - 2, UiBandHeight_() - 4, 160, 140, 70);
        }
        char packLabel[32] = {};
        UiToGbk_("打包日志", packLabel, sizeof(packLabel));
        font->TextDraw(c, packLabel, g_logRow.packX + 2, 0, g_logRow.packWidth - 4,
            UiBandHeight_(), eTextColor::WHITE, eTextAlignment::MIDDLE_CENTER);
        c->DrawFrame(g_logRow.packX, 3, g_logRow.packWidth, UiBandHeight_() - 6,
            220, 200, 110);
        if (rows > 0) {
            const int listY = g_uiLayout.ListTop();
            for (int row = 0; row < rows; ++row) {
                if (row == g_ui.hoverRow) {
                    // 2026-10-10 用户反馈悬停高亮不醒目：提亮底色并加金色描边
                    //（与打包按钮/滚动条边框同色系），行内白字对比仍足。
                    c->FillRectangle(2, listY + row * UiRowHeight_(),
                        kUiListWidth - 4, UiRowHeight_(), 128, 102, 36);
                    c->DrawFrame(2, listY + row * UiRowHeight_(),
                        kUiListWidth - 4, UiRowHeight_(), 220, 200, 110);
                }
                char stamp[32] = {};
                StoreFormatFileStem_(g_store.entries[g_ui.scroll.first + row], stamp, sizeof(stamp));
                font->TextDraw(c, stamp, 6, listY + row * UiRowHeight_(),
                    kUiListWidth - 12, UiRowHeight_(), eTextColor::WHITE, eTextAlignment::MIDDLE_LEFT);
            }
        }
        if (autoRows > 0) {
            const int listY = g_uiLayout.ListTop();
            for (int row = 0; row < autoRows; ++row) {
                if (row == g_ui.autoHoverRow) {
                    c->FillRectangle(kUiAutoListX + 2, listY + row * UiRowHeight_(),
                        kUiListWidth - 4, UiRowHeight_(), 128, 102, 36);
                    c->DrawFrame(kUiAutoListX + 2, listY + row * UiRowHeight_(),
                        kUiListWidth - 4, UiRowHeight_(), 220, 200, 110);
                }
                char stamp[40] = {};
                StoreFormatFileStem_(g_store.autoEntries[g_ui.autoScroll.first + row],
                    stamp, sizeof(stamp));
                font->TextDraw(c, stamp, kUiAutoListX + 6, listY + row * UiRowHeight_(),
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
        if (g_store.autoEntries.size() > kUiListVisibleRows) {
            const auto bar = hbs_ui::ForList(layout, g_store.autoEntries.size(), g_ui.autoScroll.first);
            const int sx = kUiAutoListX + kUiListWidth;
            const int sy = layout.ListTop();
            const int height = autoRows * UiRowHeight_();
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
        // 残影清理分两套源，按界面选（2026-10-11 04:3x 起）：
        // 折叠式用 save-under 自存快照（见 UiUnderCapture_ 的机制注释）——
        // 战场内不能拿 screenPcx16 当恢复源；旧界面保持 screenPcx16 老路径。
        // 收缩/换位/收起三条情况：先写回旧快照（覆盖将要被抹掉的全部旧像素），
        // 再按本次矩形重新捕获，供下一次收起使用。
        if (g_uiFoldLayout) {
            // save-under 只在"内容切换帧"捕获：矩形变化（含首次展开）
            // 意味着上一帧此处不是本矩形内容，backbuffer 该区域仍是干净场景。
            // 绝不能每帧捕获——capture 位于 blt 之前，连续展开时 backbuffer
            // 已被上一帧的面板像素填充，拍到的是面板自己，写回就是一整块
            // 彩条（2026-10-11 05:2x 玩家实测复现）。
            if (rectChanged) {
                UiUnderRestore_();
                if (!UiUnderCapture_(x, y, kUiBarWidth, totalH))
                    LogWarn("[Draw] under-capture failed x=%d y=%d w=%d h=%d bpp=%d",
                        x, y, kUiBarWidth, totalH, UiBackBufferBpp_(UiDDBackBuffer_()));
            }
        }
        else {
            // 双列各自缩高/隐藏滚动条也要恢复旧矩形；另一列保持20行时，
            // 面板总高度不变，不能只靠 max(rows) 的尾区清理。
            if (rectChanged && uiTailX >= 0 && wnd->screenPcx16) {
                for (int which = 0; which < 2; ++which) {
                    const int oldH = uiTailColumnRows[which] * uiTailRowHeight;
                    const int oldW = uiTailColumnWidths[which];
                    const int oldX = uiTailX + UiListX_(which);
                    const int oldY = uiTailY + uiTailBlockH;
                    if (oldH > 0 && oldW > 0 && UiBltPcx16Region_(wnd->screenPcx16,
                        oldX, oldY, oldW, oldH, oldX, oldY))
                        wnd->H3Redraw(oldX, oldY, oldW, oldH);
                }
            }
            if (rectChanged && uiTailX >= 0 && uiTailY != y && wnd->screenPcx16) {
                if (UiBltPcx16Region_(wnd->screenPcx16, uiTailX, uiTailY, kUiBarWidth,
                    uiTailBlockH, uiTailX, uiTailY)) wnd->H3Redraw(uiTailX, uiTailY, kUiBarWidth, uiTailBlockH);
                if (uiTailH > uiTailBlockH && UiBltPcx16Region_(wnd->screenPcx16,
                    uiTailX, uiTailY + uiTailBlockH, uiTailListWidth, uiTailH - uiTailBlockH,
                    uiTailX, uiTailY + uiTailBlockH))
                    wnd->H3Redraw(uiTailX, uiTailY + uiTailBlockH, uiTailListWidth, uiTailH - uiTailBlockH);
            }
            if (rectChanged && uiTailX >= 0 && uiTailY == y && uiTailBlockH > g_uiLayout.ListTop() && wnd->screenPcx16) {
                const int tailY = uiTailY + g_uiLayout.ListTop();
                const int tailH = uiTailBlockH - g_uiLayout.ListTop();
                if (UiBltPcx16Region_(wnd->screenPcx16, uiTailX, tailY, kUiBarWidth,
                    tailH, uiTailX, tailY)) wnd->H3Redraw(uiTailX, tailY, kUiBarWidth, tailH);
            }
            if (rectChanged && uiTailX >= 0 && uiTailY == y && uiTailH > totalH && wnd->screenPcx16) {
                const int tailY = uiTailY + totalH;
                if (UiBltPcx16Region_(wnd->screenPcx16, uiTailX, tailY, uiTailListWidth,
                    uiTailH - totalH, uiTailX, tailY)) wnd->H3Redraw(uiTailX, tailY, uiTailListWidth, uiTailH - totalH);
            }
            if (rectChanged && uiTailX >= 0 && uiTailY == y && uiTailListWidth > listsWidth && wnd->screenPcx16) {
                const int stripX = uiTailX + listsWidth;
                const int stripY = uiTailY + g_uiLayout.ListTop();
                const int stripH = totalH - g_uiLayout.ListTop();
                if (stripH > 0 && UiBltPcx16Region_(wnd->screenPcx16, stripX, stripY,
                    uiTailListWidth - listsWidth, stripH, stripX, stripY))
                    wnd->H3Redraw(stripX, stripY, uiTailListWidth - listsWidth, stripH);
            }
        } // !g_uiFoldLayout：旧界面继续用 screenPcx16 恢复
        // 悬浮框整块呈现（含标题行），两个列表各覆盖自身窄矩形。
        bltOk = UiBltPcx16Region_(c, 0, 0, kUiBarWidth, g_uiLayout.ListTop(), x, y);
        if (rows > 0)
            bltOk = UiBltPcx16Region_(c, 0, g_uiLayout.ListTop(), listWidth,
                rows * UiRowHeight_(), x, y + g_uiLayout.ListTop()) && bltOk;
        if (autoRows > 0)
            bltOk = UiBltPcx16Region_(c, kUiAutoListX, g_uiLayout.ListTop(), autoListWidth,
                autoRows * UiRowHeight_(), x + kUiAutoListX, y + g_uiLayout.ListTop()) && bltOk;
        wnd->H3Redraw(x, y, kUiBarWidth, g_uiLayout.ListTop());
        if (rows > 0) wnd->H3Redraw(x, y + g_uiLayout.ListTop(),
            listWidth, rows * UiRowHeight_());
        if (autoRows > 0) wnd->H3Redraw(x + kUiAutoListX, y + g_uiLayout.ListTop(),
            autoListWidth, autoRows * UiRowHeight_());
        redrawing = false;
        uiTailX = x;
        uiTailY = y;
        uiTailH = totalH;
        uiTailBlockH = g_uiLayout.ListTop();
        uiTailListWidth = listsWidth;
        uiTailColumnRows[0] = rows;
        uiTailColumnRows[1] = autoRows;
        uiTailColumnWidths[0] = listWidth;
        uiTailColumnWidths[1] = autoListWidth;
        uiTailRowHeight = UiRowHeight_();
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
        && py >= UiOriginY_() && py < UiOriginY_() + g_uiLayout.ListTop();
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

static ptrdiff_t UiHitRow_(int which, int px, int py)
{
    const int lx = g_ui.x + UiListX_(which);
    if (px < lx || px >= lx + kUiListWidth) return -1;
    UiScrollOf_(which).Clamp(UiEntriesOf_(which).size());
    return UiScrollOf_(which).Hit(g_uiLayout, py - UiOriginY_(), UiEntriesOf_(which).size());
}

static int UiHitLogLevelItem_(int px, int py)
{
    return g_logRow.HitLevel(px - g_ui.x, py - UiOriginY_(), UiBandHeight_());
}

// 「战场外」复选框热区（与绘制共用 g_logRow，画面与热区严格一致）。
static bool UiHitOutsideCheck_(int px, int py)
{
    return g_logRow.HitOutside(px - g_ui.x, py - UiOriginY_(), UiBandHeight_());
}

static bool UiHitLogPack_(int px, int py)
{
    return px >= g_ui.x + g_logRow.packX && px < g_ui.x + g_logRow.packX + g_logRow.packWidth
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

// 「战场外」复选框点击：勾选=左上角固定条界面，不勾=战场内折叠灯界面。
// 写盘失败时保持现状不动（与日志等级同一规矩，2026-10-06 用户裁定），
// 避免"界面已变但配置没写进去"的下次启动不一致。
static void UiToggleOutsideBar_()
{
    const bool next = !g_uiOutsideBar;
    if (!IniWriteKeyUtf8(g_user_ini_path, "Ui", "OutsideBattlefield", next ? "1" : "0")) {
        UiMarkNotice_("界面配置保存失败，界面未改变");
        LogError("[Config] 保存界面选择失败：target=%d keep=%d",
            next ? 1 : 0, g_uiOutsideBar ? 1 : 0);
        return;
    }
    LogInfo("[Config] 界面选择已写入用户配置：[Ui] OutsideBattlefield=%d", next ? 1 : 0);
    UiApplyOutsideBar_(next, "面板复选框");
    UiMarkNotice_(next ? "已切到战场外界面" : "已切回战场内界面");
}

static void UiCancelRebind_(const char* reason)
{
    if (!g_ui.awaitingRebind) return;
    g_ui.awaitingRebind = false;
    LogInfo("改键状态已取消：%s", reason ? reason : "unknown");
}

static void UiConfirmAndRestore_(const StoreEntry& entry)
{
    // 守卫在服务 StoreRequestRestore_ 内同样核验；这里先行复现以保持
    // "守卫失败不取消改键"的原语义。
    if (g_restoreBusy || g_restoreRequest.pending
        || !CombatStorageWindow_(H3CombatManager::Get())) return;
    UiCancelRebind_("读档请求");
    if (!StoreRequestRestore_(entry)) return;
    ClearBattleInputs_();
}

// 失败呈现（模态弹窗；日志与诊断收尾已在服务 StoreRestoreFail_ 完成）。
static void UiShowRestoreFailure_(const std::string& raw)
{
    const std::string reason = StorePlayerTextZh_(raw);
    const std::string message = std::string("读档未完成：") + reason;
    char notice[512] = {};
    UiToGbk_(message.c_str(), notice, sizeof(notice));
    H3Messagebox::Show(notice);
}

// 确认流程与结果呈现：模态交互属界面；校验与写入在服务 StoreExecuteRestore_。
static void UiRunRestoreFlow_(H3CombatManager* mgr, const StoreEntry& entry,
    unsigned generation, const std::string& expectedKey)
{
    if (g_restoreFatal) { g_restoreRequest.pending = false; return; }
    g_restoreBusy = true;
    struct BusyReset { ~BusyReset() { ClearBattleInputs_(); g_restoreBusy = false; } } busyReset;
    DiagBegin_("load", "outer-message", mgr);
    char stamp[32] = {};
    StoreFormatFileStem_(entry, stamp, sizeof(stamp));
    char text[192] = {}, utf8[192] = {};
    _snprintf(utf8, sizeof(utf8), "读回 %s 这一档？当前未保存的进度会丢掉。", stamp);
    UiToGbk_(utf8, text, sizeof(text));
    DiagStage_("load.confirm");
    const bool confirmed = H3Messagebox::Choice(text);
    if (g_restoreFatal) return;
    if (!confirmed) { DiagEnd_("cancelled", "玩家取消读档确认"); return; }
    std::string rawReason;
    int turn = 0;
    const StoreRestoreResult_ outcome =
        StoreExecuteRestore_(mgr, entry, generation, expectedKey, &rawReason, &turn);
    if (outcome == StoreRestoreResult_::Failed) {
        UiShowRestoreFailure_(rawReason);
        return;
    }
    if (outcome == StoreRestoreResult_::Fatal) return;
    if (outcome == StoreRestoreResult_::Success) {
        UiCancelRebind_("读档完成");
        // Success notice (2026-10-07 用户裁定)：本插件悬浮框醒目色显示几秒，
        // 到时自动回常规状态行；模态弹窗只留给失败。
        char done[96] = {};
        _snprintf(done, sizeof(done), "已读档：第 %d 回合", turn);
        UiMarkNoticeHighlight_(done);
    }
    // No rendering after the final RNG commit in this handler.
}

// 独立的读档请求维护包装（2026-10-10 修复语义保持）：决策在服务；超时提示归界面。
static void UiMaintainRestore_(H3CombatManager* mgr)
{
    if (StoreMaintainRestore_(mgr))
        UiMarkNotice_("读档等待超时，已取消；请重试");
}

// 返回 true 表示本次真的消费了读档请求（读档流程已跑过一遍，无论成败）。
// 调用方据此判断是否需要"读档后界面复位"——只有真的读过档才需要；
// 普通消息也会走到这里（Entry 每条符合条件的消息都调 ProcessRestore），
// 若不分青红皂白复位，界面会被消息流反复强制收起（2026-10-11 06:0x
// 回归：鼠标因此永远移不进展开区域，没机会读档）。
static bool UiProcessRestore_(H3CombatManager* mgr, int result)
{
    StoreEntry entry;
    unsigned generation = 0;
    std::string expectedKey;
    if (!StoreConsumeRestore_(mgr, result, &entry, &generation, &expectedKey)) return false;
    // 读档流程会 kRefreshField 整体重绘战场，backbuffer 换成读档后的场景。
    // save-under 快照必须作废——旧快照是读档前的战场像素，写回新战场就是
    // 彩条（2026-10-11 05:2x 玩家实测：读档后偶发一小块彩条）。
    // 无论读档成败都作废：失败会回滚，屏幕同样被动过。
    UiUnderInvalidate_();
    UiRunRestoreFlow_(mgr, entry, generation, expectedKey);
    return true;
}


static void UiHandleMouse_(H3Msg* msg)
{
    if (!msg) return;
    const H3POINT cursor = H3POINT::GetCursorPosition();
    const int px = cursor.x;
    const int py = cursor.y;
    if (msg->command == eMsgCommand::MOUSE_OVER) {
        for (int which = 0; which < 2; ++which) {
            const int lx = g_ui.x + UiListX_(which);
            UiHoverOf_(which) = px >= lx && px < lx + kUiListWidth
                ? g_uiLayout.HitRow(py - UiOriginY_(), UiListRows_(which)) : -1;
        }
        g_ui.logLevelHover = UiHitLogLevelItem_(px, py);
        g_ui.outsideHover = UiHitOutsideCheck_(px, py) ? 0 : -1;
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
    // 「战场外」复选框：切换界面（勾选=左上角固定条）。
    if (UiHitOutsideCheck_(gameX, gameY)) {
        LogDebug("点击战场外复选框：当前=%d", g_uiOutsideBar ? 1 : 0);
        UiToggleOutsideBar_();
        return;
    }
    if (UiPointInUiBlock_(gameX, gameY) && !UiPointInBar_(gameX, gameY)) {
        LogDebug("点击日志等级块空位（无动作）：(%d,%d)", gameX, gameY);
        return;
    }
    if (UiPointInBar_(gameX, gameY)) {
        if (gameX >= g_ui.x + hbs_ui::HotkeyX) {
            g_ui.awaitingRebind = true;
            StoreCancelSaveWait_("rebind entered");
            LogInfo("点击快捷键区域：(%d,%d)", gameX, gameY);
        } else {
            UiReloadEntries_(H3CombatManager::Get());
            LogDebug("点击存档列表区域：(%d,%d)", gameX, gameY);
        }
        return;
    }
    if (g_ui.pendingListRevision != g_store.listRevision) return;
    for (int which = 0; which < 2; ++which) {
        const ptrdiff_t row = UiHitRow_(which, gameX, gameY);
        if (row >= 0 && static_cast<size_t>(row) < UiEntriesOf_(which).size()) {
            UiConfirmAndRestore_(UiEntriesOf_(which)[row]);
            return;
        }
    }
    if (g_ui.awaitingRebind) g_ui.awaitingRebind = false;
}

// ---- 界面实现装配（docs/09 第2步立契约）：HdNativeUi 转发现有入口，行为零变化。 ----

// 换场清理：手势、悬停与改键交互状态；服务数据由 BattleReset_ 负责。
static void UiResetForBattle_()
{
    g_ui.scroll = {};
    g_ui.autoScroll = {};
    g_ui.listGesture = {};
    g_ui.listRightHeld = false;
    g_ui.hoverRow = -1;
    g_ui.autoHoverRow = -1;
    g_ui.logLevelHover = -1;
    g_ui.outsideHover = -1;
    UiCancelRebind_("battle reset");
    g_ui.rebindKey = 0;
    g_ui.rebindGuardUntil = 0;
    ClearBattleInputs_();
    // 快照只属于当前这一帧的战场像素；换场即失效，避免跨战斗写回旧战场。
    UiUnderRelease_();
}

// ---- 键盘/鼠标状态机（原 Entry 钩子体，第3步迁入；仅改调用通道，逻辑逐行等价） ----

// 系统键盘钩子：letter 翻译、改键/存档键日志与 pending 投递（闩锁占用）。
static bool UiOnSystemKey_(const UiKeyEvent_& e)
{
    const char letter = StoreKeyLetter_(e.vk, e.source == 0);
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
        g_ui.autoScroll.dragging = false;
        g_ui.listGesture = {};
        g_ui.listRightHeld = false;
        return false;
    }
    const bool move = e.kind == 0, leftDown = e.kind == 1, leftUp = e.kind == 2,
        rightDown = e.kind == 3, rightUp = e.kind == 4, wheel = e.kind == 5;
    const int which = UiListAt_(e.gameX, e.gameY);
    const bool hitList = which >= 0;
    const bool hitScroll = hitList && UiPointInScrollbar_(which, e.gameX, e.gameY);
    const bool swallow = hitList || UiPointInUiBlock_(e.gameX, e.gameY);
    if (g_ui.gestureListRevision != g_store.listRevision) g_ui.listGesture.CancelClick();
    if (move || leftDown || rightDown || wheel) UiCancelReleasedPointer_();
    const int draggingList = g_ui.scroll.dragging ? 0 : (g_ui.autoScroll.dragging ? 1 : -1);
    if (draggingList >= 0 && (move || leftUp)) {
        UiScrollbarDrag_(draggingList, e.gameY);
        if (leftUp) {
            UiScrollOf_(draggingList).dragging = false;
            g_ui.listGesture = {};
        }
        return true;
    }
    if (wheel) {
        if (draggingList >= 0) return true;
        if (!hitList) return false;
        UiScrollWheel_(which, (short)e.wheelDelta);
        g_ui.listGesture.CancelClick();
        InterlockedExchange(&g_pendingClickX, -1);
        InterlockedExchange(&g_pendingClickY, -1);
        LogDetail_("[List] wheel slot=%d first=%u count=%u", which,
            (unsigned)UiScrollOf_(which).first, (unsigned)UiEntriesOf_(which).size());
        return true;
    }
    if (move) return hitList;
    if (leftDown) {
        g_ui.listGesture = {};
        g_ui.scroll.dragging = false;
        g_ui.autoScroll.dragging = false;
        if (!swallow) { UiCancelRebind_("outside mouse click"); return false; }
        g_ui.gestureListRevision = g_store.listRevision;
        g_ui.listGesture.Begin(hitScroll);
        if (hitScroll) {
            UiScrollbarDown_(which, e.gameY);
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
        g_ui.pendingListRevision = g_store.listRevision;
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
    const char pressed = StoreKeyLetter_(msg->subtype, false);
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
    const bool onList = UiPointInAnyList_(msg->position.x, msg->position.y);
    if (onBar || onList || g_ui.scroll.dragging || g_ui.autoScroll.dragging) {
        UiHandleMouse_(msg);
        return true;
    }
    return false;
}

// 游戏键盘消息（原生后）：改键捕获与存档边沿触发。
static void UiOnGameKeyAfter_(H3CombatManager* mgr, const H3Msg* msg, int result, DWORD now, bool waitBlocked)
{
    const char pressed = StoreKeyLetter_(msg->subtype, false);
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
    g_ui.autoScroll.dragging = false;
    g_ui.listGesture = {};
    g_ui.listRightHeld = false;
}

class HdNativeUi final : public IBattleStoreUi {
public:
    void Initialize() override { UiResetOutsideAnchor_(); }  // 战场外固定条恒左上角
    void Draw(H3CombatManager* mgr) override { UiDrawBar_(mgr); }
    void PollHover() override
    {
        const H3POINT cursor = H3POINT::GetCursorPosition();
        g_ui.logLevelHover = UiHitLogLevelItem_(cursor.x, cursor.y);
        g_ui.outsideHover = UiHitOutsideCheck_(cursor.x, cursor.y) ? 0 : -1;
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
    void OnListReloaded() override { g_ui.hoverRow = g_ui.autoHoverRow = -1; }
    void OnFaultCleanup() override { UiOnFaultCleanup_(); }
    bool IsRebindWaiting() const override { return g_ui.awaitingRebind; }
    char RebindLatchKey() const override { return g_ui.rebindKey; }
};

// 战场外固定条界面实例：两套界面同在一个 DLL 里（2026-10-14 起不再用编译宏选），
// 具体用哪套由 UiPortDispatch_（BattleFoldBar.inc.cpp 末尾）按运行期配置分发。
static HdNativeUi g_hdNativeUi;
static IBattleStoreUi* UiNativePort_() { return &g_hdNativeUi; }
