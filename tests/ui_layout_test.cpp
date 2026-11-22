#include "ui_layout.h"
#include <cassert>
#include <initializer_list>
int main() {
    for (int count=1; count<=200; ++count) {
        for (int page=0; page<UI::pageCount(count); ++page) {
            int visible = UI::visibleCount(count,page);
            assert(visible>=1 && visible<=UI::devicesPerPage);
            int columns = visible<=3 ? 1 : 2;
            int rows = (visible + columns - 1) / columns;
            for (int i=0; i<visible; ++i) {
                UI::Rect rect = UI::tile(i,count,page);
                const int width = (230-(columns-1)*5)/columns;
                const bool wideLastTile = columns == 2 && visible % 2 && i == visible-1;
                assert(rect.w == (wideLastTile ? 230 : width));
                assert(rect.h == ((UI::gridBottom(count) - UI::gridTop)-(rows-1)*5)/rows);
                assert(rect.x == 5 + (i%columns)*(width+5));
                assert(rect.y == UI::gridTop + (i/columns)*(rect.h+5));
                assert(rect.x+rect.w<=235 && rect.y+rect.h<=UI::gridBottom(count));
                assert(UI::hit(rect.x+rect.w/2,rect.y+rect.h/2,count,page)==page*UI::devicesPerPage+i);
                assert(UI::focusedControl(i,count,page)==page*UI::devicesPerPage+i);
            }
            assert(UI::hit(205, 16, count, page)==UI::refresh);
            assert(UI::focusedControl(visible,count,page)==UI::changeView);
            if (count>UI::devicesPerPage) {
                assert(UI::hit(72,301,count,page)==UI::previous);
                assert(UI::hit(168,301,count,page)==UI::next);
            } else {
                assert(UI::hit(72,301,count,page)>=0);
            }
        }
    }
    // Larger Home Assistant instances can have entity indices above 1000.
    const int largeCount = 1010, largePage = 166;
    const auto tile = UI::tile(4, largeCount, largePage);
    assert(UI::hit(tile.x, tile.y, largeCount, largePage) == 1000);
    assert(UI::hit(10, 290, largeCount, largePage) != 1000);
    assert(UI::focusedControl(4, largeCount, largePage) == 1000);
    for (int count : {0, 1, 9, 10, 1000}) {
        assert(UI::hit(107, 4, count) == UI::changeView);
        assert(UI::hit(171, 27, count) == UI::changeView);
        assert(UI::hit(172, 27, count) != UI::changeView);
        assert(UI::focusedControl(UI::visibleCount(count) + 1, count) == UI::refresh);
    }
    assert(UI::pageCount(0)==1);
    assert(UI::pageCount(6)==1 && UI::pageCount(7)==2 && UI::pageCount(13)==3);
    assert(UI::visibleCount(7,1)==1);
    assert(UI::visibleCount(10,1)==4);
    assert(UI::tile(3,4).y + UI::tile(3,4).h >= UI::gridBottom(4) - 1);
    assert(UI::clampPage(3,3)==0);
    assert(UI::clampPage(-1,20)==0);
    assert(!UI::discoveryDue(59999,0));
    assert(UI::discoveryDue(60000,0));
    assert(UI::discoveryDue(100,static_cast<unsigned long>(100-60000UL)));
}
