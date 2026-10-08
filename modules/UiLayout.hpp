#pragma once
#include <stddef.h>

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
constexpr int kUiListVisibleRows = 20;
constexpr int ListWidth = 168;
constexpr int ScrollbarWidth = 14;
struct ScrollList {
    size_t first = 0;
    bool dragging = false;
    int dragStartY = 0;
    size_t dragFirst = 0;
    int wheelRemainder = 0;
    constexpr int Rows(size_t count) const {
        return count < kUiListVisibleRows ? static_cast<int>(count) : kUiListVisibleRows;
    }
    constexpr size_t LastFirst(size_t count) const {
        return count > kUiListVisibleRows ? count - kUiListVisibleRows : 0;
    }
    constexpr void Clamp(size_t count) {
        if (first > LastFirst(count)) first = LastFirst(count);
        if (count <= kUiListVisibleRows) dragging = false;
    }
    constexpr void Move(int rows, size_t count) {
        Clamp(count);
        if (rows < 0) {
            const size_t distance = static_cast<size_t>(-static_cast<long long>(rows));
            first = distance > first ? 0 : first - distance;
        } else {
            const size_t available = LastFirst(count) - first;
            first += static_cast<size_t>(rows) > available ? available : static_cast<size_t>(rows);
        }
    }
    constexpr void Wheel(int delta, size_t count) {
        if (dragging) return;
        const int accumulated = wheelRemainder + delta;
        wheelRemainder = accumulated % 120;
        Move(-(accumulated / 120) * 3, count);
    }
    constexpr ptrdiff_t Hit(Layout layout, int y, size_t count) const {
        const int row = layout.HitRow(y, Rows(count));
        return row < 0 ? -1 : static_cast<ptrdiff_t>(first + row);
    }
};
struct Scrollbar {
    int buttonHeight;
    int trackTop;
    int trackHeight;
    int thumbTop;
    int thumbHeight;
    constexpr int Travel() const { return trackHeight - thumbHeight; }
    constexpr size_t FirstAfterDrag(int deltaY, size_t startFirst, size_t lastFirst) const {
        if (deltaY == 0) return startFirst;
        const size_t first = startFirst > lastFirst ? lastFirst : startFirst;
        const int travel = Travel();
        if (travel <= 0 || lastFirst == 0) return first;
        const unsigned long long pixels = deltaY < 0
            ? static_cast<unsigned long long>(-static_cast<long long>(deltaY))
            : static_cast<unsigned long long>(deltaY);
        const unsigned long long span = static_cast<unsigned long long>(travel);
        if (pixels >= span) return deltaY < 0 ? 0 : lastFirst;
        // Split before multiplying so even a 64-bit lastFirst cannot overflow.
        const unsigned long long last = static_cast<unsigned long long>(lastFirst);
        const unsigned long long distance = pixels * (last / span)
            + (pixels * (last % span) + span / 2) / span;
        if (deltaY < 0) return distance > first ? 0 : first - static_cast<size_t>(distance);
        const size_t available = lastFirst - first;
        return distance > available ? lastFirst : first + static_cast<size_t>(distance);
    }
    constexpr size_t FirstAt(int thumbY, size_t lastFirst) const {
        const int y = thumbY < trackTop ? 0 :
            (thumbY > trackTop + Travel() ? Travel() : thumbY - trackTop);
        return Travel() <= 0 ? 0 : static_cast<size_t>(
            (static_cast<unsigned long long>(y) * lastFirst + Travel() / 2) / Travel());
    }
};
constexpr Scrollbar ForList(Layout layout, size_t count, size_t first) {
    const int height = kUiListVisibleRows * layout.rowHeight;
    const int button = ScrollbarWidth;
    const int track = height - 2 * button;
    const size_t lastFirst = count > kUiListVisibleRows ? count - kUiListVisibleRows : 0;
    const int thumb = lastFirst == 0 ? track : Max(18, static_cast<int>(
        static_cast<unsigned long long>(track) * kUiListVisibleRows / count));
    const int travel = track - thumb;
    const int offset = lastFirst == 0 ? 0 : static_cast<int>(
        static_cast<unsigned long long>(travel) * (first > lastFirst ? lastFirst : first) / lastFirst);
    return {button, button, track, button + offset, thumb};
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
