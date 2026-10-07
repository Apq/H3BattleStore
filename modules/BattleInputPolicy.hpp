#pragma once

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
