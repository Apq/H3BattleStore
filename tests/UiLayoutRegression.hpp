#pragma once
#include "../modules/UiLayout.hpp"

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
}
