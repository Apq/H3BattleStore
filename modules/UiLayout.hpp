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
// 存档列表最大行数固定 30（2026-10-08 用户裁定）：与同场磁盘保留条数一致，
// With 18px rows: origin 8 + bands 48 + rows 30*18 = bottom 596.
constexpr int kUiListMaxRows = 30;
constexpr int HotkeyX = 480 - 58;
constexpr int StatusLampRadius = 7;
constexpr int StatusLampX = HotkeyX - 16;
constexpr int StatusLabelWidth = StatusLampX - StatusLampRadius - 12;
constexpr int StatusLampY(Layout layout) { return layout.bandHeight + layout.bandHeight / 2; }
struct LampColor { unsigned char r, g, b; };
constexpr LampColor StatusLampColor(bool allowed) {
    return allowed ? LampColor{64, 230, 112} : LampColor{245, 64, 64};
}
constexpr int LampHalfWidth(int y, int radius) {
    int x = 0;
    while ((x + 1) * (x + 1) + y * y <= radius * radius) ++x;
    return x;
}
// Keep each source texture band's outer borders when extending its interior.
constexpr int TextureY(int y, int bandHeight) {
    const int band = y / bandHeight;
    const int local = y % bandHeight;
    return band * 24 + (local < 3 ? local :
        (local >= bandHeight - 3 ? 24 - (bandHeight - local) :
            3 + (local - 3) * 18 / (bandHeight - 6)));
}
}
