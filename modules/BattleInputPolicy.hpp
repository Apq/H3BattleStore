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
// 5 秒超时也永远到不了，请求挂起 5 分 40 秒红灯）。维护只取消排队状态，
// 不写任何战斗内存；每帧调用，不受恢复安全点门控。
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
    // 严格大于：恰好 5000ms 仍保留（UiExecuteRestore_ 进入确认前即清 pending，
    // 确认框耗时从不计入排队年龄）。
    if (s.ageMs > 5000ul) return BattleRestoreMaintain_::CancelTimeout;
    return BattleRestoreMaintain_::Keep;
}

// 栈上POD帧；只由钩子finally出栈，不分配、不在换场时清零。
// 按场次区分旧场祖先，管理器/对话框身份在返回后重新核验。
struct BattleMessageFrame_ {
    unsigned generation;
    const void* manager;
    const void* dialog;
    bool nativeReturned;
    BattleMessageFrame_* parent;
};
struct BattleMessageFrames_ {
    BattleMessageFrame_* top;
    void Enter(BattleMessageFrame_& frame, unsigned generation,
        const void* manager, const void* dialog) {
        frame = {generation, manager, dialog, false, top};
        top = &frame;
    }
    void Leave(const BattleMessageFrame_& frame) { top = frame.parent; }
    int Depth(unsigned generation) const {
        int depth = 0;
        for (const BattleMessageFrame_* f = top; f; f = f->parent)
            if (f->generation == generation) ++depth;
        return depth;
    }
    bool Boundary(unsigned generation, const void* manager, const void* dialog) const {
        return top && top->nativeReturned && top->generation == generation
            && top->manager == manager && top->dialog == dialog
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
