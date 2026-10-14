#pragma once
#include <stddef.h>

namespace hbs_ui {
constexpr int Max(int a, int b) { return a > b ? a : b; }
struct Layout {
    int bandHeight;
    int rowHeight;
    constexpr int OriginY(int designY) const { return Max(0, designY - (bandHeight - 24) / 2); }
    // 2026-10-11 自动存档：两行控制条下方加一行标题（手动存档/自动存档），
    // 列表顶随之下移一行；标题行自身不算列表行（HitRow 从 ListTop 起算）。
    constexpr int TitleTop() const { return 2 * bandHeight; }
    constexpr int ListTop() const { return 2 * bandHeight + rowHeight; }
    constexpr int Height(int rows) const { return ListTop() + rows * rowHeight; }
    constexpr int HitRow(int y, int rows) const {
        return y < ListTop() || y >= Height(rows) ? -1 : (y - ListTop()) / rowHeight;
    }
};
constexpr Layout ForFont(int fontHeight) { return { Max(24, fontHeight), Max(18, fontHeight) }; }
constexpr int kUiListVisibleRows = 20;
constexpr int ListWidth = 168;
constexpr int ScrollbarWidth = 14;
// 自动列表固定起点：手动列表宽 + 预留滚动条位 + 2px 分隔（固定不随滚动条
// 出现/消失移动，布局稳定）。
constexpr int AutoListX = ListWidth + ScrollbarWidth + 2;
// 列表区总宽（双列固定占位；实际 blt 按各自行数与滚动条裁剪）。
constexpr int ListsWidth = AutoListX + ListWidth + ScrollbarWidth;
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
constexpr int TextureBandY(int local, int bandHeight, int sourceBand) {
    return sourceBand * 24 + (local < 3 ? local :
        (local >= bandHeight - 3 ? 24 - (bandHeight - local) :
            3 + (local - 3) * 18 / (bandHeight - 6)));
}
constexpr int TextureY(int y, int bandHeight) {
    return TextureBandY(y % bandHeight, bandHeight, y / bandHeight);
}
// ---- 第一行：日志等级项（5 项）+「战场外」复选框 + 打包日志按钮 ----
// 2026-10-14 用户要求："把日志等级的选项给排得紧凑一点，留出空间来加新选项"。
// 项宽由运行期字体测量提供（生产用 H3Font::GetMaxLineWidth，含左右内边距），
// 本处只做纯几何：等宽等级项自左排列，其后留 LogRowFlagGap 放复选框，按钮贴
// 条右缘。等级项的标记槽恒为 1 字符（选中 "*"、未选中空格），故所有项等宽、
// 选中不跳位；tagged 状态另用高亮底 + 亮绿字标注（原 "(*) " 前缀太宽，五级
// 加复选框放不下 480px 条）。
constexpr int LogRowLevelCount = 5;
constexpr int LogRowPadX = 4;        // 项内左右内边距
constexpr int LogRowGap = 4;         // 相邻等级项之间的间隙
constexpr int LogRowFlagGap = 8;     // 等级项与复选框之间的留白
constexpr int LogRowPackWidth = 84;  // 「打包日志」按钮宽度
constexpr int LogRowPackMargin = 4;  // 按钮右缘与条右缘的间距
// 「战场外」复选框文本：勾选/未勾选两种形态（等宽取较宽者）。
constexpr int LogRowOutsideTagCount = 2;
constexpr const char* LogRowOutsideTag(int checked) {
    return checked ? "(*)战场外" : "( )战场外";
}
struct LogRowItem { int x; int width; };
struct LogRow {
    LogRowItem level[LogRowLevelCount];
    LogRowItem outside;
    int packX;
    int packWidth;
    int tailX;   // 等级项 + 复选框占用的右边界（不含按钮）
    int spare;   // 按钮左侧剩余空间（留给后续新选项；<0 表示超出条宽）
    constexpr int HitLevel(int dx, int dy, int bandHeight) const {
        if (dy < 0 || dy >= bandHeight) return -1;
        for (int i = 0; i < LogRowLevelCount; ++i)
            if (dx >= level[i].x && dx < level[i].x + level[i].width) return i;
        return -1;
    }
    constexpr bool HitOutside(int dx, int dy, int bandHeight) const {
        return dy >= 0 && dy < bandHeight
            && dx >= outside.x && dx < outside.x + outside.width;
    }
};
// levelsWidth/outsideWidth 由运行期字体测量提供（含左右内边距）。放不下时先
// 压缩等级项宽度（文本居中裁切），保证复选框与打包按钮永不重叠——字体异常
// 放大时也不会盖住按钮；正常字体下实测宽度留有 spare 余量（见回归测试）。
constexpr LogRow PlaceLogRow(int barWidth, int levelsWidth, int outsideWidth) {
    LogRow row = {};
    const int packX = barWidth - LogRowPackWidth - LogRowPackMargin;
    const int gaps = LogRowGap * (LogRowLevelCount - 1);
    const int roomForLevels = packX - LogRowFlagGap - outsideWidth - gaps;
    int levelWidth = levelsWidth;
    if (roomForLevels > 0 && LogRowLevelCount * levelWidth > roomForLevels)
        levelWidth = roomForLevels / LogRowLevelCount;
    int x = 0;
    for (int i = 0; i < LogRowLevelCount; ++i) {
        row.level[i] = LogRowItem{x, levelWidth};
        x += levelWidth + (i + 1 < LogRowLevelCount ? LogRowGap : 0);
    }
    row.outside = LogRowItem{x + LogRowFlagGap, outsideWidth};
    row.tailX = row.outside.x + outsideWidth;
    row.packWidth = LogRowPackWidth;
    row.packX = packX;
    row.spare = row.packX - row.tailX;
    return row;
}
}
