// ========== BattleAltTail.inc.cpp ==========
// ALT+点击：双格兵"退后一步"（2026-10-11 08:4x 用户裁定，唯一场景）。
// 玩家按住 ALT 点击双格兵尾部旁边的空格（若存在）时，兵整体向身后平移一格：
// 新 position = 原副格、新副格 = 点击格。原版做不到——默认点击格永远是兵身
// 默认端的落点，点身后格会被解释成"头端移动到该格"（转身绕行），无法平移后退。
//
// 官方依据（Heroes3Src FUN_00476500 case 1/2，H3Note §10.12）：
//   - 左键点击分发 0x476500(mgr, intent)，intent=1/2 为纯移动：action=2、
//     actionTarget = 点击格（hover 缓存 mgr+0x132D4）；
//   - 朝向语义（FUN_00468310/4683A0）：副格 = position + (orientation? +1 : -1)；
//     双格兵副格与 position 同行相邻；摆位 orientation = 1 - side。
// 本钩子在原函数返回后覆盖 actionTarget = 原副格。攻击走 case 3/F（action=7），
// 覆盖条件 action==2 天然排除——ALT 对攻击、施法、等待无影响；点击的空格
// 不可走时游戏本就不会给出移动意图（E0≠1/2 → action≠2），钩子同样不触发。

// 纯决策核心：ALT+点击"尾旁身后格"时返回退后落位（原副格）；否则 -1 不干预。
// 与生产同源（tests/AltTailTests.cpp 以 H3BATTLE_ALTTAIL_CORE_ONLY 只含此层）。
static int AltTailRetreatTarget_(int action, bool altDown, bool doubleWide, bool humanSide,
    int orientation, int position, int clickedHex)
{
    if (action != 2 || !altDown || !doubleWide || !humanSide) return -1;
    if (position < 0 || position > 186 || clickedHex < 0 || clickedHex > 186) return -1;
    const int step = orientation != 0 ? 1 : -1;
    const int tail = position + step;   // 副格（兵占着，必与 position 同行相邻）
    const int behind = tail + step;     // 尾旁身后空格 = 退后后的新副格
    if (behind < 0 || behind > 186) return -1;
    if (tail / 17 != position / 17 || behind / 17 != tail / 17) return -1; // 同行相邻结构界
    if (clickedHex != behind) return -1;
    return tail;                        // 新 position = 原副格，兵整体退后一格
}

#ifndef H3BATTLE_ALTTAIL_CORE_ONLY

static const int GUARD_ALTTAIL = GuardRegisterHook_("BattleStore.AltTail");

// 公共解析：ALT 按下且"当前/选中兵"为人类方双格兵、点击格恰为其尾旁横向格时，
// 返回退后落位（原副格）；否则 -1。战斗与战术两阶段共用（当前兵 = mgr+0x132B8/
// 0x132BC，战斗中为行动兵、战术阶段为选中兵——FUN_00476500 同一取法）。
static int AltTailResolve_(H3CombatManager* mgr)
{
    if (!mgr || IsBadReadPtr(mgr, sizeof(H3CombatManager))) return -1;
    const int side = mgr->currentMonSide;
    const int index = mgr->currentMonIndex;
    if (side < 0 || side > 1 || index < 0 || index >= 21) return -1;
    const H3CombatCreature* unit = &mgr->stacks[side][index];
    if (IsBadReadPtr(unit, sizeof(H3CombatCreature))) return -1;
    // +0x84 bit0 = 双格兵 flag（H3Note BattleCrashFix 笔记、FUN_00468310 同源）。
    const bool doubleWide = (*(const uint8_t*)((const uint8_t*)unit + 0x84) & 1) != 0;
    const int unitSide = *(const int32_t*)((const uint8_t*)unit + 0xF4);
    if (unitSide < 0 || unitSide > 1) return -1;
    return AltTailRetreatTarget_((int)mgr->action, true, doubleWide,
        mgr->isHuman[unitSide] != 0,
        *(const int32_t*)((const uint8_t*)unit + 0x44),
        *(const int32_t*)((const uint8_t*)unit + 0x38),
        *(const int32_t*)((const uint8_t*)mgr + 0x132D4));
}

// 战术阶段专用：不要求 action==2（摆位不走 action 队列），只看几何与阵营。
static int AltTailTacticsResolve_(H3CombatManager* mgr)
{
    if (!mgr || IsBadReadPtr(mgr, sizeof(H3CombatManager))) return -1;
    const int side = mgr->currentMonSide;
    const int index = mgr->currentMonIndex;
    if (side < 0 || side > 1 || index < 0 || index >= 21) return -1;
    const H3CombatCreature* unit = &mgr->stacks[side][index];
    if (IsBadReadPtr(unit, sizeof(H3CombatCreature))) return -1;
    const bool doubleWide = (*(const uint8_t*)((const uint8_t*)unit + 0x84) & 1) != 0;
    const int unitSide = *(const int32_t*)((const uint8_t*)unit + 0xF4);
    if (unitSide < 0 || unitSide > 1) return -1;
    return AltTailRetreatTarget_(2, true, doubleWide,
        mgr->isHuman[unitSide] != 0,
        *(const int32_t*)((const uint8_t*)unit + 0x44),
        *(const int32_t*)((const uint8_t*)unit + 0x38),
        *(const int32_t*)((const uint8_t*)mgr + 0x132D4));
}

// 0x476500 void __thiscall(H3CombatManager* mgr, int intent)
// 战斗阶段：原函数返回后覆盖 actionTarget（移动意图 action==2 已由原函数设定）。
// 战术阶段：摆位在原函数内同步完成，必须在调用前改写点击格缓存 mgr+0x132D4。
static void __stdcall Hook_AltTailClick_(HiHook* hook, H3CombatManager* mgr, int intent)
{
    __try {
        if (mgr && (GetAsyncKeyState(VK_MENU) & 0x8000)
            && *(const int32_t*)((const uint8_t*)mgr + 0x13D68) != 0) { // tacticsPhase
            const int target = AltTailTacticsResolve_(mgr);
            if (target >= 0) {
                const int clicked = *(const int32_t*)((const uint8_t*)mgr + 0x132D4);
                *(int32_t*)((uint8_t*)mgr + 0x132D4) = target;
                LogInfo("[AltTail] tactics retreat: clicked=%d -> %d side=%d:%d",
                    clicked, target, mgr->currentMonSide, mgr->currentMonIndex);
            }
        }
    }
    __except (GuardCrashFilter_(GUARD_ALTTAIL, GetExceptionInformation())) {}
    // 原函数恰好一次、guarded 块之外；原版异常照常传播。
    THISCALL_2(void, hook->GetDefaultFunc(), mgr, intent);
    __try {
        // 仅纯移动（攻击 case 3/F 设 action=7 天然排除）。
        if (mgr && (int)mgr->action == 2
            && *(const int32_t*)((const uint8_t*)mgr + 0x13D68) == 0) {
            const int target = AltTailResolve_(mgr);
            if (target >= 0) {
                const int clicked = *(const int32_t*)((const uint8_t*)mgr + 0x132D4);
                *(int32_t*)((uint8_t*)mgr + 0x44) = target; // mgr->actionTarget = 原副格
                LogInfo("[AltTail] retreat one step: clicked=%d -> target=%d side=%d:%d",
                    clicked, target, mgr->currentMonSide, mgr->currentMonIndex);
            }
        }
    }
    __except (GuardCrashFilter_(GUARD_ALTTAIL, GetExceptionInformation())) {}
}

#endif // H3BATTLE_ALTTAIL_CORE_ONLY
