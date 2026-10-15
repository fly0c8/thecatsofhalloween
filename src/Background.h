// Background.h - copper-gradient sky, moon and parallax hills.
#pragma once
#include "raylib.h"

class Background {
public:
    void Load();
    void Unload();
    // cam = view camera in world pixels, worldH = world height in pixels
    void Draw(Vector2 cam, int viewW, int viewH, float worldW, float worldH, float time) const;

private:
    Texture2D moon_{};
};
