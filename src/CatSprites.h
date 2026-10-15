// CatSprites.h - procedurally painted cat sprite sheets.
#pragma once
#include "raylib.h"

enum CatFrame {
    CF_IDLE0, CF_IDLE1, CF_WALK0, CF_WALK1, CF_WALK2, CF_WALK3,
    CF_JUMP, CF_FALL, CF_ATTACK0, CF_ATTACK1, CF_HURT, CF_KO,
    CF_COUNT
};

constexpr int CAT_CELL_W = 24;
constexpr int CAT_CELL_H = 28;

struct CatSheet {
    Texture2D tex{};   // row 0: normal frames, row 1: white "hit flash" frames
};

// variant 0 = Mortimer (black cat, witch hat), 1 = Ginger (tabby pirate)
CatSheet BuildCatSheet(int variant);
void UnloadCatSheet(CatSheet& s);

// Draw a frame. (x, y) = top-left of the 24x28 cell in screen pixels.
void DrawCatFrame(const CatSheet& s, CatFrame f, int x, int y, bool faceLeft, bool flash = false, Color tint = WHITE);
