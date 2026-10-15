// Input.h - per-player input snapshot.
// The game only ever looks at a PlayerInput, never at the keyboard directly,
// so later a remote player's input can arrive over the websocket instead.
#pragma once
#include <cstdint>

struct PlayerInput {
    bool left = false;
    bool right = false;
    bool jump = false;    // held
    bool down = false;
    bool attack = false;  // held

    uint8_t Pack() const
    {
        return (uint8_t)(left | (right << 1) | (jump << 2) | (down << 3) | (attack << 4));
    }
    static PlayerInput Unpack(uint8_t b)
    {
        PlayerInput i;
        i.left = b & 1; i.right = (b >> 1) & 1; i.jump = (b >> 2) & 1;
        i.down = (b >> 3) & 1; i.attack = (b >> 4) & 1;
        return i;
    }
};

// The local player (one cat per client):
//   keyboard: move A/D or arrows, jump W/Up/Space, down S/Down, scratch F/X/Ctrl/Shift
//   Xbox controller: left stick / D-pad move, A jump, X/B/RB/RT scratch
PlayerInput ReadLocalInput();

// Menus: "fire" = Space/Enter/F or A/X/Menu, pause = Esc/P or Menu, back = Esc or B/View
bool AnyFirePressed();
bool PausePressed();
bool BackPressed();
bool GamepadConnected();
