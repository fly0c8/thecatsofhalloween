#include "Input.h"
#include "raylib.h"

// Xbox layout (raylib names):  A = RIGHT_FACE_DOWN   B = RIGHT_FACE_RIGHT   X = RIGHT_FACE_LEFT
// Y = RIGHT_FACE_UP   LB/RB = *_TRIGGER_1   LT/RT = *_TRIGGER_2   View = MIDDLE_LEFT   Menu = MIDDLE_RIGHT
static constexpr int MAX_PADS = 4;
static constexpr float STICK_DEADZONE = 0.35f;

static bool PadDown(int pad, int button) { return IsGamepadButtonDown(pad, button); }
static bool PadPressed(int pad, int button) { return IsGamepadButtonPressed(pad, button); }
static float PadAxis(int pad, int axis) { return GetGamepadAxisMovement(pad, axis); }

template <typename F>
static bool AnyPad(F&& test)
{
    for (int p = 0; p < MAX_PADS; ++p)
        if (IsGamepadAvailable(p) && test(p)) return true;
    return false;
}

PlayerInput ReadLocalInput()
{
    PlayerInput in;
    in.left   = IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT);
    in.right  = IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT);
    in.jump   = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP) || IsKeyDown(KEY_SPACE);
    in.down   = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);
    in.attack = IsKeyDown(KEY_F) || IsKeyDown(KEY_X) || IsKeyDown(KEY_LEFT_CONTROL) ||
                IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_KP_0);

    for (int pad = 0; pad < MAX_PADS; ++pad) {
        if (!IsGamepadAvailable(pad)) continue;
        const float x = PadAxis(pad, GAMEPAD_AXIS_LEFT_X);
        const float y = PadAxis(pad, GAMEPAD_AXIS_LEFT_Y);
        // left stick or D-pad to move, A to jump (D-pad up too, like an Amiga joystick)
        in.left   |= x < -STICK_DEADZONE || PadDown(pad, GAMEPAD_BUTTON_LEFT_FACE_LEFT);
        in.right  |= x >  STICK_DEADZONE || PadDown(pad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
        in.down   |= y > 0.6f            || PadDown(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
        in.jump   |= PadDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || PadDown(pad, GAMEPAD_BUTTON_LEFT_FACE_UP);
        // scratch: X, B, RB or RT
        in.attack |= PadDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) || PadDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) ||
                     PadDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) || PadDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2);
    }
    return in;
}

bool AnyFirePressed()
{
    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_F) ||
        IsKeyPressed(KEY_RIGHT_CONTROL) || IsKeyPressed(KEY_LEFT_SHIFT))
        return true;
    return AnyPad([](int p) {
        return PadPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || PadPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) ||
               PadPressed(p, GAMEPAD_BUTTON_MIDDLE_RIGHT);
    });
}

bool PausePressed()
{
    return IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P) ||
           AnyPad([](int p) { return PadPressed(p, GAMEPAD_BUTTON_MIDDLE_RIGHT); });
}

bool BackPressed()
{
    return IsKeyPressed(KEY_ESCAPE) ||
           AnyPad([](int p) { return PadPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || PadPressed(p, GAMEPAD_BUTTON_MIDDLE_LEFT); });
}

bool GamepadConnected()
{
    return AnyPad([](int) { return true; });
}
