#include "CatSprites.h"
#include "Amiga.h"
#include "PixelArt.h"
#include <map>
#include <string>
#include <vector>

using namespace amiga;

// Head, 14x14, looking to the right. Letters:
//  B fur  b fur shade  s stripe  W eye white  P pupil  N nose  E inner ear
//  M mouth  T tooth  R tongue
static const std::vector<std::string> HEAD_NORMAL = {
    ".B.........B..",
    ".BB.......BB..",
    ".BEB.....BEB..",
    ".BEBBsBsBBEBB.",
    "BBBBBBsBBBBBBB",
    "BBWWWBBBWWWWBB",
    "BWWWWWBWWWWWWB",
    "BWWPPWBWWWWPPB",
    "BBWPPBBWWWWPPB",
    "BBBBBBBBWWWWBB",
    "BBBBBBBBBBBNNB",
    ".BBBBMMMMMMBB.",
    "..BBBBBTBBBB..",
    "...BBBBBBBB...",
};
static const std::vector<std::string> HEAD_BLINK = {
    ".B.........B..",
    ".BB.......BB..",
    ".BEB.....BEB..",
    ".BEBBsBsBBEBB.",
    "BBBBBBsBBBBBBB",
    "BBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBB",
    "BPPPPBBPPPPPPB",
    "BBBBBBBBBBBBBB",
    "BBBBBBBBBBBBBB",
    "BBBBBBBBBBBNNB",
    ".BBBBMMMMMMBB.",
    "..BBBBBTBBBB..",
    "...BBBBBBBB...",
};
static const std::vector<std::string> HEAD_ANGRY = {
    ".B.........B..",
    ".BB.......BB..",
    ".BEB.....BEB..",
    ".BEBBsBsBBEBB.",
    "BPPBBBsBBBPPPB",
    "BBWPPBBBPPWWBB",
    "BWWWWPBWWWWWWB",
    "BWWWPPBWWWWPPB",
    "BBWPPBBWWWWPPB",
    "BBBBBBBBWWWWBB",
    "BBBBBBBBBBBNNB",
    ".BBBMMMMMMMMB.",
    "..BBTMRRRMTB..",
    "...BBMMMMBB...",
};
static const std::vector<std::string> HEAD_HURT = {
    ".B.........B..",
    ".BB.......BB..",
    ".BEB.....BEB..",
    ".BEBBsBsBBEBB.",
    "BBBBBBsBBBBBBB",
    "BPBBBPBPBBBBPB",
    "BBPBPBBBPBBPBB",
    "BBBPBBBBBPPBBB",
    "BBPBPBBBPBBPBB",
    "BPBBBPBPBBBBPB",
    "BBBBBBBBBBBNNB",
    ".BBBBBMMMMBBB.",
    "..BBBBMRRMBB..",
    "...BBBBRRBB...",
};

// Witch hat for Mortimer (placed so the brim sits on the head)
static const std::vector<std::string> WITCH_HAT = {
    "...........HH.",
    "..........HH..",
    ".........HHH..",
    "........HHHH..",
    ".......HHHHH..",
    "......HHHHHH..",
    "......YYYYYY..",
    "...HHHHHHHHHHH",
    "....HHHHHHHHH.",
};

enum HeadKind { HK_NORMAL, HK_BLINK, HK_ANGRY, HK_HURT };

struct LegPose { int fx, fy; };   // foot position for each of the 4 legs

static void PaintCat(PixelCanvas& c, int variant, CatFrame f)
{
    // ---------- choose pose -------------------------------------------------
    HeadKind head = HK_NORMAL;
    int headX = 9, headY = 5, bodyY = 20;
    // hips: back legs x=6,8  front legs x=14,16 at y=23
    LegPose legs[4] = { {6, 27}, {8, 27}, {14, 27}, {16, 27} };
    std::vector<std::pair<int, int>> tail = { {5, 19}, {2, 15}, {3, 11}, {1, 8}, {2, 6} };
    bool paw = false, pawRaised = false, armsUp = false;

    switch (f) {
    case CF_IDLE0: break;
    case CF_IDLE1: head = HK_BLINK; tail = { {5, 19}, {2, 15}, {2, 11}, {3, 8}, {5, 7} }; break;
    case CF_WALK0:
        legs[0] = {4, 27}; legs[1] = {10, 27}; legs[2] = {12, 27}; legs[3] = {18, 27};
        tail = { {5, 19}, {2, 16}, {1, 12}, {2, 9}, {4, 8} }; headY = 5; break;
    case CF_WALK1:
        legs[1] = {8, 25}; legs[2] = {15, 25}; headY = 4; bodyY = 19;
        tail = { {5, 19}, {2, 15}, {2, 11}, {1, 8}, {2, 6} }; break;
    case CF_WALK2:
        legs[0] = {9, 27}; legs[1] = {5, 27}; legs[2] = {17, 27}; legs[3] = {13, 27};
        tail = { {5, 19}, {2, 16}, {2, 12}, {4, 9}, {5, 7} }; break;
    case CF_WALK3:
        legs[0] = {6, 25}; legs[3] = {17, 25}; headY = 4; bodyY = 19;
        tail = { {5, 19}, {2, 15}, {2, 11}, {1, 8}, {2, 6} }; break;
    case CF_JUMP:
        legs[0] = {2, 25}; legs[1] = {4, 26}; legs[2] = {19, 24}; legs[3] = {21, 23};
        tail = { {5, 19}, {2, 17}, {0, 14}, {0, 11} }; headY = 4; break;
    case CF_FALL:
        legs[0] = {5, 27}; legs[1] = {9, 27}; armsUp = true;
        tail = { {5, 19}, {3, 14}, {4, 9}, {3, 5}, {1, 4} }; break;
    case CF_ATTACK0:
        head = HK_ANGRY; pawRaised = true; legs[3] = {16, 27};
        tail = { {5, 19}, {1, 16}, {0, 12}, {1, 9} }; break;
    case CF_ATTACK1:
        head = HK_ANGRY; paw = true; headX = 10;
        legs[0] = {4, 27}; legs[1] = {7, 27};
        tail = { {5, 19}, {1, 17}, {0, 13}, {1, 10} }; break;
    case CF_HURT:
        head = HK_HURT; headY = 6; bodyY = 21;
        legs[0] = {3, 27}; legs[1] = {7, 27}; legs[2] = {15, 27}; legs[3] = {20, 27};
        tail = { {5, 20}, {1, 18}, {0, 14}, {1, 11} }; break;
    default: break;
    }

    if (f == CF_KO) {
        // Flat on its back, legs in the air, tongue out. Classic.
        c.Ellipse(11, 23, 8, 3, 'B');
        c.Rect(4, 24, 15, 2, 'b');
        c.Line(6, 21, 5, 16, 'B', 2);
        c.Line(9, 21, 10, 16, 'B', 2);
        c.Line(13, 21, 13, 15, 'B', 2);
        c.Line(16, 21, 18, 16, 'B', 2);
        c.Line(3, 23, 0, 21, 'B', 2);
        // head on its side
        c.Ellipse(19, 22, 4, 4, 'B');
        c.Set(18, 20, 'P'); c.Set(20, 20, 'P'); c.Set(19, 21, 'P'); c.Set(18, 22, 'P'); c.Set(20, 22, 'P');
        c.Set(21, 24, 'R'); c.Set(22, 25, 'R'); c.Set(22, 24, 'R');
        c.Set(16, 18, 'B'); c.Set(16, 17, 'E'); c.Set(22, 18, 'B');
        if (variant == 1) { c.Set(18, 20, 'P'); c.Rect(17, 20, 3, 2, 'P'); }
        return;
    }

    // ---------- tail (behind everything) -------------------------------------
    for (size_t i = 0; i + 1 < tail.size(); ++i)
        c.Line(tail[i].first, tail[i].second, tail[i + 1].first, tail[i + 1].second,
               (variant == 1 && (i & 1)) ? 's' : 'B', 2);

    // ---------- legs -----------------------------------------------------------
    const int hips[4] = { 6, 8, 14, 16 };
    for (int i = 0; i < 4; ++i) {
        if (armsUp && i >= 2) continue;
        if (paw && i == 3) continue;
        if (pawRaised && i == 3) continue;
        c.Line(hips[i], bodyY + 2, legs[i].fx, legs[i].fy, i == 1 || i == 3 ? 'B' : 'b', 2);
    }

    // ---------- body -----------------------------------------------------------
    c.Ellipse(11, bodyY, 7, 4, 'B');
    for (int x = 5; x <= 17; ++x) { c.Set(x, bodyY + 3, 'b'); }
    if (variant == 1) { // tabby stripes
        for (int x : {7, 10, 13}) { c.Set(x, bodyY - 3, 's'); c.Set(x, bodyY - 2, 's'); c.Set(x + 1, bodyY - 1, 's'); }
    }

    // ---------- head -------------------------------------------------------------
    const std::vector<std::string>* h = &HEAD_NORMAL;
    if (head == HK_BLINK) h = &HEAD_BLINK;
    if (head == HK_ANGRY) h = &HEAD_ANGRY;
    if (head == HK_HURT)  h = &HEAD_HURT;
    c.Blit(headX, headY, *h);

    // ---------- arms / paw -----------------------------------------------------
    if (armsUp) {   // panicking while falling
        c.Line(14, bodyY - 1, 17, bodyY - 7, 'B', 2);
        c.Line(16, bodyY, 21, bodyY - 5, 'B', 2);
    }
    if (pawRaised) {
        c.Line(16, bodyY + 1, 20, bodyY - 3, 'B', 2);
        c.Set(21, bodyY - 4, 'C'); c.Set(21, bodyY - 2, 'C');
    }
    if (paw) {   // full swipe, claws out
        c.Line(16, bodyY + 1, 21, bodyY - 1, 'B', 2);
        c.Set(23, bodyY - 2, 'C'); c.Set(23, bodyY, 'C'); c.Set(22, bodyY - 3, 'C');
    }

    // ---------- accessories -----------------------------------------------------
    if (variant == 0) {
        c.Blit(headX - 1, headY - 5, WITCH_HAT);
    } else {
        // pirate eyepatch over the left eye + strap
        if (head != HK_HURT) {
            for (int y = 5; y <= 8; ++y)
                for (int x = 1; x <= 5; ++x) c.Set(headX + x, headY + y, 'P');
        }
        c.Line(headX + 0, headY + 6, headX + 4, headY + 3, 'P');
        c.Line(headX + 5, headY + 5, headX + 12, headY + 2, 'P');
        // little red bandana knot on the back of the head
        c.Set(headX - 1, headY + 4, 'R'); c.Set(headX - 2, headY + 5, 'R'); c.Set(headX - 1, headY + 6, 'R');
    }
}

static std::map<char, Color> CatPalette(int variant)
{
    if (variant == 0) {   // Mortimer: black cat, lime googly eyes, purple witch hat
        return {
            {'O', RGB12(0x000)}, {'B', RGB12(0x335)}, {'b', RGB12(0x224)}, {'s', RGB12(0x335)},
            {'W', RGB12(0xCF4)}, {'P', RGB12(0x000)}, {'N', RGB12(0xF8A)}, {'E', RGB12(0xC59)},
            {'M', RGB12(0x500)}, {'T', RGB12(0xFFF)}, {'R', RGB12(0xE44)}, {'C', RGB12(0xFFF)},
            {'H', RGB12(0x63A)}, {'Y', RGB12(0xF80)},
        };
    }
    return {                  // Ginger: orange tabby pirate
        {'O', RGB12(0x100)}, {'B', RGB12(0xF92)}, {'b', RGB12(0xC52)}, {'s', RGB12(0xA30)},
        {'W', RGB12(0xFFF)}, {'P', RGB12(0x000)}, {'N', RGB12(0xF8A)}, {'E', RGB12(0xFAB)},
        {'M', RGB12(0x500)}, {'T', RGB12(0xFFF)}, {'R', RGB12(0xE22)}, {'C', RGB12(0xFFF)},
        {'H', RGB12(0x000)}, {'Y', RGB12(0xFF0)},
    };
}

CatSheet BuildCatSheet(int variant)
{
    Image img = GenImageColor(CAT_CELL_W * CF_COUNT, CAT_CELL_H * 2, BLANK);
    const auto palette = CatPalette(variant);
    for (int f = 0; f < CF_COUNT; ++f) {
        PixelCanvas c(CAT_CELL_W, CAT_CELL_H);
        PaintCat(c, variant, (CatFrame)f);
        c.Outline('O');
        c.BakeInto(img, f * CAT_CELL_W, 0, palette);
        c.BakeInto(img, f * CAT_CELL_W, CAT_CELL_H, palette, true);
    }
    CatSheet s;
    s.tex = LoadTextureFromImage(img);
    SetTextureFilter(s.tex, TEXTURE_FILTER_POINT);
    UnloadImage(img);
    return s;
}

void UnloadCatSheet(CatSheet& s)
{
    if (s.tex.id) UnloadTexture(s.tex);
    s.tex = {};
}

void DrawCatFrame(const CatSheet& s, CatFrame f, int x, int y, bool faceLeft, bool flash, Color tint)
{
    Rectangle src{ (float)(f * CAT_CELL_W), flash ? (float)CAT_CELL_H : 0.0f,
                   (float)(faceLeft ? -CAT_CELL_W : CAT_CELL_W), (float)CAT_CELL_H };
    DrawTextureRec(s.tex, src, Vector2{ (float)x, (float)y }, tint);
}
