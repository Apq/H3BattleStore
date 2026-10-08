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
// 列表可见行数上限（2026-10-08 用户裁定）：按可用高度取满——600 高度基准下
// 能显示多少行就列多少行；更大屏也不超过磁盘保留的同场 30 条。
constexpr int kUiListHardCap = 30;
constexpr int RowsForHeight(int listTopY, int bottomY, int rowHeight) {
    if (rowHeight <= 0) return 1;
    const int avail = bottomY - listTopY;
    const int rows = avail > 0 ? avail / rowHeight : 1;
    return rows < 1 ? 1 : (rows > kUiListHardCap ? kUiListHardCap : rows);
}
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
