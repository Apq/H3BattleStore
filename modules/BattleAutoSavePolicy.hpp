#pragma once

// 每回合首个玩家等待只尝试一次；失败不重试，换场/成功读档重新武装。
struct BattleAutoSavePolicy_ {
    bool valid = false;
    int turn = 0;
    constexpr bool Due(bool playerWindow, int currentTurn) const {
        return playerWindow && (!valid || turn != currentTurn);
    }
    constexpr void Attempted(int currentTurn) { valid = true; turn = currentTurn; }
    constexpr void Reset() { valid = false; turn = 0; }
};
