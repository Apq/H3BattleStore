#pragma once

struct BattleStorageWindowState_ {
    bool initialized;
    bool fatal;
    bool mainDialog;
    bool finished;
    bool autoCombat;
    bool tactics;
    bool action;
    bool executing;
    bool casting;
    bool humanTurn;
    bool activeReady;
};

static constexpr bool BattleStorageAllowed_(const BattleStorageWindowState_& state)
{
    return state.initialized && !state.fatal && state.mainDialog && !state.finished
        && !state.autoCombat && !state.tactics && !state.action && !state.executing
        && !state.casting && state.humanTurn && state.activeReady;
}

// 读档请求的独立维护判定（2026-10-10 玩家日志：绝对消息深度阻断消费入口后，
// 超时取消也永远到不了，请求挂起 5 分 40 秒红灯）。维护只取消排队状态，
// 不写任何战斗内存；每帧调用，不受恢复安全点门控。
// 超时兜底 20 秒（2026-10-10 应用户要求由 5 秒加长）：排队请求滞留 20 秒
// 仍无消费边界即取消；UiExecuteRestore_ 进入确认前已清 pending，确认框
// 耗时从不计入排队年龄。
static constexpr unsigned long kRestoreQueueTimeoutMs_ = 20000;
enum class BattleRestoreMaintain_ { Keep, CancelBattleChanged, CancelTimeout };
struct BattleRestoreMaintainState_ {
    bool pending;
    bool battleAvailable;       // 管理器可读且战斗未结束
    unsigned entryGeneration;   // 请求排队时的战斗场次
    unsigned currentGeneration;
    unsigned long ageMs;
};
static constexpr BattleRestoreMaintain_ BattleRestoreMaintainDecision_(const BattleRestoreMaintainState_& s)
{
    if (!s.pending) return BattleRestoreMaintain_::Keep;
    if (!s.battleAvailable || s.entryGeneration != s.currentGeneration)
        return BattleRestoreMaintain_::CancelBattleChanged;
    // 严格大于：恰好 20000ms 仍保留。
    if (s.ageMs > kRestoreQueueTimeoutMs_) return BattleRestoreMaintain_::CancelTimeout;
    return BattleRestoreMaintain_::Keep;
}

// 登记制消息帧表（2026-10-10 18:18 玩家日志实证修复）：原实现把栈上 POD 帧
// 挂 parent 链（parent 落在帧内 +0x0D）。玩家在战斗结束→同管理器立刻重开后，
// Boundary() 沿该链读到无效地址 0x1881，触发 AV（H3BattleStore.dll+0x157A4）；
// 日志证明链上指针无效、触发集中在换场首帧，但不能单独证明最初破坏链的机制
// （例如原生非局部退栈未执行 finally 仍属待证）。改为固定数组登记、按帧地址
// 索引，永不追踪栈指针；换代清理旧代登记；地址复用覆盖同地址旧登记；同代
// 满表时整代 fail-closed，避免少算深度后误认 Boundary。帧字段就地修改后须
// Update 同步。
struct BattleMessageFrame_ {
    unsigned generation;
    const void* manager;
    const void* dialog;
    bool nativeReturned;
};
struct BattleMessageFrames_ {
    static constexpr int kMaxFrames_ = 64;
    struct Entry_ { const void* key; unsigned long long seq; BattleMessageFrame_ frame; };
    Entry_ slots[kMaxFrames_] = {};
    unsigned long long nextSeq = 0;
    unsigned saturatedGeneration = 0;
    bool saturated = false;
    int newest = -1;
    int Find_(const void* key) const {
        for (int i = 0; i < kMaxFrames_; ++i)
            if (slots[i].key == key) return i;
        return -1;
    }
    void ResetGeneration_(unsigned generation) {
        for (int i = 0; i < kMaxFrames_; ++i)
            if (slots[i].key && slots[i].frame.generation != generation)
                slots[i].key = nullptr;
        if (saturated && saturatedGeneration != generation) saturated = false;
        newest = -1;
        unsigned long long best = 0;
        for (int i = 0; i < kMaxFrames_; ++i)
            if (slots[i].key && slots[i].seq > best) { best = slots[i].seq; newest = i; }
    }
    unsigned long long Enter(BattleMessageFrame_& frame, unsigned generation,
        const void* manager, const void* dialog) {
        ResetGeneration_(generation);
        frame = {generation, manager, dialog, false};
        const void* key = &frame;
        // 地址复用优先覆盖；其次取空位；满表时该代 fail-closed。
        int slot = Find_(key);
        if (slot < 0) slot = Find_(nullptr);
        if (slot < 0) {
            saturated = true;
            saturatedGeneration = generation;
            newest = -1;
            return 0;
        }
        slots[slot].key = key;
        slots[slot].seq = ++nextSeq;
        slots[slot].frame = frame;
        newest = slot;
        return slots[slot].seq;
    }
    void Update(const BattleMessageFrame_& frame, unsigned long long token) {
        const int slot = Find_(&frame);
        if (slot >= 0 && token && slots[slot].seq == token) slots[slot].frame = frame;
    }
    void Leave(const BattleMessageFrame_& frame, unsigned long long token) {
        const int slot = Find_(&frame);
        if (slot >= 0 && token && slots[slot].seq == token) slots[slot].key = nullptr;
        newest = -1;
        unsigned long long best = 0;
        for (int i = 0; i < kMaxFrames_; ++i)
            if (slots[i].key && slots[i].seq > best) { best = slots[i].seq; newest = i; }
    }
    int Depth(unsigned generation) const {
        int depth = 0;
        for (int i = 0; i < kMaxFrames_; ++i)
            if (slots[i].key && slots[i].frame.generation == generation) ++depth;
        return depth;
    }
    bool Boundary(unsigned generation, const void* manager, const void* dialog) const {
        if (saturated && saturatedGeneration == generation) return false;
        if (newest < 0 || !slots[newest].key) return false;
        const BattleMessageFrame_& top = slots[newest].frame;
        return top.nativeReturned && top.generation == generation
            && top.manager == manager && top.dialog == dialog
            && dialog && Depth(generation) == 1;
    }
};

static constexpr bool BattleUiLeftDown_(unsigned message) {
    return message == 0x201 || message == 0x203;
}
static constexpr bool BattleUiRightDown_(unsigned message) {
    return message == 0x204 || message == 0x206;
}
enum class BattleUiRelease_ { Pass, Swallow, Activate };
struct BattleUiPointerGesture_ {
    bool held = false;
    bool cancelled = false;
    constexpr void Begin(bool scrollbar) { held = true; cancelled = scrollbar; }
    constexpr void CancelClick() { if (held) cancelled = true; }
    constexpr void StopReleasedDrag(bool& dragging, bool leftDown) {
        if (dragging && !leftDown) { dragging = false; CancelClick(); }
    }
    constexpr BattleUiRelease_ Release(bool inside, bool scrollbar) {
        const bool wasHeld = held;
        const bool wasCancelled = cancelled;
        held = cancelled = false;
        if (!inside) return wasHeld ? BattleUiRelease_::Swallow : BattleUiRelease_::Pass;
        return wasHeld && !wasCancelled && !scrollbar
            ? BattleUiRelease_::Activate : BattleUiRelease_::Swallow;
    }
};

// 0x200 aliases native item commands and mouse-button notifications.
// Overlay clicks are dispatched exclusively by the system mouse hook.
static constexpr bool BattleUiMayConsumeMessage_(int command)
{
    return command == 0x4;
}

static constexpr bool BattleIsKeyboardMessage_(int command)
{
    return command == 0x1 || command == 0x2;
}

// 折叠式界面热区语义（第二套界面，默认实现；docs/09 第8节）：
// 展开态要求光标留在整个面板矩形内，移出即收起；收起态只有状态灯是
// 热区，光标进入才展开。判定纯函数化便于回归。
static constexpr bool FoldBarWantsExpanded_(bool expanded, bool inLamp, bool inPanel)
{
    return expanded ? inPanel : inLamp;
}

static constexpr bool FoldBarRectContains_(int x, int y, int w, int h, int px, int py)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

// 折叠式界面锚点常量（生产实现与回归测试共用；锚点=战场内右上角）。
static const int kFoldGameWidth = 800;   // SoD 800x600 游戏坐标
static const int kFoldMargin = 8;
