#include "PixelArt.h"
#include <cmath>
#include <cstdlib>

void PixelCanvas::Rect(int x, int y, int w, int h, char c)
{
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) Set(i, j, c);
}

void PixelCanvas::Ellipse(int cx, int cy, int rx, int ry, char c)
{
    for (int j = -ry; j <= ry; ++j)
        for (int i = -rx; i <= rx; ++i) {
            const float dx = (float)i / (rx + 0.5f), dy = (float)j / (ry + 0.5f);
            if (dx * dx + dy * dy <= 1.0f) Set(cx + i, cy + j, c);
        }
}

void PixelCanvas::Line(int x0, int y0, int x1, int y1, char c, int thickness)
{
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        for (int t = 0; t < thickness; ++t) {
            // thicken sideways relative to the dominant direction
            if (dx > -dy) Set(x0, y0 + t, c); else Set(x0 + t, y0, c);
        }
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void PixelCanvas::Blit(int x, int y, const std::vector<std::string>& rows)
{
    for (int j = 0; j < (int)rows.size(); ++j)
        for (int i = 0; i < (int)rows[j].size(); ++i)
            if (rows[j][i] != '.') Set(x + i, y + j, rows[j][i]);
}

void PixelCanvas::Replace(char from, char to)
{
    for (char& c : px_) if (c == from) c = to;
}

void PixelCanvas::Outline(char outline)
{
    std::vector<char> copy = px_;
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            if (copy[y * w_ + x] != '.') continue;
            auto filled = [&](int i, int j) {
                return i >= 0 && j >= 0 && i < w_ && j < h_ && copy[j * w_ + i] != '.';
            };
            if (filled(x - 1, y) || filled(x + 1, y) || filled(x, y - 1) || filled(x, y + 1))
                px_[y * w_ + x] = outline;
        }
}

void PixelCanvas::BakeInto(Image& img, int ox, int oy, const std::map<char, Color>& palette, bool silhouette) const
{
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            const char c = px_[y * w_ + x];
            if (c == '.') continue;
            Color col = MAGENTA;
            if (silhouette) col = (c == 'O') ? Color{ 0, 0, 0, 255 } : Color{ 255, 255, 255, 255 };
            else {
                auto it = palette.find(c);
                if (it != palette.end()) col = it->second;
            }
            ImageDrawPixel(&img, ox + x, oy + y, col);
        }
}
