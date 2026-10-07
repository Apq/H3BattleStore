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
