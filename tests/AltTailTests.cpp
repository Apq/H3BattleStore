// AltTailTests：双格兵 ALT 退后一步的纯决策核心回归（与生产同源，
// 以 H3BATTLE_ALTTAIL_CORE_ONLY 只编译 AltTailRetreatTarget_ 层）。
#include <cstdio>

#define H3BATTLE_ALTTAIL_CORE_ONLY
#include "../modules/BattleAltTail.inc.cpp"

static int g_failed_ = 0;
static void Expect(bool ok, const char* what)
{
    if (ok) { std::printf("  PASS  %s\n", what); }
    else    { std::printf("FAIL  %s\n", what); ++g_failed_; }
}

int main()
{
    // 场景基线：orientation=0（副格在左/尾左头右），position=100，尾=99，尾旁横向空格=98。
    Expect(AltTailRetreatTarget_(2, true, true, true, 0, 100, 98) == 99,
        "retreat left lands on old tail hex");
    // orientation=1（尾右）：position=95（row5 列10），尾=96，尾旁空格=97 → 落 96。
    Expect(AltTailRetreatTarget_(2, true, true, true, 1, 95, 97) == 96,
        "retreat right lands on old tail hex");
    // 尾旁"仅横向"：点击同一行更远/更近的非尾旁格不触发（97、99、101 等都拒绝）。
    Expect(AltTailRetreatTarget_(2, true, true, true, 0, 100, 97) == -1
        && AltTailRetreatTarget_(2, true, true, true, 0, 100, 99) == -1
        && AltTailRetreatTarget_(2, true, true, true, 0, 100, 101) == -1,
        "only the hex directly beside the tail triggers");
    // 贴行边无横向空格：position=84（row4 末列），orientation=1 时尾=85 已跨行 → 拒绝。
    Expect(AltTailRetreatTarget_(2, true, true, true, 1, 84, 86) == -1,
        "no lateral space at row edge refuses");
    // 棋盘边界：orientation=0、position=1（尾=0，尾旁=-1 越界）→ 拒绝。
    Expect(AltTailRetreatTarget_(2, true, true, true, 0, 1, -1) == -1,
        "off-board behind hex refuses");
    // 攻击/无 ALT/非双格/非人类/非移动意图一律不干预。
    Expect(AltTailRetreatTarget_(7, true, true, true, 0, 100, 98) == -1
        && AltTailRetreatTarget_(2, false, true, true, 0, 100, 98) == -1
        && AltTailRetreatTarget_(2, true, false, true, 0, 100, 98) == -1
        && AltTailRetreatTarget_(2, true, true, false, 0, 100, 98) == -1,
        "attack/no-alt/single-wide/ai-side stay vanilla");
    // position/点击格越界防御。
    Expect(AltTailRetreatTarget_(2, true, true, true, 0, 187, 185) == -1
        && AltTailRetreatTarget_(2, true, true, true, 0, 100, 187) == -1,
        "out-of-range inputs refuse");

    if (g_failed_) { std::printf("%d alttail test(s) failed\n", g_failed_); return 1; }
    std::printf("PASS all alttail tests\n");
    return 0;
}
