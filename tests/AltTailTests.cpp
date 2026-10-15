// AltTailTests：双格兵 ALT 退后一步的纯决策核心回归（与生产同源，
// 以 H3BATTLE_ALTTAIL_CORE_ONLY 只编译 AltTailRetreatTarget_ 层）。
// 09:20 实测口径：原版默认 target=clicked-(orientation?+1:-1)（点击格反推一格
// =后退两步），ALT 覆盖为点击格本身 = position 落点击格 = 只后退一格。
// 2026-10-15 起测试框架换 Catch2 v3 amalgamated（参照 H3Space）。
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include <catch_amalgamated.hpp>

#define H3BATTLE_ALTTAIL_CORE_ONLY
#include "../modules/BattleAltTail.inc.cpp"

// orientation=1（尾/position端在右）：position=140，正后方一格=139，点 139 → 落 139
// （兵 {140,141}→{139,140} = 向身后平移一格；原版会落 138 = 平移两格）。
TEST_CASE("retreat one step lands on clicked hex (tail right)")
{
    CHECK(AltTailRetreatTarget_(2, true, true, true, 1, 140, 139) == 139);
}

// orientation=0（尾端在左）：position=140，正后方一格=141，点 141 → 落 141
// （兵 {139,140}→{140,141} = 平移一格）。
TEST_CASE("retreat one step lands on clicked hex (tail left)")
{
    CHECK(AltTailRetreatTarget_(2, true, true, true, 0, 140, 141) == 141);
}

// 只有"尾部正后方那一格"触发：点击自己/副格/原版默认反推的格/更远格全部拒绝。
TEST_CASE("only the hex directly behind the tail end triggers")
{
    CHECK(AltTailRetreatTarget_(2, true, true, true, 1, 140, 138) == -1);
    CHECK(AltTailRetreatTarget_(2, true, true, true, 1, 140, 140) == -1);
    CHECK(AltTailRetreatTarget_(2, true, true, true, 1, 140, 141) == -1);
    CHECK(AltTailRetreatTarget_(2, true, true, true, 0, 140, 138) == -1);
}

// 贴行边无身后格：position=85（row5 首列），orientation=1 时身后格=84 已跨行 → 拒绝。
TEST_CASE("no hex behind at row edge refuses")
{
    CHECK(AltTailRetreatTarget_(2, true, true, true, 1, 85, 84) == -1);
}

// 棋盘边界：position=0，orientation=0 时身后格=-1 越界 → 拒绝。
TEST_CASE("off-board retreat hex refuses")
{
    CHECK(AltTailRetreatTarget_(2, true, true, true, 0, 0, -1) == -1);
}

// 攻击/无 ALT/非双格/非人类/非移动意图一律不干预。
TEST_CASE("attack/no-alt/single-wide/ai-side stay vanilla")
{
    CHECK(AltTailRetreatTarget_(7, true, true, true, 1, 140, 139) == -1);
    CHECK(AltTailRetreatTarget_(2, false, true, true, 1, 140, 139) == -1);
    CHECK(AltTailRetreatTarget_(2, true, false, true, 1, 140, 139) == -1);
    CHECK(AltTailRetreatTarget_(2, true, true, false, 1, 140, 139) == -1);
}

// position/点击格越界防御。
TEST_CASE("out-of-range inputs refuse")
{
    CHECK(AltTailRetreatTarget_(2, true, true, true, 1, 187, 139) == -1);
    CHECK(AltTailRetreatTarget_(2, true, true, true, 1, 140, 187) == -1);
}
