#pragma once
#include <windows.h>
#include "motion.hpp"
#include <map>

class ListScroll {
public:
    void attach(HWND list, HWND rail);
    void draw(HDC dc) const;
    struct Appearance { double hover,selection; };
    Appearance appearance(int row,bool selected);
    void settings_changed();
    int hovered_row=-1;
private:
    HWND list=nullptr, rail=nullptr;
    bool hot=false;
    int grab=-1, wheel_delta=0;
    bool animate=true,ticking=false;
    motion::Tween rail_hover;
    struct Row {motion::Tween hover,selection;};
    std::map<int,Row> rows;
    struct Range { int length, thumb, offset, max_top; };
    Range range() const;
    void move(int top);
    void hover(int row, bool over);
    void wake();
    void frame();
    void settle();
    void invalidate(int row);
    static LRESULT CALLBACK list_proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
    static LRESULT CALLBACK rail_proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
};
