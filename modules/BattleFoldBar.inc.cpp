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

static bool g_foldExpanded = false;
static H3LoadedPcx16* g_foldLamp = nullptr;
// 展开态面板矩形（每帧由 UiDrawBar_ 的推导量刷新；输入钩子即时查询）。
static int g_foldPanelX = 0;
static int g_foldPanelY = 0;
static int g_foldPanelW = 0;
static int g_foldPanelH = 0;

// 灯中心（游戏坐标）：与展开面板内的灯位严格同一处（合成图内灯在
// kUiBarWidth-radius-1, radius+1），保证收起↔展开切换时灯不跳位。
// 面板右缘贴战场右缘（留 kFoldMargin），面板从灯向左、向下生长。
static int FoldLampCenterX_()
{
    return g_uiBarAnchorX + kUiBarWidth - hbs_ui::StatusLampRadius - 1;
}

static int FoldLampCenterY_()
{
    return g_uiLayout.OriginY(g_uiBarAnchorY) + hbs_ui::StatusLampRadius + 1;
}

static void FoldHotZone_(int cursorX, int cursorY, bool* inLamp, bool* inPanel)
{
    const int lampW = 2 * hbs_ui::StatusLampRadius + 1 + 2 * kFoldLampPad;
    const int lampX = FoldLampCenterX_() - hbs_ui::StatusLampRadius - kFoldLampPad;
    const int lampY = FoldLampCenterY_() - hbs_ui::StatusLampRadius - kFoldLampPad;
    *inLamp = FoldBarRectContains_(lampX, lampY, lampW, lampW, cursorX, cursorY);
    *inPanel = FoldBarRectContains_(g_foldPanelX, g_foldPanelY,
        g_foldPanelW, g_foldPanelH, cursorX, cursorY);
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
        // 锚点一次性算准：灯中心 = 战场右缘-边距（X）、边距+半径（Y）。
        // 由灯位反推面板左上角，展开/收起切换时灯严格不动。
        // 忽略 BarX/BarY（键名与旧行为不变）。
        g_uiFoldLayout = true;
        g_uiBarAnchorX = kFoldGameWidth - kFoldMargin
            - kUiBarWidth + hbs_ui::StatusLampRadius + 1;
        g_uiBarAnchorY = kFoldMargin;
        LogInfo("[Ui] fold bar anchor=%d,%d lamp=%d,%d expanded_input=灯热区",
            g_uiBarAnchorX, g_uiBarAnchorY, FoldLampCenterX_(), FoldLampCenterY_());
    }

    void Draw(H3CombatManager* mgr) override
    {
        // 重入闸（2026-10-11 02:18 闪退实证）：收起态 H3Redraw 会同步重入
        // AfterBlt→Draw，无闸时"画灯→H3Redraw→Draw"无限递归栈溢出。
        // 旧 UiDrawBar_ 的 redrawing 闸同理，只护展开路径，这里护收起路径。
        static bool foldDrawing = false;
        if (foldDrawing) return;
        FoldUpdateFromCursor_();
        if (g_foldExpanded) {
            foldDrawing = true;
            UiDrawBar_(mgr);
            foldDrawing = false;
            // 记录展开态面板矩形（两行控制条 + 当前列表），供热区判定。
            g_foldPanelX = g_ui.x;
            g_foldPanelY = UiOriginY_();
            g_foldPanelW = kUiBarWidth;
            g_foldPanelH = g_uiLayout.Height(UiListRows_());
            return;
        }
        // 收起：只画状态灯，失效残影跟踪。战场内每帧自动重绘，上一帧展开的
        // 大矩形会被战斗场景自然覆盖，不需要从 screenPcx16 拷回（与战场外
        // 区域的差异，2026-10-11 用户纠正）。灯色跟随保存窗口门禁，语义与
        // 旧界面同一判定（UiStorageAllowed_）。
        UiInvalidateTail_();
        const bool storageAllowed = UiStorageAllowed_(mgr);
        const int lampW = 2 * hbs_ui::StatusLampRadius + 1;
        if (!g_foldLamp || !g_foldLamp->buffer
            || g_foldLamp->width != lampW || g_foldLamp->height != lampW) {
            if (g_foldLamp) g_foldLamp->Destroy();
            g_foldLamp = H3LoadedPcx16::Create(lampW, lampW);
            if (!g_foldLamp || !g_foldLamp->buffer) return;
        }
        g_foldLamp->FillRectangle(0, 0, lampW, lampW, 20, 20, 20);
        UiDrawStatusLamp_(g_foldLamp, hbs_ui::StatusLampRadius,
            hbs_ui::StatusLampRadius, storageAllowed);
        H3WindowManager* wnd = H3WindowManager::Get();
        if (!wnd || !wnd->screenPcx16) return;
        const int lampX = FoldLampCenterX_() - hbs_ui::StatusLampRadius;
        const int lampY = FoldLampCenterY_() - hbs_ui::StatusLampRadius;
        if (UiBltPcx16Region_(g_foldLamp, 0, 0, lampW, lampW, lampX, lampY)) {
            foldDrawing = true;
            wnd->H3Redraw(lampX, lampY, lampW, lampW);
            foldDrawing = false;
        }
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
    void CancelRebind(const char* reason) override { UiCancelRebind_(reason); }
    void MarkSaved(uint64_t timestampUtcMs) override { UiMarkSaved_(timestampUtcMs); }
    void MarkNotice(const char* utf8Text) override { UiMarkNotice_(utf8Text); }
    void OnBattleReset() override { UiResetForBattle_(); }

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
