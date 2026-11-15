#pragma once
#include <windows.h>
#include <filesystem>
#include <vector>

// Premultiplied BGRA pixels, bounded to display resolution.
struct PreviewImage {
    int width=0, height=0;
    std::vector<unsigned char> pixels;
    bool load(const std::filesystem::path& file);
    void draw(HDC dc, RECT area, BYTE opacity=255) const;
    static PreviewImage capture(HWND window,int inset);
};
