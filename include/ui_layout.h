#pragma once

namespace UI {
constexpr int devicesPerPage = 9;
constexpr int gridTop = 94, gridHeight = 182;
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
constexpr int allOn = -2, allOff = -3, refresh = -4, previous = -5, next = -6;
struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x+w && py >= y && py < y+h; }
};
inline Rect tile(int index, int count) {
    count = visibleCount(count);
    int columns = count <= 3 ? 1 : count <= 6 ? 2 : 3;
    // Four/five controls reserve the same two-column, three-row grid as six.
    int rows = count <= 3 ? count : 3;
    if (rows < 1) rows = 1;
    int gap = 6;
    int cellW = (224 - (columns-1)*gap) / columns;
    int cellH = (gridHeight - (rows-1)*gap) / rows;
    return {8 + (index % columns)*(cellW+gap), gridTop + (index/columns)*(cellH+gap), cellW, cellH};
}
inline int hit(int x, int y, int count, int page = 0) {
    page = clampPage(page,count);
    if (pageCount(count) > 1 && y >= 28 && y < 56) {
        if (x >= 166 && x < 194) return previous;
        if (x >= 204 && x < 232) return next;
    }
    int visible = visibleCount(count,page);
    for (int i = 0; i < visible; ++i)
        if (tile(i,visible).contains(x,y)) return page*devicesPerPage+i;
    if (y >= 284 && y < 316) {
        if (x >= 8 && x < 78) return allOn;
        if (x >= 85 && x < 155) return allOff;
        if (x >= 162 && x < 232) return refresh;
    }
    return -1;
}
inline int focusCount(int count, int page) {
    return visibleCount(count,page) + (pageCount(count)>1 ? 5 : 3);
}
inline int focusedControl(int focus, int count, int page = 0) {
    int visible = visibleCount(count,page);
    if (focus < visible) return page*devicesPerPage+focus;
    switch (focus-visible) {
        case 0: return allOn;
        case 1: return allOff;
        case 2: return refresh;
        case 3: return previous;
        default: return next;
    }
}
}
