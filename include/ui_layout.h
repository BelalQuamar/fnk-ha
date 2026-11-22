#pragma once

namespace UI {
constexpr int devicesPerPage = 9;
constexpr int gridTop = 38;
constexpr unsigned long refreshIntervalMs = 60000;
inline bool discoveryDue(unsigned long now, unsigned long last) {
    return (unsigned long)(now-last) >= refreshIntervalMs;
}
inline int pageCount(int count) { return count > 0 ? (count+8)/9 : 1; }
inline int clampPage(int page, int count) {
    return page < 0 ? 0 : page >= pageCount(count) ? pageCount(count)-1 : page;
}
inline int visibleCount(int count, int page = 0) {
    int remaining = count - page*devicesPerPage;
    return remaining > 9 ? 9 : remaining < 0 ? 0 : remaining;
}
constexpr int refresh = -4, previous = -5, next = -6, changeView = -7;
struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x+w && py >= y && py < y+h; }
};
constexpr Rect viewButton{107, 4, 65, 24};
constexpr Rect refreshButton{180, 4, 52, 24};
constexpr Rect previousButton{52, 289, 40, 25};
constexpr Rect nextButton{148, 289, 40, 25};
inline int gridBottom(int count) { return pageCount(count) > 1 ? 283 : 312; }

inline Rect tile(int index, int count, int page = 0) {
    const int visible = visibleCount(count, page);
    const int columns = visible <= 3 ? 1 : visible <= 6 ? 2 : 3;
    const int rows = visible > 0 ? (visible + columns - 1) / columns : 1;
    const int gap = 6;
    const int width = (224 - (columns-1)*gap) / columns;
    const int height = (gridBottom(count) - gridTop - (rows-1)*gap) / rows;
    return {8 + (index % columns)*(width+gap), gridTop + (index/columns)*(height+gap), width, height};
}
inline int hit(int x, int y, int count, int page = 0) {
    page = clampPage(page, count);
    if (viewButton.contains(x, y)) return changeView;
    if (refreshButton.contains(x, y)) return refresh;
    if (pageCount(count) > 1) {
        if (previousButton.contains(x, y)) return previous;
        if (nextButton.contains(x, y)) return next;
    }
    const int visible = visibleCount(count, page);
    for (int i = 0; i < visible; ++i)
        if (tile(i, count, page).contains(x, y)) return page*devicesPerPage+i;
    return -1;
}
inline int focusCount(int count, int page) {
    return visibleCount(count, page) + (pageCount(count) > 1 ? 4 : 2);
}
inline int focusedControl(int focus, int count, int page = 0) {
    const int visible = visibleCount(count, page);
    if (focus < visible) return page*devicesPerPage+focus;
    switch (focus-visible) {
        case 0: return changeView;
        case 1: return refresh;
        case 2: return previous;
        default: return next;
    }
}
}
