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
static_assert(hbs_ui::StatusLampColor(true).g > hbs_ui::StatusLampColor(true).r);
static_assert(hbs_ui::StatusLampColor(false).r > hbs_ui::StatusLampColor(false).g);
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
constexpr BattleStorageWindowState_ idleHuman = {
    true, false, true, false, false, false, false, false, false, true, true};
static_assert(BattleStorageAllowed_(idleHuman));
static_assert([] { auto state = idleHuman; state.casting = true; return !BattleStorageAllowed_(state); }());
static_assert([] { auto state = idleHuman; state.executing = true; return !BattleStorageAllowed_(state); }());
}
