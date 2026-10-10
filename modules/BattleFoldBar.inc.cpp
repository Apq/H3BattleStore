// ========== BattleFoldBar.inc.cpp ==========
// 第二套界面：折叠式悬浮框（默认实现；H3BS_UI_NATIVE=1 切回旧的两行常显条）。
// 用户裁定（2026-10-11）：锚点移到战场内右上角；收起时只显示状态灯，
// 光标进入灯上自动向左向下展开完整悬浮框（外观与旧界面一致：两行控制条 +
// 普通存档列表，非下拉框），光标移出展开区域自动收起。
// 位置忽略 [Ui] BarX/BarY（位置是界面概念，旧界面键值照旧生效不动）。
// 复用 BattleUi.inc.cpp 的绘制/交互/状态机；本文件只做折叠门控与灯绘制。
// 只经契约抽象被 Entry 调用；仅游戏主线程，SEH 规则同契约注释。

#include "BattleUiPort.hpp"

// 战场画面标准宽度（SoD 800x600 游戏坐标；HD 下 screenPcx16 同尺寸）与
// 锚点边距见 BattleInputPolicy.hpp 的 kFoldGameWidth/kFoldMargin（生产与
// 回归测试共用）。收起态灯热区额外外扩 padding，方便瞄准。
static const int kFoldLampPad = 4;
// 灯含 UiDrawStatusLamp_ 外框圈的总半径（圈半径 = radius+1）；小灯图按
// 2R+1 分配才不越界（2026-10-11 02:4x 越界踩堆实证）。
static const int kFoldLampRadius = hbs_ui::StatusLampRadius + 1;
// 面板右缘与灯左缘之间的间隙：灯保持原位、不被展开的面板压住
// （用户 03:1x 纠正："保持灯的位置不变,向左(及下)展开的区域,不应该与灯重叠"）。
static const int kFoldLampGap = 2;

static bool g_foldExpanded = false;
static H3LoadedPcx16* g_foldLamp = nullptr;
// 灯心（屏幕游戏坐标），由 FoldSyncAnchor_ 每帧从战斗对话框刷新；面板由
// 灯位反推，收起/展开切换时灯严格不跳位，且面板右缘不压灯。
static int g_foldLampX = 0;
static int g_foldLampY = 0;
// 展开态面板矩形（每帧由 UiDrawBar_ 的推导量刷新；输入钩子即时查询）。
static int g_foldPanelX = 0;
static int g_foldPanelY = 0;
static int g_foldPanelW = 0;
static int g_foldPanelH = 0;
// 呈现钩子重入闸（2026-10-11 02:18 闪退实证）：H3Redraw 会同步重入
// AfterBlt→Draw，无闸时"画→H3Redraw→Draw"无限递归栈溢出（0xC00000FD）。
// 收起路径与展开路径共用（UiDrawBar_ 自身另有 redrawing 闸）。
static bool g_foldDrawing = false;
// 展开态光标接管（2026-10-11 03:3x 用户要求）：展开区域内用游戏默认光标
// （箭头），不再显示战场光标/图标；离开展开区域时恢复接管前的光标。
// 只记"是否接管"与接管前的 type/frame，便于原样恢复。
static bool g_foldCursorTaken = false;
static int g_foldCursorType = 0;
static int g_foldCursorFrame = 0;

// 灯心：与收起态小图中心同一坐标，展开态也不变（灯独立于面板绘制）。
static int FoldLampCenterX_() { return g_foldLampX; }
static int FoldLampCenterY_() { return g_foldLampY; }

// 锚定战场矩形（2026-10-11 02:3x 用户实证纠正）：目标区域是战斗对话框
// （mgr->dlg）的内右上角，不是 800x600 逻辑屏的右上角——HD 分辨率下战场
// 对话框居中且尺寸随分辨率变化，按逻辑屏算的 (785,16) 会落到战场外顶部
// 中间。每帧从 dlg 刷新灯心与面板锚点（换场/分辨率变化自动跟随）；无战斗、
// 矩形放不下"面板+灯"时保持上次值。Initialize 的值只是进战斗前的占位。
static void FoldSyncAnchor_(H3CombatManager* mgr)
{
    const H3CombatDlg* dlg = mgr ? mgr->dlg : nullptr;
    if (!dlg) return;
    const int dx = dlg->GetX();
    const int dy = dlg->GetY();
    const int dw = dlg->GetWidth();
    const int dh = dlg->GetHeight();
    // 需要同时容纳面板（kUiBarWidth）与灯（2R+间隙+边距），否则不动。
    if (dw < kUiBarWidth + kFoldMargin + 2 * kFoldLampRadius + kFoldLampGap
        || dh < 4 * kFoldMargin) return;
    // 灯心贴战场内右上角：右缘内 kFoldMargin、上缘内 kFoldMargin。
    const int lampX = dx + dw - kFoldMargin - kFoldLampRadius;
    const int lampY = dy + kFoldMargin + kFoldLampRadius;
    // 面板：右缘与灯左缘之间留 kFoldLampGap，上缘与灯顶缘齐平，
    // 面板从灯向左、向下生长（OriginY 反推 anchorY 使面板顶缘落在 lampY-R）。
    const int panelRight = lampX - kFoldLampRadius - kFoldLampGap;
    const int panelTop = lampY - kFoldLampRadius;
    const int anchorX = panelRight - kUiBarWidth;
    int anchorY = panelTop + (g_uiLayout.bandHeight - 24) / 2;
    if (anchorY < 0) anchorY = 0;
    if (anchorX != g_uiBarAnchorX || anchorY != g_uiBarAnchorY
        || lampX != g_foldLampX || lampY != g_foldLampY) {
        g_uiBarAnchorX = anchorX;
        g_uiBarAnchorY = anchorY;
        g_foldLampX = lampX;
        g_foldLampY = lampY;
        LogDebug("[Ui] fold anchor dlg=%d,%d %dx%d anchor=%d,%d lamp=%d,%d gap=%d",
            dx, dy, dw, dh, anchorX, anchorY, lampX, lampY, kFoldLampGap);
    }
}

static void FoldHotZone_(int cursorX, int cursorY, bool* inLamp, bool* inPanel)
{
    // 灯热区：灯可见范围（含外框圈）再外扩 kFoldLampPad，便于瞄准；
    // 外扩量已覆盖面板右缘与灯左缘之间的间隙，两个热区不会出现死缝。
    const int lampW = 2 * kFoldLampRadius + 1 + 2 * kFoldLampPad;
    const int lampX = FoldLampCenterX_() - kFoldLampRadius - kFoldLampPad;
    const int lampY = FoldLampCenterY_() - kFoldLampRadius - kFoldLampPad;
    *inLamp = FoldBarRectContains_(lampX, lampY, lampW, lampW, cursorX, cursorY);
    *inPanel = FoldBarRectContains_(g_foldPanelX, g_foldPanelY,
        g_foldPanelW, g_foldPanelH, cursorX, cursorY);
}

// 光标接管/恢复（仅在游戏主线程、AfterBlt 同侧调用；与绘制同帧）。
static void FoldTakeDefaultCursor_()
{
    H3MouseManager* mm = H3MouseManager::Get();
    if (!mm) return;
    if (!g_foldCursorTaken) {
        g_foldCursorType = mm->GetType();
        g_foldCursorFrame = mm->GetFrame();
        g_foldCursorTaken = true;
    }
    // type 0 即 eCursor::DEFAULT（游戏默认光标）；frame 0 为默认帧。
    mm->SetCursor(0, 0);
}

static void FoldRestoreCursor_()
{
    if (!g_foldCursorTaken) return;
    H3MouseManager* mm = H3MouseManager::Get();
    if (mm) mm->SetCursor(g_foldCursorFrame, g_foldCursorType);
    g_foldCursorTaken = false;
}

// 强制收起（读档成功后调用）。读档会 kRefreshField 重绘整个战场，面板
// 遮挡区的 backbuffer 内容已换成读档后的场景，此时若继续展开，本插件自身
// 仍留在 backbuffer 上的面板像素会被拍进新快照，之后收起写回 = 彩条
// （2026-10-11 05:2x 玩家实测：读档后偶发一小块彩条）。收起后需重新
// hover 才展开；状态灯不受影响，仍每帧重画。
static void FoldForceCollapse_()
{
    if (!g_foldExpanded) return;
    g_foldExpanded = false;
    FoldRestoreCursor_();
}

// 光标位置→展开状态（纯判定 FoldBarWantsExpanded_）。事件坐标优先，其次查询
// 系统光标；输入钩子与绘制帧都调用，保证即时一致。
static void FoldUpdate_(int cursorX, int cursorY)
{
    bool inLamp = false, inPanel = false;
    FoldHotZone_(cursorX, cursorY, &inLamp, &inPanel);
    g_foldExpanded = FoldBarWantsExpanded_(g_foldExpanded, inLamp, inPanel);
}
static void FoldUpdateFromCursor_()
{
    const H3POINT cursor = H3POINT::GetCursorPosition();
    FoldUpdate_(cursor.x, cursor.y);
}

// 收起态输入放行判定：只有灯区内的输入交给界面，其余透传战场。
static bool FoldWantsInput_(int cursorX, int cursorY)
{
    if (g_foldExpanded) return true;
    FoldUpdate_(cursorX, cursorY);
    return g_foldExpanded;   // 事件落在灯上→这一下既是展开也是点击
}

class FoldableBarUi final : public IBattleStoreUi {
public:
    void Initialize() override
    {
        // 初值占位（800x600 逻辑屏内右上）：真正的灯心与面板锚点由
        // FoldSyncAnchor_ 每帧从战斗对话框矩形刷新（Initialize 时还没有
        // 战斗）。忽略 BarX/BarY（键名与旧行为不变）。
        g_uiFoldLayout = true;
        g_foldLampX = kFoldGameWidth - kFoldMargin - kFoldLampRadius;
        g_foldLampY = kFoldMargin + kFoldLampRadius;
        g_uiBarAnchorX = g_foldLampX - kFoldLampRadius - kFoldLampGap - kUiBarWidth;
        g_uiBarAnchorY = kFoldMargin;
        LogInfo("[Ui] fold bar placeholder anchor=%d,%d lamp=%d,%d",
            g_uiBarAnchorX, g_uiBarAnchorY, FoldLampCenterX_(), FoldLampCenterY_());
    }

    // 画状态灯（展开/收起同一位置）：小灯图按 2R+1 分配、灯心取 (R,R)，
    // UiDrawStatusLamp_ 的外框圈正好铺满不越界。（注意：H3LoadedPcx16::
    // FillRectangle 自带钳制，越界只会被裁掉，不会写坏堆内存——早年把
    // "彩色条纹"归因于此是错的，已更正，见 docs/变更记录.md 2026-10-11。）
    void DrawFoldLamp_(H3CombatManager* mgr)
    {
        const bool storageAllowed = UiStorageAllowed_(mgr);
        const int lampW = 2 * kFoldLampRadius + 1;
        if (!g_foldLamp || !g_foldLamp->buffer
            || g_foldLamp->width != lampW || g_foldLamp->height != lampW) {
            if (g_foldLamp) g_foldLamp->Destroy();
            g_foldLamp = H3LoadedPcx16::Create(lampW, lampW);
            if (!g_foldLamp || !g_foldLamp->buffer) return;
        }
        g_foldLamp->FillRectangle(0, 0, lampW, lampW, 20, 20, 20);
        UiDrawStatusLamp_(g_foldLamp, kFoldLampRadius, kFoldLampRadius,
            storageAllowed);
        H3WindowManager* wnd = H3WindowManager::Get();
        if (!wnd || !wnd->screenPcx16) return;
        const int lampX = FoldLampCenterX_() - kFoldLampRadius;
        const int lampY = FoldLampCenterY_() - kFoldLampRadius;
        if (UiBltPcx16Region_(g_foldLamp, 0, 0, lampW, lampW, lampX, lampY)) {
            g_foldDrawing = true;
            wnd->H3Redraw(lampX, lampY, lampW, lampW);
            g_foldDrawing = false;
        }
    }

    void Draw(H3CombatManager* mgr) override
    {
        if (g_foldDrawing) return;
        FoldSyncAnchor_(mgr);
        FoldUpdateFromCursor_();
        if (g_foldExpanded) {
            g_foldDrawing = true;
            UiDrawBar_(mgr);
            g_foldDrawing = false;
            // 记录展开态面板矩形（两行控制条 + 当前列表），供热区判定。
            g_foldPanelX = g_ui.x;
            g_foldPanelY = UiOriginY_();
            g_foldPanelW = kUiBarWidth;
            g_foldPanelH = g_uiLayout.Height(UiListRows_());
            // 灯在面板之外原位重画：面板右缘与灯左缘之间留间隙，
            // 展开不压灯、收起不跳位（用户 03:1x 纠正）。
            g_foldDrawing = true;
            DrawFoldLamp_(mgr);
            g_foldDrawing = false;
            // 展开区域内改用游戏默认光标（用户 03:3x 要求）。
            FoldTakeDefaultCursor_();
            return;
        }
        // 收起：把展开期间被面板盖住的战场像素从自存快照写回（save-under，
        // 见 BattleUi.inc.cpp 的 UiUnderCapture_ 机制注释）。战场内**不能**
        // 从 screenPcx16 拷回恢复源——那是战场外方案，战内实测复现彩色条纹
        // （用户 04:0x 实证）。快照由 UiDrawBar_ 在折叠布局每帧 rectChanged
        // 前捕获；从未展开过则没有快照，无从恢复也不需要恢复。
        // 换场时快照由 UiResetForBattle_ 释放，不跨战斗保留。
        if (UiUnderBufValid_()) {
            const int tailX = g_ui.x, tailY = UiOriginY_();
            const int tailH = g_uiLayout.Height(UiListRows_());
            g_foldDrawing = true;
            const bool ok = UiUnderRestore_();
            g_foldDrawing = false;
            LogDebug("[Ui] fold collapse restore x=%d y=%d h=%d ok=%d",
                tailX, tailY, tailH, ok ? 1 : 0);
        }
        // 离开展开区域（吸收态）：恢复接管前的光标，再画灯。
        FoldRestoreCursor_();
        DrawFoldLamp_(mgr);
    }

    void PollHover() override
    {
        if (!g_foldExpanded) return;
        const H3POINT cursor = H3POINT::GetCursorPosition();
        g_ui.logLevelHover = UiHitLogLevelItem_(cursor.x, cursor.y);
    }
    void FrameClick() override
    {
        if (!g_foldExpanded) return;
        const LONG clickX = InterlockedExchange(&g_pendingClickX, -1);
        const LONG clickY = InterlockedExchange(&g_pendingClickY, -1);
        if (clickX < 0 || clickY < 0) return;
        if (!FoldWantsInput_((int)clickX, (int)clickY)) return;
        UiHandleFrameClick_((int)clickX, (int)clickY);
    }
    void PollRebindKey() override { if (g_foldExpanded) UiPollRebindKey_(); }
    bool ReloadEntries(const H3CombatManager* mgr) override { return UiReloadEntries_(mgr); }
    bool HitBar(H3Msg* msg, bool fullBlock) override
    {
        if (!msg) return false;
        if (!FoldWantsInput_(msg->position.x, msg->position.y)) return false;
        return UiHitBar_(msg, fullBlock);
    }
    void HandleMouse(H3Msg* msg) override
    {
        if (!msg) return;
        if (!FoldWantsInput_(msg->position.x, msg->position.y)) return;
        UiHandleMouse_(msg);
    }
    void MaintainRestore(H3CombatManager* mgr) override { UiMaintainRestore_(mgr); }
    void ProcessRestore(H3CombatManager* mgr, int result) override { UiProcessRestore_(mgr, result); }
    // 读档成功后强制收起：战场被 kRefreshField 整体重绘，展开态继续会把
    // 界面自身的面板像素拍进 save-under 新快照，之后收起写回即彩条。
    void OnRestoreApplied() override { FoldForceCollapse_(); }
    void CancelRebind(const char* reason) override { UiCancelRebind_(reason); }
    void MarkSaved(uint64_t timestampUtcMs) override { UiMarkSaved_(timestampUtcMs); }
    void MarkNotice(const char* utf8Text) override { UiMarkNotice_(utf8Text); }
    void OnBattleReset() override
    {
        // 换场：展开态与光标接管一起复位，避免上一场的光标类型带到下一场。
        g_foldExpanded = false;
        FoldRestoreCursor_();
        UiResetForBattle_();
    }

    bool OnSystemKey(const UiKeyEvent_& e) override { return UiOnSystemKey_(e); }
    bool OnSystemMouse(const UiMouseEvent_& e, bool combatOpen) override
    {
        if (!FoldWantsInput_(e.gameX, e.gameY)) return false;
        return UiOnSystemMouse_(e, combatOpen);
    }
    void OnGameKeyBefore(const H3Msg* msg, int level) override { UiOnGameKeyBefore_(msg, level); }
    bool OnGameMouse(H3Msg* msg) override
    {
        if (!msg) return false;
        if (!FoldWantsInput_(msg->position.x, msg->position.y)) return false;
        return UiOnGameMouse_(msg);
    }
    void OnGameKeyAfter(H3CombatManager* mgr, const H3Msg* msg, int result, unsigned now, bool waitBlocked) override
    {
        UiOnGameKeyAfter_(mgr, msg, result, now, waitBlocked);
    }
    void OnFrameKeyPoll(H3CombatManager* mgr, unsigned now, bool waitBlocked) override
    {
        UiOnFrameKeyPoll_(mgr, now, waitBlocked);
    }
    void OnListReloaded() override { g_ui.hoverRow = -1; }
    void OnFaultCleanup() override { UiOnFaultCleanup_(); }
    bool IsRebindWaiting() const override { return g_ui.awaitingRebind; }
    char RebindLatchKey() const override { return g_ui.rebindKey; }
};

static FoldableBarUi g_foldBarUi;
// 钩子层唯一界面入口；编译期默认即折叠式界面。
static IBattleStoreUi* const g_uiPort = &g_foldBarUi;
