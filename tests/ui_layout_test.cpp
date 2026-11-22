#include "ui_layout.h"
#include <cassert>
#include <initializer_list>
int main() {
    for (int count=1; count<=200; ++count) {
        for (int page=0; page<UI::pageCount(count); ++page) {
            int visible = UI::visibleCount(count,page);
            assert(visible>=1 && visible<=9);
            int columns = visible<=3 ? 1 : visible<=6 ? 2 : 3;
            int rows = visible<=3 ? visible : 3;
            for (int i=0; i<visible; ++i) {
                UI::Rect rect = UI::tile(i,visible);
                assert(rect.w == (224-(columns-1)*6)/columns);
                assert(rect.h == (UI::gridHeight-(rows-1)*6)/rows);
                assert(rect.x == 8 + (i%columns)*(rect.w+6));
                assert(rect.y == UI::gridTop + (i/columns)*(rect.h+6));
                assert(rect.x+rect.w<=232 && rect.y+rect.h<=276);
                assert(UI::hit(rect.x+rect.w/2,rect.y+rect.h/2,count,page)==page*9+i);
                assert(UI::focusedControl(i,count,page)==page*9+i);
            }
            assert(UI::hit(10,290,count,page)==UI::allOn);
            assert(UI::hit(100,290,count,page)==UI::allOff);
            assert(UI::hit(200,290,count,page)==UI::refresh);
            assert(UI::hit(120,280,count,page)==-1);
            assert(UI::focusedControl(visible,count,page)==UI::allOn);
            if (count>9) {
                assert(UI::hit(175,40,count,page)==UI::previous);
                assert(UI::hit(215,40,count,page)==UI::next);
            }
        }
    }
    // Larger Home Assistant instances can have entity indices above 1000.
    const int largeCount = 1010, largePage = 111;
    const auto tile = UI::tile(1, UI::visibleCount(largeCount, largePage));
    assert(UI::hit(tile.x, tile.y, largeCount, largePage) == 1000);
    assert(UI::hit(10, 290, largeCount, largePage) != 1000);
    assert(UI::focusedControl(1, largeCount, largePage) == 1000);
    for (int count : {0, 1, 9, 10, 1000}) {
        assert(UI::hit(166, 3, count) == UI::changeView);
        assert(UI::hit(231, 22, count) == UI::changeView);
        assert(UI::hit(232, 22, count) != UI::changeView);
        assert(UI::focusedControl(UI::focusCount(count, 0) - 1, count) == UI::changeView);
    }
    assert(UI::pageCount(0)==1);
    assert(UI::pageCount(9)==1 && UI::pageCount(10)==2 && UI::pageCount(19)==3);
    assert(UI::visibleCount(10,1)==1);
    assert(UI::visibleCount(13,1)==4);
    assert(UI::tile(3,4).h==56); // Four uses 2 columns x 3 rows, not 2 x 2.
    assert(UI::clampPage(3,3)==0);
    assert(UI::clampPage(-1,20)==0);
    assert(!UI::discoveryDue(59999,0));
    assert(UI::discoveryDue(60000,0));
    assert(UI::discoveryDue(100,static_cast<unsigned long>(100-60000UL)));
}
