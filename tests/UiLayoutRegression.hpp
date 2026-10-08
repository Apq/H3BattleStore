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
// 列表最大行数固定 30（2026-10-08 用户裁定）：600 高度默认字体下
// 8+48+30*18=596 恰好全部可见，与同场磁盘保留条数一致。
static_assert(hbs_ui::kUiListMaxRows == 30);
static_assert(normal.Height(hbs_ui::kUiListMaxRows) == 48 + 30 * 18);
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
