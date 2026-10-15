// Music.h - a tiny 4-channel "ProTracker" style synth.
// Instruments are 8-bit wavetables, the sequencer ticks at 50 Hz (PAL VBlank),
// channels are hard-panned like Paula, and there is an "LED" low-pass filter.
// Sound effects are mixed through the same engine.
#pragma once

enum class Sfx { Jump, Meow, Hit, Stomp, Candy, KO, Fall, Start, Win };

namespace music {

void Init();                 // after InitAudioDevice()
void Shutdown();
void Update();               // call once per frame (keeps the stream fed on all platforms)

void SetEnabled(bool on);    // music on/off (sfx stay on)
bool Enabled();
void Restart();              // jump back to the start of the song

void Play(Sfx s, float pan = 0.0f);   // pan -1 (left) .. +1 (right)

// Render audio offline (interleaved stereo 16-bit) - handy for testing.
void Render(short* out, unsigned frames);

} // namespace music
