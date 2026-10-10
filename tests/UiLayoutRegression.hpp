#pragma once
#include "../modules/UiLayout.hpp"
#include "../modules/BattleInputPolicy.hpp"

namespace hbs_ui_test {
constexpr auto normal = hbs_ui::ForFont(16);
constexpr auto tall = hbs_ui::ForFont(30);
static_assert(normal.bandHeight == 24 && normal.rowHeight == 18);
static_assert(normal.ListTop() == 48 && normal.Height(10) == 228);
static_assert(tall.bandHeight == 30 && tall.rowHeight == 30);
static_assert(tall.ListTop() == 60 && tall.Height(10) == 360);
static_assert(normal.OriginY(8) == 8 && tall.OriginY(8) == 5);
static_assert(tall.OriginY(8) + tall.bandHeight / 2 == 8 + 24 / 2);
static_assert(hbs_ui::ForFont(64).OriginY(8) == 0);
static_assert(hbs_ui::ForFont(22).bandHeight == 24 && hbs_ui::ForFont(22).rowHeight == 22);
static_assert(hbs_ui::ForFont(0).rowHeight == 18);
static_assert(hbs_ui::kUiListVisibleRows == 20);
static_assert(normal.Height(hbs_ui::kUiListVisibleRows) == 408);
constexpr bool ScrollListRegression() {
    hbs_ui::ScrollList list;
    if (list.Rows(0) != 0 || list.Rows(19) != 19 || list.Rows(21) != 20) return false;
    if (list.Hit(normal, 48, 0) != -1) return false;
    list.Move(3, 100);
    if (list.first != 3 || list.Hit(normal, 48, 100) != 3
        || list.Hit(normal, 407, 100) != 22 || list.Hit(normal, 408, 100) != -1) return false;
    list.Move(200, 100);
    if (list.first != 80 || list.Hit(normal, 407, 100) != 99) return false;
    list.Move(-200, 100);
    if (list.first != 0) return false;
    list.Wheel(-60, 100);
    if (list.first != 0 || list.wheelRemainder != -60) return false;
    list.Wheel(-60, 100);
    if (list.first != 3 || list.wheelRemainder != 0) return false;
    list.Wheel(120, 100);
    if (list.first != 0) return false;
    list.Move(100, 100);
    list.dragging = true;
    list.Clamp(20);
    if (list.first != 0 || list.dragging) return false;
    list.Move(20, 21);
    if (list.first != 1 || list.Hit(normal, 407, 21) != 20) return false;
    const auto start = hbs_ui::ForList(normal, 100, 0);
    const auto end = hbs_ui::ForList(normal, 100, 80);
    if (start.thumbTop != start.trackTop || end.thumbTop + end.thumbHeight
        != end.trackTop + end.trackHeight) return false;
    if (start.FirstAt(-1000, 80) != 0 || start.FirstAt(1000, 80) != 80) return false;
    const auto huge = hbs_ui::ForList(normal, 1000000, 999980);
    if (huge.thumbHeight != 18 || huge.FirstAt(huge.thumbTop, 999980) != 999980) return false;
    return true;
}
static_assert(ScrollListRegression());
constexpr bool ScrollbarDragRegression() {
    hbs_ui::ScrollList list;
    list.first = 3;
    list.dragStartY = 100;
    list.dragFirst = list.first;
    const auto bar = hbs_ui::ForList(normal, 1000, list.first);
    const size_t last = list.LastFirst(1000);
    if (bar.FirstAt(bar.thumbTop, last) == list.first) return false;
    if (bar.FirstAfterDrag(0, list.dragFirst, last) != 3
        || bar.FirstAfterDrag(1, list.dragFirst, last) != 6
        || bar.FirstAfterDrag(-1, list.dragFirst, last) != 0) return false;
    if (bar.FirstAfterDrag(1, 100, last) != 103
        || bar.FirstAfterDrag(-1, 100, last) != 97) return false;
    if (bar.FirstAfterDrag(1000, 3, last) != last
        || bar.FirstAfterDrag(-1000, 3, last) != 0) return false;
    if (bar.FirstAfterDrag(2147483647, 3, last) != last
        || bar.FirstAfterDrag(-2147483647 - 1, 3, last) != 0) return false;
    const auto twenty = hbs_ui::ForList(normal, 20, 0);
    if (twenty.FirstAfterDrag(0, 0, 0) != 0
        || twenty.FirstAfterDrag(1, 0, 0) != 0
        || twenty.FirstAfterDrag(-1, 0, 0) != 0) return false;
    const auto twentyOne = hbs_ui::ForList(normal, 21, 0);
    if (twentyOne.Travel() != 16
        || twentyOne.FirstAfterDrag(7, 0, 1) != 0
        || twentyOne.FirstAfterDrag(8, 0, 1) != 1
        || twentyOne.FirstAfterDrag(-7, 1, 1) != 1
        || twentyOne.FirstAfterDrag(-8, 1, 1) != 0) return false;
    const auto million = hbs_ui::ForList(normal, 1000000, 5000);
    if (million.FirstAfterDrag(0, 5000, 999980) != 5000
        || million.FirstAfterDrag(1, 5000, 999980) != 8185
        || million.FirstAfterDrag(-1, 5000, 999980) != 1815
        || million.FirstAfterDrag(million.Travel(), 0, 999980) != 999980
        || million.FirstAfterDrag(-million.Travel(), 999980, 999980) != 0
        || million.FirstAfterDrag(2147483647, 0, 999980) != 999980
        || million.FirstAfterDrag(-2147483647 - 1, 999980, 999980) != 0) return false;
    const hbs_ui::Scrollbar wideTravel = {0, 0, 10018, 0, 18};
    if (wideTravel.FirstAfterDrag(5000, 499990, 999980) != 999980
        || wideTravel.FirstAfterDrag(-5000, 499990, 999980) != 0) return false;
    const hbs_ui::Scrollbar noTravel = {0, 0, 18, 0, 18};
    if (noTravel.FirstAfterDrag(1, 3, 980) != 3
        || noTravel.FirstAfterDrag(-1, 1000, 980) != 980) return false;
    const hbs_ui::Scrollbar half = {0, 0, 4, 0, 2};
    const size_t maximum = static_cast<size_t>(-1);
    if (half.FirstAfterDrag(1, 0, maximum) != maximum / 2 + 1
        || half.FirstAfterDrag(-1, maximum, maximum) != maximum / 2) return false;
    return true;
}
static_assert(ScrollbarDragRegression());
static_assert(tall.HitRow(59, 10) == -1 && tall.HitRow(60, 10) == 0);
static_assert(tall.HitRow(89, 10) == 0 && tall.HitRow(90, 10) == 1);
static_assert(tall.HitRow(359, 10) == 9 && tall.HitRow(360, 10) == -1);
static_assert(tall.HitRow(60, 0) == -1 && tall.Height(0) == 60);
constexpr bool TextureValid(int height) {
    int previous = -1;
    for (int y = 0; y < height * 2; ++y) {
        const int sy = hbs_ui::TextureY(y, height);
        if (sy < 0 || sy >= 48 || sy < previous) return false;
        if (height == 24 && sy != y) return false;
        previous = sy;
    }
    return true;
}
static_assert(TextureValid(24) && TextureValid(30) && TextureValid(64));
static_assert(hbs_ui::TextureY(0, 30) == 0 && hbs_ui::TextureY(29, 30) == 23);
static_assert(hbs_ui::TextureY(30, 30) == 24 && hbs_ui::TextureY(59, 30) == 47);
static_assert(hbs_ui::StatusLampX + hbs_ui::StatusLampRadius + 1 < hbs_ui::HotkeyX);
static_assert(6 + hbs_ui::StatusLabelWidth < hbs_ui::StatusLampX - hbs_ui::StatusLampRadius - 1);
static_assert(hbs_ui::StatusLampY(normal) - hbs_ui::StatusLampRadius - 1 > normal.bandHeight);
static_assert(hbs_ui::StatusLampY(tall) + hbs_ui::StatusLampRadius + 1 < tall.ListTop());
static_assert(hbs_ui::LampHalfWidth(0, 7) == 7 && hbs_ui::LampHalfWidth(7, 7) == 0);
// 灯环越界回归（2026-10-11 02:4x 彩条实证）：UiDrawStatusLamp_ 的外框圈半径
// 用 radius+1，故"灯心 + 外框圈"整体跨度是 2(r+1)+1。小灯图必须按这个跨度
// 分配；旧代码按 2r+1 分配会单边越界 1px 踩相邻堆内存（越界写入的正是灯环/
// 灯色，表现为战场偶发彩色条纹）。
constexpr int kLampRingRadius = hbs_ui::StatusLampRadius + 1;
constexpr int kLampSpanFromBarWidth = hbs_ui::HotkeyX + 58;     // HotkeyX = 条宽-58
static_assert(hbs_ui::LampHalfWidth(kLampRingRadius, kLampRingRadius) == 0);
static_assert(2 * kLampRingRadius + 1 == 17);
static_assert(kLampRingRadius + (2 * kLampRingRadius + 1) <= kLampSpanFromBarWidth);
// 面板与灯互不重叠（用户 03:1x 纠正：灯保持原位，向左下展开的区域不压灯）：
// 灯心贴战场内右上角（右缘/上缘内 kFoldMargin），面板右缘 = 灯心 - R - gap，
// 面板左缘仍在战场内。用 HotkeyX 反推条宽，不依赖生产 TU 的 kUiBarWidth。
constexpr int kLampCenterXTest = kFoldGameWidth - kFoldMargin - kLampRingRadius;
constexpr int kLampCenterYTest = kFoldMargin + kLampRingRadius;
constexpr int kPanelRightTest = kLampCenterXTest - kLampRingRadius - 2;   // gap=2
constexpr int kPanelLeftTest = kPanelRightTest - kLampSpanFromBarWidth;
static_assert(kLampCenterXTest + kLampRingRadius < kFoldGameWidth);        // 灯在战场内右缘
static_assert(kLampCenterYTest - kLampRingRadius >= 0);                   // 灯在战场内上缘
static_assert(kPanelRightTest + 2 <= kLampCenterXTest - kLampRingRadius); // 面板不压灯
static_assert(kPanelLeftTest >= 0);                                       // 面板不出左缘
static_assert(hbs_ui::StatusLampColor(true).g > hbs_ui::StatusLampColor(true).r);
static_assert(hbs_ui::StatusLampColor(false).r > hbs_ui::StatusLampColor(false).g);
// 折叠界面热区语义（BattleFoldBar）：收起态只有灯是热区，光标移入灯内展开
// 一次；展开态光标留在灯或面板内都保持，两者都离开才收起。灯在面板之外，
// 故"展开态 + 光标在灯上（inPanel=false）"必须仍然展开（03:3x 实证：否则
// 悬停灯上会每帧收起→展开反复重画）。
static_assert(FoldBarWantsExpanded_(false, true, true));           // 移入灯 → 展开
static_assert(FoldBarWantsExpanded_(false, false, true) == false); // 收起态光面板不算
static_assert(FoldBarWantsExpanded_(false, true, false));          // 移入灯 → 展开
static_assert(FoldBarWantsExpanded_(false, false, false) == false); // 都不在 → 保持收起
static_assert(FoldBarWantsExpanded_(true, false, true));           // 展开态留在面板
static_assert(FoldBarWantsExpanded_(true, true, false));           // 展开态停在灯上仍保持
static_assert(FoldBarWantsExpanded_(true, true, true));            // 两者都在
static_assert(FoldBarWantsExpanded_(true, false, false) == false); // 都离开 → 收起
static_assert(FoldBarRectContains_(10, 20, 30, 40, 10, 20));
static_assert(FoldBarRectContains_(10, 20, 30, 40, 39, 59));
static_assert(!FoldBarRectContains_(10, 20, 30, 40, 40, 59));
static_assert(!FoldBarRectContains_(10, 20, 30, 40, 60, 20));
static_assert(!FoldBarRectContains_(10, 20, 30, 40, 9, 30));
// 折叠锚点落在战场内右上角方向（常量与生产共用，宽度不属纯布局头不管辖）。
static_assert(kFoldGameWidth - kFoldMargin > 8);
static_assert(kFoldGameWidth >= 800);
static_assert(kFoldMargin > 0 && kFoldMargin < 64);
static_assert(kFoldGameWidth - kFoldMargin > kFoldGameWidth / 2);
constexpr bool StorageWindowTruthTable() {
    for (unsigned mask = 0; mask < (1u << 11); ++mask) {
        const BattleStorageWindowState_ state = {
            (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0,
            (mask & 8) != 0, (mask & 16) != 0, (mask & 32) != 0,
            (mask & 64) != 0, (mask & 128) != 0, (mask & 256) != 0,
            (mask & 512) != 0, (mask & 1024) != 0};
        if (BattleStorageAllowed_(state) != (mask == (1u | 4u | 512u | 1024u))) return false;
    }
    return true;
}
static_assert(StorageWindowTruthTable());
// 折叠界面"读档后收起"的作用域回归（2026-10-11 06:0x 玩家实测回归）：
// 折叠层只在**真的读过档**时强制收起，普通消息不得收起。原缺陷是把收起
// 钩子挂在消息钩子后无条件调用（ProcessRestore 每条消息都会到），结果鼠标
// 刚移出灯就被按回未展开态，再也展不开、没机会读档。
// 这里用纯函数复刻生产语义：帧内 FoldUpdate_ 推进展开态，消息只在
// restoreApplied 为真时收起。
constexpr bool FoldRestoreCollapseScope() {
    // 帧内：FoldUpdate_ 用纯判定推进展开态；消息：只有 restoreApplied 为真才收起。
    bool expanded = false;
    auto frame = [&expanded](bool inLamp, bool inPanel) {
        expanded = FoldBarWantsExpanded_(expanded, inLamp, inPanel);
    };
    auto message = [&expanded](bool restoreApplied) {
        if (restoreApplied) expanded = false;
    };
    // 读档前：hover 灯展开 → 移入面板保持 → 普通消息不得收起。
    frame(true, false);
    if (!expanded) return false;
    frame(false, true);                 // 光标移出灯、进入面板
    if (!expanded) return false;
    message(false);                     // 普通战斗消息（无读档）
    if (!expanded) return false;        // 必须仍然展开
    frame(false, true);
    if (!expanded) return false;
    // 读过档：收起一次，且重新 hover 灯能再展开、并能移入面板。
    message(true);
    if (expanded) return false;
    frame(true, false);
    if (!expanded) return false;
    frame(false, true);
    if (!expanded) return false;        // 读档后仍能移入面板
    // 之后再来普通消息，不得再次收起。
    message(false);
    return expanded;
}
static_assert(FoldRestoreCollapseScope());
constexpr BattleStorageWindowState_ idleHuman = {
    true, false, true, false, false, false, false, false, false, true, true};
static_assert(BattleStorageAllowed_(idleHuman));
static_assert([] { auto state = idleHuman; state.casting = true; return !BattleStorageAllowed_(state); }());
static_assert([] { auto state = idleHuman; state.executing = true; return !BattleStorageAllowed_(state); }());
}
