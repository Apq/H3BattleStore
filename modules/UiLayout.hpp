#pragma once

namespace hbs_ui {
constexpr int Max(int a, int b) { return a > b ? a : b; }
struct Layout {
    int bandHeight;
    int rowHeight;
    constexpr int OriginY(int designY) const { return Max(0, designY - (bandHeight - 24) / 2); }
    constexpr int ListTop() const { return 2 * bandHeight; }
    constexpr int Height(int rows) const { return ListTop() + rows * rowHeight; }
    constexpr int HitRow(int y, int rows) const {
        return y < ListTop() || y >= Height(rows) ? -1 : (y - ListTop()) / rowHeight;
    }
};
constexpr Layout ForFont(int fontHeight) { return { Max(24, fontHeight), Max(18, fontHeight) }; }
// Keep each source texture band's outer borders when extending its interior.
constexpr int TextureY(int y, int bandHeight) {
    const int band = y / bandHeight;
    const int local = y % bandHeight;
    return band * 24 + (local < 3 ? local :
        (local >= bandHeight - 3 ? 24 - (bandHeight - local) :
            3 + (local - 3) * 18 / (bandHeight - 6)));
}
}
