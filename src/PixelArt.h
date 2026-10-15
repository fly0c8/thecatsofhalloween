// PixelArt.h - tiny "Deluxe Paint" in code.
// Sprites are painted into a grid of palette letters, get an automatic black
// outline, and are then baked into a texture. No external image files needed.
#pragma once
#include "raylib.h"
#include <string>
#include <vector>
#include <map>

class PixelCanvas {
public:
    PixelCanvas(int w, int h) : w_(w), h_(h), px_(w * h, '.') {}

    int W() const { return w_; }
    int H() const { return h_; }
    char Get(int x, int y) const { return (x < 0 || y < 0 || x >= w_ || y >= h_) ? '.' : px_[y * w_ + x]; }
    void Set(int x, int y, char c) { if (x >= 0 && y >= 0 && x < w_ && y < h_) px_[y * w_ + x] = c; }

    void Rect(int x, int y, int w, int h, char c);
    void Ellipse(int cx, int cy, int rx, int ry, char c);
    void Line(int x0, int y0, int x1, int y1, char c, int thickness = 1);
    // Copy an ASCII picture (rows of equal length, '.' = transparent)
    void Blit(int x, int y, const std::vector<std::string>& rows);
    // Replace colour 'from' by 'to' everywhere
    void Replace(char from, char to);
    // Every empty pixel touching a filled one becomes 'O' (outline)
    void Outline(char outline = 'O');

    // Draw into an Image at (ox, oy) using a letter -> colour map
    void BakeInto(Image& img, int ox, int oy, const std::map<char, Color>& palette, bool silhouette = false) const;

private:
    int w_, h_;
    std::vector<char> px_;
};
