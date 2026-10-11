// ========== BattleAltTail.inc.cpp ==========
// ALT+点击：双格兵"退后一步"（2026-10-11 08:4x–09:2x 用户裁定/实测修正，唯一场景）。
// 玩家按住 ALT 点击双格兵尾部（position 端）正后方的那格（若存在）时，兵整体
// 只向身后平移一格：position 落到点击格。原版做不到——原版对双格兵默认把
// 点击格按当前朝向反推一格（09:20 实测 target 恒 = clicked-(orientation?+1:-1)），
// 点击身后格时 position 会落两格外，即"后退两步"；按 ALT 抵消该反推即为一步。
//
// 官方依据（Heroes3Src FUN_00476500 case 1/2，H3Note §10.12）：左键点击分发
// 0x476500(mgr, intent)，intent=1/2 纯移动：action=2、actionTarget=点击格 D4
// （mgr+0x132D4 hover 缓存），再对双格兵换算 target = D4-(orientation?+1:-1)。
// 朝向（FUN_00468310/4683A0）：副格 = position + (orientation?+1:-1)，摆位
// orientation = 1 - side；副格与 position 同行相邻（17 格/行）。攻击走 case 3/F
// （action=7）天然排除；点击格不可走时游戏本就不产生移动意图，钩子不触发
// （"如果存在"天然成立）。

// 纯决策核心：ALT+点"position 端正后方一格"时返回退后一步的落位=点击格本身；
// 否则 -1 不干预。与生产同源（tests/AltTailTests.cpp 以 H3BATTLE_ALTTAIL_CORE_ONLY
// 只编译此层）。
static int AltTailRetreatTarget_(int action, bool altDown, bool doubleWide, bool humanSide,
    int orientation, int position, int clickedHex)
{
    if (action != 2 || !altDown || !doubleWide || !humanSide) return -1;
    if (position < 0 || position > 186 || clickedHex < 0 || clickedHex > 186) return -1;
    const int step = orientation != 0 ? 1 : -1;
    const int retreat = position - step; // position 端正后方一格（与 position 同行相邻）
    if (retreat < 0 || retreat > 186) return -1;
    if (retreat / 17 != position / 17) return -1; // 同行相邻结构界
    if (clickedHex != retreat) return -1;
    return retreat;                     // position 落点击格：兵整体后退一格
}

#ifndef H3BATTLE_ALTTAIL_CORE_ONLY

static const int GUARD_ALTTAIL = GuardRegisterHook_("BattleStore.AltTail");

// 战斗阶段解析：命中退后场景时返回应写入的 actionTarget（=点击格）。战后兵
// position 落该格，整体只平移一格。
static int AltTailResolve_(int action, H3CombatManager* mgr)
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
    return AltTailRetreatTarget_(action, true, doubleWide,
        mgr->isHuman[unitSide] != 0,
        *(const int32_t*)((const uint8_t*)unit + 0x44),
        *(const int32_t*)((const uint8_t*)unit + 0x38),
        *(const int32_t*)((const uint8_t*)mgr + 0x132D4));
}

// 战术阶段解析：命中退后场景时返回应写入的点击格缓存（mgr+0x132D4）=position。
// 原版恒把缓存格按朝向反推一格（target=Hex-step），缓存=position 时反推结果
// 恰为点击格（position-step）。
static int AltTailTacticsResolve_(H3CombatManager* mgr, int* clickedOut)
{
    if (clickedOut) *clickedOut = -1;
    if (!mgr || IsBadReadPtr(mgr, sizeof(H3CombatManager))) return -1;
    const int side = mgr->currentMonSide;
    const int index = mgr->currentMonIndex;
    if (side < 0 || side > 1 || index < 0 || index >= 21) return -1;
    const H3CombatCreature* unit = &mgr->stacks[side][index];
    if (IsBadReadPtr(unit, sizeof(H3CombatCreature))) return -1;
    const bool doubleWide = (*(const uint8_t*)((const uint8_t*)unit + 0x84) & 1) != 0;
    const int unitSide = *(const int32_t*)((const uint8_t*)unit + 0xF4);
    if (unitSide < 0 || unitSide > 1) return -1;
    const int clicked = *(const int32_t*)((const uint8_t*)mgr + 0x132D4);
    const int position = *(const int32_t*)((const uint8_t*)unit + 0x38);
    const int retreat = AltTailRetreatTarget_(2, true, doubleWide,
        mgr->isHuman[unitSide] != 0,
        *(const int32_t*)((const uint8_t*)unit + 0x44), position, clicked);
    if (retreat < 0) return -1;
    if (clickedOut) *clickedOut = clicked;
    return position;
}

// 0x476500 void __thiscall(H3CombatManager* mgr, int intent)
// 战斗阶段：原函数返回后覆盖 actionTarget（移动意图 action=2 已由原函数设定）。
// 战术阶段：摆位在原函数内同步完成，必须在调用前改写点击格缓存 mgr+0x132D4。
// 0x476500 void __thiscall(H3CombatManager* mgr, int intent)
// 战斗阶段：原函数返回后覆盖 actionTarget（移动意图 action=2 已由原函数设定）。
// 战术阶段：摆位在原函数内同步完成，必须在调用前改写点击格缓存 mgr+0x132D4。
// 日志只在命中 ALT 退后场景时打一行（09:31 收窄，未命中=原版行为无痕迹）。
static void __stdcall Hook_AltTailClick_(HiHook* hook, H3CombatManager* mgr, int intent)
{
    bool reached = false;
    int prePos = -1, preOrient = -1;
    __try {
        if (mgr && !IsBadReadPtr(mgr, sizeof(H3CombatManager))) {
            reached = true;
            const int side = mgr->currentMonSide;
            const int index = mgr->currentMonIndex;
            if (side >= 0 && side <= 1 && index >= 0 && index < 21) {
                const H3CombatCreature* unit = &mgr->stacks[side][index];
                if (!IsBadReadPtr(unit, sizeof(H3CombatCreature))) {
                    prePos = *(const int32_t*)((const uint8_t*)unit + 0x38);
                    preOrient = *(const int32_t*)((const uint8_t*)unit + 0x44);
                }
            }
        }
    }
    __except (GuardCrashFilter_(GUARD_ALTTAIL, GetExceptionInformation())) {}
    // 战术阶段：调用前改写点击格缓存为 position（原版反推后 target=点击格）。
    __try {
        if (reached && (GetAsyncKeyState(VK_MENU) & 0x8000)
            && *(const int32_t*)((const uint8_t*)mgr + 0x13D68) != 0) { // tacticsPhase
            int clicked = -1;
            const int cacheHex = AltTailTacticsResolve_(mgr, &clicked);
            if (cacheHex >= 0) {
                *(int32_t*)((uint8_t*)mgr + 0x132D4) = cacheHex;
                LogInfo("[AltTail] tactics retreat: clicked=%d cacheHex=%d pos=%d",
                    clicked, cacheHex, prePos);
            }
        }
    }
    __except (GuardCrashFilter_(GUARD_ALTTAIL, GetExceptionInformation())) {}
    // 原函数恰好一次、guarded 块之外；原版异常照常传播。
    THISCALL_2(void, hook->GetDefaultFunc(), mgr, intent);
    __try {
        // 仅纯移动（攻击 case 3/F 设 action=7 天然排除）。
        if (reached && (int)mgr->action == 2
            && *(const int32_t*)((const uint8_t*)mgr + 0x13D68) == 0) {
            const int target = AltTailResolve_((int)mgr->action, mgr);
            if (target >= 0) {
                *(int32_t*)((uint8_t*)mgr + 0x44) = target; // mgr->actionTarget = 点击格
                LogInfo("[AltTail] retreat one step: target=%d pos=%d orient=%d",
                    target, prePos, preOrient);
            }
        }
    }
    __except (GuardCrashFilter_(GUARD_ALTTAIL, GetExceptionInformation())) {}
}

#endif // H3BATTLE_ALTTAIL_CORE_ONLY
