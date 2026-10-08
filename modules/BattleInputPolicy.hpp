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
