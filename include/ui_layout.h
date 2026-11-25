#pragma once

namespace UI {
constexpr int devicesPerPage = 6;
constexpr int gridTop = 38;
constexpr unsigned long refreshIntervalMs = 60000;
inline bool discoveryDue(unsigned long now, unsigned long last) {
    return (unsigned long)(now-last) >= refreshIntervalMs;
}
inline int pageCount(int count, bool camera = false) {
    return count > 0 ? (camera ? count : (count - 1)/devicesPerPage + 1) : 1;
}
inline int clampPage(int page, int count, bool camera = false) {
    const int pages = pageCount(count, camera);
    return page < 0 ? 0 : page >= pages ? pages-1 : page;
}
inline int visibleCount(int count, int page = 0) {
    int remaining = count - page*devicesPerPage;
    return remaining > devicesPerPage ? devicesPerPage : remaining < 0 ? 0 : remaining;
}
constexpr int refresh = -4, previous = -5, next = -6, changeView = -7;
struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x+w && py >= y && py < y+h; }
};
constexpr Rect refreshButton{5, 287, 30, 28};
constexpr Rect previousButton{40, 287, 34, 28};
constexpr Rect pageIndicator{76, 287, 45, 28};
constexpr Rect nextButton{123, 287, 34, 28};
constexpr Rect viewButton{162, 287, 73, 28};
constexpr int gridBottom = 283;

inline Rect tile(int index, int count, int page = 0) {
    const int visible = visibleCount(count, page);
    const int columns = visible <= 3 ? 1 : 2;
    const int rows = visible > 0 ? (visible + columns - 1) / columns : 1;
    const int gap = 5, margin = 5, gridWidth = 240 - 2*margin;
    const bool wideLastTile = columns == 2 && visible % 2 && index == visible - 1;
    const int width = (gridWidth - (columns-1)*gap) / columns;
    const int height = (gridBottom - gridTop - (rows-1)*gap) / rows;
    return {margin + (index % columns)*(width+gap), gridTop + (index/columns)*(height+gap),
            wideLastTile ? gridWidth : width, height};
}
inline int hit(int x, int y, int count, int page = 0, bool camera = false) {
    page = clampPage(page, count, camera);
    if (viewButton.contains(x, y)) return changeView;
    if (refreshButton.contains(x, y)) return refresh;
    if (pageCount(count, camera) > 1) {
        if (previousButton.contains(x, y)) return previous;
        if (nextButton.contains(x, y)) return next;
    }
    const int visible = camera ? 0 : visibleCount(count, page);
    for (int i = 0; i < visible; ++i)
        if (tile(i, count, page).contains(x, y)) return page*devicesPerPage+i;
    return -1;
}
inline int focusCount(int count, int page, bool camera = false) {
    return (camera ? 0 : visibleCount(count, page)) + (pageCount(count, camera) > 1 ? 4 : 2);
}
inline int focusedControl(int focus, int count, int page = 0, bool camera = false) {
    const int visible = camera ? 0 : visibleCount(count, page);
    if (focus < visible) return page*devicesPerPage+focus;
    switch (focus-visible) {
        case 0: return refresh;
        case 1: return pageCount(count, camera) > 1 ? previous : changeView;
        case 2: return next;
        default: return changeView;
    }
}
}
