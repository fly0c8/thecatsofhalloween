// Rumble.h - controller vibration.
//  * Windows desktop: XInput (Xbox controllers), loaded at runtime - no extra libraries needed
//  * Web: the browser's Gamepad vibration API (through raylib)
//  * elsewhere: silently does nothing
// Deliberately includes no OS headers, so it can sit next to raylib.h.
#pragma once

namespace rumble {

// Shake every connected controller. low = heavy left motor, high = light right motor (0..1).
void Pulse(float low, float high, float seconds);

// Call once per frame: switches the motors off again when a pulse is over.
void Update(float dt);

// Stop immediately (e.g. when the game closes).
void Stop();

} // namespace rumble
