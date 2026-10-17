#include "Music.h"
#include "raylib.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int SR = 44100;
constexpr float TICK_HZ = 50.0f;          // PAL vertical blank
constexpr int ROWS = 64;
constexpr int CHANNELS = 4;
constexpr int SFX_VOICES = 4;
constexpr float PI2 = 6.28318530718f;

// ---------------------------------------------------------------------------
// Wavetables (8-bit, 256 samples, just like a chip-RAM sample loop)
// ---------------------------------------------------------------------------
enum Wave { W_SINE, W_SQUARE, W_PULSE, W_SAW, W_TRI, W_ORGAN, W_BELL, W_NOISE, W_COUNT };
std::array<std::array<int8_t, 256>, W_COUNT> g_waves;

void BuildWaves()
{
    for (int w = 0; w < W_COUNT; ++w) {
        float buf[256], peak = 0.0001f;
        for (int i = 0; i < 256; ++i) {
            const float x = i / 256.0f * PI2;
            float v = 0;
            switch (w) {
            case W_SINE:   v = sinf(x); break;
            case W_SQUARE: v = i < 128 ? 1.0f : -1.0f; break;
            case W_PULSE:  v = i < 64 ? 1.0f : -1.0f; break;
            case W_SAW:    v = 1.0f - i / 128.0f; break;
            case W_TRI:    v = i < 128 ? -1.0f + i / 64.0f : 3.0f - i / 64.0f; break;
            case W_ORGAN:  v = sinf(x) + 0.6f * sinf(2 * x) + 0.35f * sinf(3 * x) + 0.25f * sinf(4 * x)
                              + 0.12f * sinf(6 * x) + 0.08f * sinf(8 * x); break;
            case W_BELL:   v = sinf(x) + 0.35f * sinf(4 * x) + 0.2f * sinf(7 * x) + 0.1f * sinf(11 * x); break;
            default: break;
            }
            buf[i] = v;
            peak = fmaxf(peak, fabsf(v));
        }
        for (int i = 0; i < 256; ++i) g_waves[w][i] = (int8_t)lrintf(buf[i] / peak * 127.0f);
    }
}

// ---------------------------------------------------------------------------
// Instruments
// ---------------------------------------------------------------------------
struct Instrument {
    Wave wave = W_SQUARE;
    float attack = 0.005f, decay = 0.2f, sustain = 0.6f, release = 0.08f;
    float volume = 0.5f;
    float vibDepth = 0, vibRate = 5, vibDelay = 0;   // semitones / Hz / s
    float glide = 0;                                 // portamento time constant (s)
    float drop = 0, dropTime = 0.03f;                // pitch drop (kick drum)
    float noiseMul = 8.0f;                           // noise sample-rate multiplier
};

enum Ins {
    I_BASS, I_ORGAN, I_THEREMIN, I_ARP, I_BELL, I_KICK, I_SNARE, I_HAT,
    I_FX_SQUARE, I_FX_PULSE, I_FX_SAW, I_FX_NOISE, I_FX_SINE, I_COUNT
};
Instrument g_ins[I_COUNT];

void BuildInstruments()
{
    Instrument& bass = g_ins[I_BASS];
    bass.wave = W_PULSE; bass.decay = 0.18f; bass.sustain = 0.35f; bass.release = 0.05f; bass.volume = 0.42f;

    Instrument& organ = g_ins[I_ORGAN];
    organ.wave = W_ORGAN; organ.attack = 0.01f; organ.decay = 0.4f; organ.sustain = 0.75f; organ.release = 0.2f;
    organ.volume = 0.36f; organ.vibDepth = 0.18f; organ.vibRate = 5.5f; organ.vibDelay = 0.18f;

    Instrument& th = g_ins[I_THEREMIN];
    th.wave = W_SINE; th.attack = 0.06f; th.decay = 0.5f; th.sustain = 0.9f; th.release = 0.25f;
    th.volume = 0.42f; th.vibDepth = 0.4f; th.vibRate = 6.5f; th.vibDelay = 0.05f; th.glide = 0.06f;

    Instrument& arp = g_ins[I_ARP];
    arp.wave = W_SQUARE; arp.decay = 0.25f; arp.sustain = 0.35f; arp.release = 0.05f; arp.volume = 0.13f;

    Instrument& bell = g_ins[I_BELL];
    bell.wave = W_BELL; bell.decay = 0.7f; bell.sustain = 0.0f; bell.release = 0.3f; bell.volume = 0.38f;
    bell.vibDepth = 0.08f; bell.vibRate = 4.0f;

    Instrument& kick = g_ins[I_KICK];
    kick.wave = W_SINE; kick.attack = 0.001f; kick.decay = 0.11f; kick.sustain = 0; kick.volume = 0.85f;
    kick.drop = 34; kick.dropTime = 0.025f;

    Instrument& snare = g_ins[I_SNARE];
    snare.wave = W_NOISE; snare.attack = 0.001f; snare.decay = 0.09f; snare.sustain = 0; snare.volume = 0.42f;
    snare.noiseMul = 6.0f;

    Instrument& hat = g_ins[I_HAT];
    hat.wave = W_NOISE; hat.attack = 0.001f; hat.decay = 0.025f; hat.sustain = 0; hat.volume = 0.2f;
    hat.noiseMul = 8.0f;

    g_ins[I_FX_SQUARE].wave = W_SQUARE;
    g_ins[I_FX_PULSE].wave = W_PULSE;
    g_ins[I_FX_SAW].wave = W_SAW;
    g_ins[I_FX_NOISE].wave = W_NOISE;
    g_ins[I_FX_SINE].wave = W_SINE;
    for (int i = I_FX_SQUARE; i <= I_FX_SINE; ++i) {
        g_ins[i].attack = 0.002f; g_ins[i].decay = 10; g_ins[i].sustain = 1; g_ins[i].release = 0.03f; g_ins[i].volume = 0.4f;
    }
}

// ---------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------
struct CurvePoint { float t, note; };

struct Voice {
    const Instrument* ins = nullptr;
    bool active = false, gate = false;
    float note = 60, target = 60;     // midi (float for glides)
    float t = 0, relT = 0, envAtRelease = 0;
    double phase = 0;
    int arpX = 0, arpY = 0;
    float vol = 1;
    uint32_t rng = 0x9E3779B9u;
    float noise = 0;
    // sfx pitch curve
    std::vector<CurvePoint> curve;
    float duration = 0;
    float sfxVib = 0;
    float pan = 0;
};

float Envelope(Voice& v, float dt)
{
    const Instrument& in = *v.ins;
    float e;
    if (v.gate) {
        if (v.t < in.attack) e = v.t / in.attack;
        else e = in.sustain + (1.0f - in.sustain) * expf(-(v.t - in.attack) / in.decay);
        v.envAtRelease = e;
    } else {
        v.relT += dt;
        e = v.envAtRelease * (1.0f - v.relT / in.release);
        if (e <= 0) { e = 0; v.active = false; }
    }
    return e;
}

int g_arpStep = 0;   // advanced every tick

float VoiceSample(Voice& v)
{
    if (!v.active || !v.ins) return 0;
    const float dt = 1.0f / SR;
    const Instrument& in = *v.ins;

    // pitch
    if (!v.curve.empty()) {
        // piecewise-linear sfx pitch curve
        float n = v.curve.back().note;
        for (size_t i = 0; i + 1 < v.curve.size(); ++i)
            if (v.t >= v.curve[i].t && v.t < v.curve[i + 1].t) {
                const float k = (v.t - v.curve[i].t) / (v.curve[i + 1].t - v.curve[i].t);
                n = v.curve[i].note + (v.curve[i + 1].note - v.curve[i].note) * k;
                break;
            }
        v.note = n;
        if (v.gate && v.t >= v.duration) { v.gate = false; v.relT = 0; }
    } else if (in.glide > 0) {
        v.note += (v.target - v.note) * (1.0f - expf(-dt / in.glide));
    } else {
        v.note = v.target;
    }
    float p = v.note;
    const int step = g_arpStep % 3;
    if (step == 1) p += (float)v.arpX;
    if (step == 2) p += (float)v.arpY;
    if (in.vibDepth > 0 && v.t > in.vibDelay) p += in.vibDepth * sinf(PI2 * in.vibRate * v.t);
    if (v.sfxVib > 0) p += v.sfxVib * sinf(PI2 * 12.0f * v.t);
    if (in.drop > 0) p += in.drop * expf(-v.t / in.dropTime);

    const float freq = 440.0f * powf(2.0f, (p - 69.0f) / 12.0f);
    float s;
    if (in.wave == W_NOISE) {
        const double prev = v.phase;
        v.phase += freq * in.noiseMul / SR;
        if ((int64_t)v.phase != (int64_t)prev) {
            v.rng ^= v.rng << 13; v.rng ^= v.rng >> 17; v.rng ^= v.rng << 5;
            v.noise = (int8_t)(v.rng & 0xFF) / 128.0f;   // 8-bit noise sample
        }
        if (v.phase > 1e6) v.phase -= 1e6;
        s = v.noise;
    } else {
        v.phase += freq * 256.0 / SR;
        if (v.phase >= 256.0) v.phase -= 256.0 * floor(v.phase / 256.0);
        s = g_waves[in.wave][(int)v.phase & 255] / 128.0f;   // no interpolation: Paula grit
    }
    const float e = Envelope(v, dt);
    v.t += dt;
    return s * e * in.volume * v.vol;
}

void NoteOn(Voice& v, const Instrument* ins, float note, float vol, bool legato)
{
    const bool slide = legato && v.active && v.gate && v.ins == ins && ins->glide > 0;
    v.ins = ins;
    v.target = note;
    v.vol = vol;
    v.curve.clear();
    v.sfxVib = 0;
    if (!slide) {
        v.note = note;
        v.t = 0;
        v.phase = 0;
    }
    v.active = true;
    v.gate = true;
    v.relT = 0;
}

void NoteOff(Voice& v)
{
    if (v.active && v.gate) { v.gate = false; v.relT = 0; }
}

// ---------------------------------------------------------------------------
// Song data
// ---------------------------------------------------------------------------
struct Cell { int note = -1; int ins = -1; int arpX = 0, arpY = 0; float vol = 1.0f; };   // note -2 = off
struct Pattern { Cell c[ROWS][CHANNELS]; };

std::vector<Pattern> g_patterns;
std::vector<int> g_order;
int g_loopTo = 1;

int ParseNote(const std::string& tok)
{
    if (tok == "-") return -1;
    if (tok == ".") return -2;
    static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    int n = semis[tok[0] - 'A'];
    size_t i = 1;
    if (i < tok.size() && tok[i] == '#') { ++n; ++i; }
    if (i < tok.size() && tok[i] == 'b') { --n; ++i; }
    const int octave = tok[i] - '0';
    return 12 * (octave + 1) + n;
}

struct Chord { int root; bool minor; };

enum Drums { DR_NONE, DR_INTRO, DR_FULL, DR_BREAK };

Pattern MakePattern(const Chord (&chords)[4], const char* melody, int leadIns, int transpose, Drums drums, bool arps = true)
{
    Pattern p;
    // bass + arps from the chords
    static const int bassRows[8] = { 0, 2, 4, 6, 8, 10, 12, 14 };
    static const int bassOff[8]  = { 0, 0, 12, 0, 0, 7, 12, 7 };
    for (int bar = 0; bar < 4; ++bar) {
        const Chord& ch = chords[bar];
        for (int k = 0; k < 8; ++k) {
            Cell& c = p.c[bar * 16 + bassRows[k]][0];
            c.note = ch.root - 12 + bassOff[k];
            c.ins = I_BASS;
            c.vol = (k == 0 || k == 4) ? 1.0f : 0.8f;
        }
        if (arps) {
            for (int r = 0; r < 16; r += 4) {
                Cell& c = p.c[bar * 16 + r][2];
                c.note = ch.root + 12;
                c.ins = I_ARP;
                c.arpX = ch.minor ? 3 : 4;
                c.arpY = 7;
                c.vol = r == 0 ? 1.0f : 0.75f;
            }
        }
        // drums
        const char* pat = "................";
        if (drums == DR_INTRO) pat = "K...H...K...H.H.";
        if (drums == DR_FULL)  pat = (bar == 3) ? "K.H.S.HKK.S.SSSS" : "K.H.S.HKK.H.S.HH";
        if (drums == DR_BREAK) pat = "K.......K.......";
        for (int r = 0; r < 16; ++r) {
            Cell& c = p.c[bar * 16 + r][3];
            switch (pat[r]) {
            case 'K': c.note = 36; c.ins = I_KICK; break;
            case 'S': c.note = 84; c.ins = I_SNARE; c.vol = (bar == 3 && r > 11) ? 0.6f + 0.1f * (r - 12) : 1.0f; break;
            case 'H': c.note = 108; c.ins = I_HAT; c.vol = (r % 4 == 2) ? 1.0f : 0.6f; break;
            default: break;
            }
        }
    }
    // lead melody
    if (melody) {
        std::istringstream ss(melody);
        std::string tok;
        int row = 0;
        while (ss >> tok && row < ROWS) {
            const int n = ParseNote(tok);
            Cell& c = p.c[row][1];
            if (n >= 0) { c.note = n + transpose; c.ins = leadIns; }
            else if (n == -2) c.note = -2;
            ++row;
        }
    }
    return p;
}

void BuildSong()
{
    const Chord Am{ 57, true }, F{ 53, false }, Dm{ 50, true }, E{ 52, false }, C{ 48, false };
    const Chord prog1[4] = { Am, F, Dm, E };
    const Chord prog2[4] = { Am, C, Dm, E };
    const Chord progB[4] = { Am, Am, F, E };

    const char* melA =
        "E5 - - - D5 - C5 - B4 - C5 - A4 - - - "
        "F5 - - - E5 - D5 - C5 - - - A4 - - - "
        "D5 - - - F5 - - - A5 - G5 - F5 - E5 - "
        "G#4 - - - B4 - - - E5 - - - - - . . ";
    const char* melB =
        "A5 - - - G5 - E5 - C5 - E5 - A4 - - - "
        "G5 - - - E5 - C5 - G4 - C5 - E5 - - - "
        "F5 - E5 - D5 - C5 - B4 - A4 - G#4 - - - "
        "E5 - - - - - - - G#4 - B4 - E5 - . . ";
    const char* bells =
        "A5 - - - - - - - E5 - - - - - - - "
        "C6 - - - - - - - B5 - - - A5 - - - "
        "C6 - - - - - - - A5 - - - F5 - - - "
        "G#5 - - - - - - - B5 - - - E6 - - - ";

    g_patterns.clear();
    g_patterns.push_back(MakePattern(prog1, nullptr, I_ORGAN, 0, DR_INTRO));        // 0 intro
    g_patterns.push_back(MakePattern(prog1, melA, I_ORGAN, 0, DR_FULL));            // 1 organ A
    g_patterns.push_back(MakePattern(prog2, melB, I_ORGAN, 0, DR_FULL));            // 2 organ B
    g_patterns.push_back(MakePattern(prog1, melA, I_THEREMIN, 0, DR_FULL));         // 3 theremin A
    g_patterns.push_back(MakePattern(prog2, melB, I_THEREMIN, 0, DR_FULL));         // 4 theremin B
    g_patterns.push_back(MakePattern(progB, bells, I_BELL, 0, DR_BREAK, false));    // 5 spooky break
    g_order = { 0, 1, 2, 1, 2, 5, 3, 4, 5 };
    g_loopTo = 1;
}

// ---------------------------------------------------------------------------
// Player state
// ---------------------------------------------------------------------------
Voice g_chan[CHANNELS];
Voice g_sfx[SFX_VOICES];
int g_sfxNext = 0;
int g_orderPos = 0, g_row = 0, g_tick = 0;
int g_speed = 7;                 // ticks per row (~107 BPM)
double g_tickAcc = 0;
std::atomic<bool> g_musicOn{ true };
std::atomic<bool> g_restart{ false };

// stereo echo for the lead channel + LED filter state
std::vector<float> g_echoL, g_echoR;
size_t g_echoPos = 0;
float g_lpL = 0, g_lpR = 0;

struct SfxRequest { Sfx s; float pan; };
std::mutex g_queueMutex;
std::vector<SfxRequest> g_queue;

AudioStream g_stream{};
bool g_streamOk = false;

void StartSfx(const Instrument* ins, std::vector<CurvePoint> curve, float vol, float pan, float vib = 0)
{
    Voice& v = g_sfx[g_sfxNext];
    g_sfxNext = (g_sfxNext + 1) % SFX_VOICES;
    v.ins = ins;
    v.curve = std::move(curve);
    v.duration = v.curve.back().t;
    v.note = v.curve.front().note;
    v.target = v.note;
    v.vol = vol;
    v.pan = pan;
    v.sfxVib = vib;
    v.t = 0; v.phase = 0; v.relT = 0;
    v.arpX = v.arpY = 0;
    v.active = v.gate = true;
}

void TriggerSfx(const SfxRequest& r)
{
    const float p = r.pan;
    switch (r.s) {
    case Sfx::Jump:  StartSfx(&g_ins[I_FX_SQUARE], { {0, 62}, {0.11f, 79} }, 0.30f, p); break;
    case Sfx::Meow:  StartSfx(&g_ins[I_FX_PULSE], { {0, 76}, {0.07f, 83}, {0.22f, 80}, {0.38f, 71} }, 0.32f, p, 0.25f); break;
    case Sfx::Hit:
        StartSfx(&g_ins[I_FX_NOISE], { {0, 96}, {0.16f, 72} }, 0.55f, p);
        StartSfx(&g_ins[I_FX_SQUARE], { {0, 52}, {0.12f, 36} }, 0.40f, p);
        break;
    case Sfx::Stomp: StartSfx(&g_ins[I_FX_SQUARE], { {0, 48}, {0.05f, 74}, {0.16f, 62} }, 0.35f, p); break;
    case Sfx::Candy: StartSfx(&g_ins[I_FX_SQUARE], { {0, 84}, {0.05f, 84}, {0.051f, 91}, {0.13f, 91} }, 0.22f, p); break;
    case Sfx::KO:    StartSfx(&g_ins[I_FX_SAW], { {0, 79}, {1.0f, 38} }, 0.36f, p, 0.6f); break;
    case Sfx::Fall:  StartSfx(&g_ins[I_FX_SINE], { {0, 88}, {0.7f, 52} }, 0.45f, p); break;
    case Sfx::Start: StartSfx(&g_ins[I_FX_SQUARE], { {0, 69}, {0.08f, 69}, {0.081f, 72}, {0.16f, 72}, {0.161f, 76}, {0.24f, 76}, {0.241f, 81}, {0.45f, 81} }, 0.3f, 0); break;
    case Sfx::Win:   StartSfx(&g_ins[I_FX_SQUARE], { {0, 72}, {0.15f, 72}, {0.151f, 76}, {0.3f, 76}, {0.301f, 79}, {0.45f, 79}, {0.451f, 84}, {0.9f, 84} }, 0.32f, 0, 0.15f); break;
    case Sfx::Smash:
        StartSfx(&g_ins[I_FX_NOISE], { {0, 92}, {0.22f, 60} }, 0.50f, p);
        StartSfx(&g_ins[I_FX_SQUARE], { {0, 60}, {0.18f, 38} }, 0.32f, p);
        break;
    case Sfx::Angry: StartSfx(&g_ins[I_FX_SAW], { {0, 40}, {0.12f, 47}, {0.28f, 38}, {0.5f, 44} }, 0.40f, p, 0.8f); break;
    case Sfx::Wand:  StartSfx(&g_ins[I_FX_SQUARE], { {0, 84}, {0.06f, 84}, {0.061f, 88}, {0.12f, 88}, {0.121f, 91}, {0.18f, 91}, {0.181f, 96}, {0.6f, 96} }, 0.26f, 0, 0.2f); break;
    case Sfx::Boost: StartSfx(&g_ins[I_FX_PULSE], { {0, 60}, {0.18f, 86} }, 0.28f, p); break;
    case Sfx::Boo:    StartSfx(&g_ins[I_FX_SINE], { {0, 74}, {0.15f, 79}, {0.7f, 62} }, 0.40f, p, 0.9f); break;
    case Sfx::Cackle: StartSfx(&g_ins[I_FX_PULSE], { {0, 86}, {0.06f, 82}, {0.061f, 87}, {0.12f, 83}, {0.121f, 88}, {0.18f, 84}, {0.181f, 89}, {0.3f, 85} }, 0.26f, p, 0.5f); break;
    case Sfx::Poof:
        StartSfx(&g_ins[I_FX_NOISE], { {0, 84}, {0.3f, 100} }, 0.35f, p);
        StartSfx(&g_ins[I_FX_SQUARE], { {0, 76}, {0.2f, 55} }, 0.25f, p);
        break;
    }
}

void ProcessRow()
{
    const Pattern& pat = g_patterns[g_order[g_orderPos]];
    for (int ch = 0; ch < CHANNELS; ++ch) {
        const Cell& c = pat.c[g_row][ch];
        Voice& v = g_chan[ch];
        if (c.note == -2) { NoteOff(v); continue; }
        if (c.note < 0) continue;
        if (!g_musicOn) continue;
        NoteOn(v, &g_ins[c.ins], (float)c.note, c.vol, ch == 1);
        v.arpX = c.arpX;
        v.arpY = c.arpY;
    }
}

void Tick()
{
    ++g_arpStep;
    if (g_restart.exchange(false)) {
        g_orderPos = 0; g_row = 0; g_tick = 0;
        for (auto& v : g_chan) v.active = false;
    }
    if (g_tick == 0) ProcessRow();
    if (++g_tick >= g_speed) {
        g_tick = 0;
        if (++g_row >= ROWS) {
            g_row = 0;
            if (++g_orderPos >= (int)g_order.size()) g_orderPos = g_loopTo;
        }
    }
}

void StreamCallback(void* buffer, unsigned int frames)
{
    music::Render((short*)buffer, frames);
}

} // namespace

namespace music {

void Init()
{
    BuildWaves();
    BuildInstruments();
    BuildSong();
    g_echoL.assign((size_t)(SR * 0.3f), 0.0f);
    g_echoR.assign((size_t)(SR * 0.3f), 0.0f);
    if (IsAudioDeviceReady()) {
        SetAudioStreamBufferSizeDefault(2048);
        g_stream = LoadAudioStream(SR, 16, 2);
        SetAudioStreamCallback(g_stream, StreamCallback);
        PlayAudioStream(g_stream);
        g_streamOk = true;
    }
}

void Shutdown()
{
    if (g_streamOk) {
        StopAudioStream(g_stream);
        UnloadAudioStream(g_stream);
        g_streamOk = false;
    }
}

void Update() {}

void SetEnabled(bool on)
{
    g_musicOn = on;
    if (!on) for (auto& v : g_chan) NoteOff(v);
}
bool Enabled() { return g_musicOn; }
void Restart() { g_restart = true; }

void Play(Sfx s, float pan)
{
    std::lock_guard<std::mutex> lock(g_queueMutex);
    g_queue.push_back({ s, pan });
}

void Render(short* out, unsigned frames)
{
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        for (auto& r : g_queue) TriggerSfx(r);
        g_queue.clear();
    }
    const double samplesPerTick = SR / TICK_HZ;
    // Paula panning: channels 0 & 3 left, 1 & 2 right (softened a little)
    const float panL[CHANNELS] = { 0.8f, 0.25f, 0.25f, 0.8f };
    const float panR[CHANNELS] = { 0.25f, 0.8f, 0.8f, 0.25f };

    for (unsigned i = 0; i < frames; ++i) {
        if (g_tickAcc <= 0) { Tick(); g_tickAcc += samplesPerTick; }
        g_tickAcc -= 1.0;

        float l = 0, r = 0, leadOut = 0;
        for (int ch = 0; ch < CHANNELS; ++ch) {
            const float s = VoiceSample(g_chan[ch]);
            l += s * panL[ch];
            r += s * panR[ch];
            if (ch == 1) leadOut = s;
        }
        // haunted-house echo on the lead, bounced to the opposite side
        const float eL = g_echoL[g_echoPos], eR = g_echoR[g_echoPos];
        g_echoL[g_echoPos] = leadOut * 0.5f + eR * 0.35f;
        g_echoR[g_echoPos] = eL * 0.6f;
        g_echoPos = (g_echoPos + 1) % g_echoL.size();
        l += eL * 0.6f;
        r += eR * 0.6f;

        for (Voice& v : g_sfx) {
            const float s = VoiceSample(v);
            l += s * (1.0f - fmaxf(0.0f, v.pan)) * 0.8f;
            r += s * (1.0f + fminf(0.0f, v.pan)) * 0.8f;
        }

        // Amiga "LED" low-pass filter
        g_lpL += (l - g_lpL) * 0.42f;
        g_lpR += (r - g_lpR) * 0.42f;
        const float outL = fmaxf(-1.0f, fminf(1.0f, g_lpL * 0.7f));
        const float outR = fmaxf(-1.0f, fminf(1.0f, g_lpR * 0.7f));
        out[2 * i] = (short)(outL * 32000);
        out[2 * i + 1] = (short)(outR * 32000);
    }
}

} // namespace music
