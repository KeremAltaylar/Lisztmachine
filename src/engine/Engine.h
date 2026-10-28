#pragma once
#include "Composer.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>

namespace liszt {

// One Surge XT Effects instance: its type and parameters, written exactly as Surge
// displays them ("Reverb Decay Time" = "7.0 s"). Every value is read back after setting.
struct FxParam { const char* name; const char* value; };
struct FxSpec
{
    const char* type;
    std::vector<FxParam> params;
};

// The sound palette: every patch a part may play, darkest first. Read from a text file
// (sounds/berlin.txt) so the sounds can be changed without touching code.
struct PaletteEntry
{
    juce::String patch; // Surge XT factory patch, "Category/Name"
    float gainDb;       // this patch's level in the mix
    float brightness;   // measured spectral centroid, Hz: the dark-to-bright order
};
// A per-track echo, ping-pong between left and right: its time in beats, how much comes back,
// and how much is heard.
// `throws`: only the hits the composer throws go into the echo, not the whole track.
struct InsertDelay { float beats = 0, feedback = 0, mix = 0; bool throws = false; };
// The drums' processing, each with its own mix (0 = dry): a crusher (bits, and samples held so the
// rate drops), a compressor for punch (a slow attack lets the transient through), how deep the kick ducks it.
struct CrushSpec { float bits = 16, hold = 1, mix = 0; };
struct CompSpec { float thresholdDb = 0, ratio = 1, attackMs = 10, releaseMs = 120, mix = 0; };
// A per-track drive, like an amp or a tape pushed hard: how hard (0..1) and where its top rolls off.
struct DriveSpec { float amount = 0, toneHz = 6000; };
struct Palette
{
    std::string scene = "berlin";
    const SceneInfo* info = &sceneInfo("berlin");
    std::vector<PaletteEntry> parts[kMaxTracks];
    float trackGainDb[kMaxTracks] = {}; // "gain <track> dB": the balance between tracks
    float masterDb = 0;                 // "master dB": drive into the master's soft clipper
    InsertDelay delays[kMaxTracks];     // "delay <track> beats feedback mix"
    DriveSpec drives[kMaxTracks];       // "drive <track> amount toneHz"
    CrushSpec crushes[kMaxTracks];      // "crush <track> bits hold mix"
    CompSpec comps[kMaxTracks];         // "comp <track> thresholdDb ratio attackMs releaseMs mix"
    float duckDepth[kMaxTracks];        // "duck <track> depth": over the mix stage's own; -1 keeps it
    // Lines: "part gain bright Category/Patch" (a Surge patch, or internal/click|blip|sub),
    // "voices ..." for every track of the voice group, "gain ...", "delay ...".
    juce::String load(const juce::File& file, const std::string& scene = "berlin"); // returns an error or empty
};
juce::File findPalette(const juce::String& name); // sounds/<name>.txt, searched upward from the executable

// When each part changes sound, and to which palette entry.
struct SoundChange { int bar, track, entry; };
// Calm sections take darker patches, the peak the brightest; parts change at alternate
// sections and a few bars apart, never all at once. Deterministic per seed.
std::vector<SoundChange> planSounds(const Composer& composer, const Palette& palette, uint64_t seed);

// A sampled acoustic kit (Karoryfer's Gogodze Phu Vol II, CC0; tools/fetch_drums.py fetches it).
// Each drum has several loudness layers and four alternate hits per layer: the velocity picks the
// layer, the hits rotate, so no two in a row are the same. The track decides which drum a note is:
//   kick: the kick · snare: from note 60 up the rim, a soft hit the edge, else the centre
//   hats: note 46 open (choked by the next closed), else closed · perc: toms, low/mid/high by pitch
// One-shots: a note-off does nothing. Loaded once and shared by every track that uses it.
class DrumSampler
{
public:
    enum Part { KickPart, SnarePart, HatsPart, TomPart };
    // Message thread. Empty on success; an error if the kit is not on this machine.
    static juce::String loadKit();
    static bool kitLoaded();
    void prepare(double sampleRate, Part part);
    void render(const juce::MidiBuffer& midi, float* left, float* right, int numSamples);
    void allOff() { for (auto& v : voices_) v.sample = nullptr; }

private:
    struct Voice { const std::vector<float>* sample = nullptr; double pos = 0; float gain = 0, fade = 1, fadeStep = 0; bool open = false; };
    void start(int note, float velocity);
    void run(float* l, float* r, int from, int to);
    std::array<Voice, 24> voices_;
    double rate_ = 1;    // the kit's 44.1 kHz in this sample rate's steps
    Part part_ = KickPart;
    int rr_ = 0, next_ = 0;
    float chokeStep_ = 0;
};
juce::File findDrumKit(); // Gogodze/: inside the app, next to the executable, or ~/LisztmachineSounds

// The engine's own instrument for impulses: no plugin, sample-exact, nothing to detune.
//   click: under note 96 a sine impulse (pitch +36 semitones, up to 15 kHz) decaying in 2-6 ms;
//          from 96 up a noise tick, 1.5 ms
//   blip:  a sine at the note's pitch, 1 ms attack, 30-90 ms decay by velocity
//   sub:   a held sine (and a little of its octave, for small speakers), 40 ms in, 400 ms out
//   snare: a falling tone body and a burst of noise; from note 60 up a short rimshot
class InternalSynth
{
public:
    enum Kind { Click, Blip, Sub, Kick, Hat, Clap, Snare, Kick808, Metal, Zap };
    void prepare(double sampleRate, Kind kind) { sr_ = sampleRate; kind_ = kind; for (auto& v : voices_) v.note = -1; }
    void render(const juce::MidiBuffer& midi, float* left, float* right, int numSamples);
    void allOff() { for (auto& v : voices_) v.note = -1; }

private:
    struct Voice { int note = -1; double phase = 0, phase2 = 0, phase3 = 0, inc = 0, env = 0, gain = 0, decay = 0, attack = 0, release = 0; float panL = 0.7f, panR = 0.7f; bool noise = false, held = false; float lastNoise = 0, lastNoise2 = 0; int64_t age = 0; };
    void start(int note, float velocity);
    void stopNote(int note);
    void run(float* l, float* r, int from, int to);
    std::array<Voice, 64> voices_;
    double sr_ = 48000;
    Kind kind_ = Click;
    uint32_t seed_ = 12345;
    int flip_ = 0;
};

// The ping-pong echo of one track: the dry signal and its echoes, darkened and thinned a
// little each time round so long feedback stays clear instead of muddy.
class FeedbackDelay
{
public:
    void prepare(double sampleRate);
    void set(double seconds, float feedback, float mix, bool throwsOnly = false);
    void process(float* left, float* right, int numSamples);
    void throwFor(int at, int samples) { throwAt_ = at; throwLeft_ = samples; } // in this block, from sample `at`

private:
    std::vector<float> l_, r_;
    int w_ = 0, d_ = 1;
    float fb_ = 0, mix_ = 0, lpL_ = 0, lpR_ = 0, hpL_ = 0, hpR_ = 0, lpA_ = 0.4f, hpA_ = 0.02f;
    bool throwsOnly_ = false;
    float send_ = 1, sendStep_ = 0.01f;
    int throwAt_ = -1, throwLeft_ = 0;
};

// The shared buses. Delay times are set from the tempo when a concert starts.
inline const FxSpec kDelayBus { "Delay", { { "Feedback/EQ Feedback", "68 %" }, { "Feedback/EQ Crossfeed", "35 %" },
                                           { "Feedback/EQ Low Cut", "180 Hz" }, { "Feedback/EQ High Cut", "2200 Hz" },
                                           { "Modulation Depth", "8 cents" }, { "Output Mix", "100 %" } } };
inline const FxSpec kReverbBus { "Reverb 2", { { "Pre-Delay Pre-Delay", "40 ms" }, { "Reverb Room Size", "85 %" },
                                               { "Reverb Decay Time", "7 s" }, { "Reverb Modulation", "60 %" },
                                               { "EQ LF Damping", "10 %" }, { "EQ HF Damping", "65 %" },
                                               { "Output Mix", "100 %" } } };
constexpr float kDelayToReverb = 0.35f;

juce::File findSurgeEffects();
// Sets a parameter from the text Surge displays, then reads it back. Returns an error or empty.
juce::String setParameterText(juce::AudioPluginInstance& p, const juce::String& name, const juce::String& value);
std::unique_ptr<juce::AudioPluginInstance> createSurgeEffect(const FxSpec& spec, double sampleRate, int maxBlock,
                                                             juce::String& error);

juce::File findSurgePlugin();
juce::File findSurgePatch(const juce::String& categoryAndName);

// Loads a Surge XT factory patch (.fxp) into a hosted Surge instance and reads the
// state back to confirm it took. Returns an error, or empty on success.
juce::String loadSurgePatch(juce::AudioPluginInstance& surge, const juce::File& fxp);
// Pure tuning for a loaded patch: one voice per oscillator, no detune or drift, whole-semitone
// oscillators, no chorus-type effects.
void purify(juce::AudioPluginInstance& surge, bool roundTuning = true);

std::unique_ptr<juce::AudioPluginInstance> createSurge(double sampleRate, int maxBlock, juce::String& error);

// Lookahead peak limiter. The signal is delayed 1.5 ms so the gain is already down when
// a peak arrives; below the threshold it is exactly unity. (juce::dsp::Limiter was measured
// adding ~5 dB of gain, which is a level control hiding inside a safety stage.)
class PeakLimiter
{
public:
    void prepare(double sampleRate, float thresholdDb, float releaseMs);
    void reset();
    void process(float* left, float* right, int numSamples);
    float maxReductionDb = 0; // deepest gain reduction so far
    int64_t clipped = 0;      // samples the final safety clamp had to touch

private:
    std::vector<float> need_, delayL_, delayR_;
    int look_ = 72, at_ = 0;
    float threshold_ = 0.89f, attack_ = 0.07f, release_ = 0.9998f, gain_ = 1;
};

// RBJ 12 dB/octave high-pass: clears the low end of everything that is not kick or bass.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void highpass(double sampleRate, double hz);
    float process(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// The master bus before the limiter: a gentle stereo-linked compressor (2:1, soft knee) that
// glues the mix, then makeup gain into a tanh soft clipper with its ceiling at -1 dBFS, which
// is where the loudness comes from. ponytail: no oversampling, so a hard drive aliases a little;
// oversample the clipper if the hats ever sound gritty.
// Gnarl without fizz: an asymmetric tanh (odd and even harmonics, like a valve) run at twice the
// sample rate, then a gentle low-pass at the tone frequency, and a DC blocker for the asymmetry.
// The level is held about where it was, so the balance between tracks stays.
class Drive
{
public:
    void prepare(double sampleRate, const DriveSpec& spec);
    bool on() const { return k_ > 1; }
    float process(float x, int channel);

private:
    float k_ = 1, comp_ = 1, lpA_ = 1, bias_ = 0.2f;
    float last_[2] = {}, lp_[2] = {}, dcIn_[2] = {}, dcOut_[2] = {};
    float shape(float x) const { return std::tanh(k_ * x + bias_) - std::tanh(bias_); }
};

class Crusher
{
public:
    void prepare(const CrushSpec& s) { spec_ = s; levels_ = std::pow(2.0f, s.bits - 1); count_ = 0; }
    bool on() const { return spec_.mix > 0; }
    void process(float& l, float& r)
    {
        if (count_++ % std::max(1, int(spec_.hold)) == 0) { hl_ = std::round(l * levels_) / levels_; hr_ = std::round(r * levels_) / levels_; }
        l += (hl_ - l) * spec_.mix;
        r += (hr_ - r) * spec_.mix;
    }

private:
    CrushSpec spec_;
    float levels_ = 32768, hl_ = 0, hr_ = 0;
    int64_t count_ = 0;
};

// A stereo-linked compressor with a parallel mix: the squashed copy under the dry one ("New York").
class Compressor
{
public:
    void prepare(double sampleRate, const CompSpec& s);
    bool on() const { return spec_.mix > 0; }
    void process(float& l, float& r);
    float maxReductionDb = 0;

private:
    CompSpec spec_;
    float env_ = 0, attack_ = 0, release_ = 0, makeup_ = 1;
};

class GlueBus
{
public:
    void prepare(double sampleRate);
    void reset() { env_ = 0; }
    void process(float* left, float* right, int numSamples);
    float thresholdDb = -18, ratio = 2, makeupDb = 0;
    float maxReductionDb = 0; // the compressor's deepest reduction so far

private:
    float env_ = 0, attack_ = 0, release_ = 0;
};

class Engine
{
public:
    Engine();
    ~Engine();

    // Message thread. Creates two Surge instances per part and the shared buses.
    juce::String load(double sampleRate, int maxBlock, const Palette& palette);
    // Message thread. A new concert from this seed, from bar 0 or from any later bar.
    void start(uint64_t seed, int startBar = 0);
    std::string scene = "berlin"; // which composer start() uses: the palette's scene
    float feedbackOverride = -1;  // >= 0: every track's echo feeds back this much (the app's control, the renderer's --feedback)
    void stop();
    // Message thread, often (a timer): loads each part's next sound into its idle instance
    // ahead of the change. The audio thread never waits for this; a sound not ready in
    // time is skipped and counted in missedChanges.
    void service();
    juce::String currentPatch(int track) const;
    const std::vector<SoundChange>& soundPlan() const { return plan_; }
    const Palette& palette() const { return palette_; }

    // Audio thread. Channels 0/1 are the PA mix; 2/3, when present, carry the click.
    void render(float* const* out, int numChannels, int numSamples);

    const Composer* composer() const { return composer_.get(); }
    bool isPlaying() const { return playing_.load(); }
    bool isFinished() const { return finished_.load(); }
    int64_t preRollSamples() const { return int64_t(0.05 * sampleRate_); }
    juce::String delayDescription() const; // "1/8. · 0.39 s | 1/4 · 0.52 s"

    std::atomic<int> bar { 0 };
    std::atomic<double> seconds { 0 }; // playhead
    std::atomic<float> peakPre { 0 }, peakPost { 0 }; // master, before and after the limiter
    struct Meter { std::atomic<float> peak { 0 }; std::atomic<double> sumSquares { 0 }; std::atomic<int64_t> samples { 0 }; };
    Meter meters[kMaxTracks];
    Meter delayMeter, reverbMeter; // what the shared buses return to the mix
    bool effects = true;           // false: no delay or reverb (the renderer's --dry)
    std::atomic<int> changesMade { 0 }, missedChanges { 0 };
    float clickLevel = 0.5f;
    PeakLimiter limiter;
    GlueBus glue;
    bool mixStage = true;          // false: no ducking, low cuts, width or glue (the renderer's --raw)
    uint16_t enabledTracks = 0xFFFF; // bit per track; the renderer's --solo

private:
    // Each part has two instances: one live, the other loading the next sound or ringing out.
    enum SlotState { Idle, Loading, Ready, Live, Tail };
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::AudioProcessorParameter* cutoff = nullptr;
        float cutoffBase = 0.5f, gain = 1;
        int entry = -1;
        std::atomic<int> state { Idle };
        int64_t quiet = 0, tailed = 0; // samples of silence, and in all, while ringing out
        int64_t releaseAt = -1;        // the old sound lets go of its held notes here: a crossfade
        bool releasing[128] = {};
        juce::MidiBuffer midi;
    };
    struct TrackState
    {
        Slot slots[2];
        std::atomic<int> live { 0 };
        float filterTarget = 0, filterNow = 0;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain;
        juce::AudioBuffer<float> buffer;
        float held[128] = {}; // velocity of each note sounding now, 0 when off
        std::vector<SoundChange> changes;
        std::atomic<size_t> nextChange { 0 };
        bool internal = false;      // the engine's own instrument, not Surge
        InternalSynth synth;
        bool sampled = false, synthLayer = false; // the sampled kit; the synth kick under it
        DrumSampler sampler;
        FeedbackDelay echo;
        bool hasEcho = false;
        float trim = 1;             // the track's balance gain
        // The mix stage: how deep the kick ducks it, its stereo width (0 mono, 1 as played),
        // its pan gains and its low cut.
        float duck = 0, width = 1, panL = 1, panR = 1;
        bool lowCut = false;
        Biquad hp[2];
        Drive drive;
        Crusher crush;
        Compressor comp;
        // The widener: the track's mid, delayed a few ms and high-passed, added as side. Mono
        // sum unchanged, low end stays centred.
        std::vector<float> wide;
        int wideAt = 0;
        float wideAmount = 0, wideLp = 0;
        juce::AudioBuffer<float> mix; // the track's slots summed, before the mix stage
    };
    juce::String loadInto(Slot& slot, int track, int entry);
    void changeSound(int track, int at);

    void renderChunk(float* const* out, int numChannels, int offset, int len);
    void renderClick(float* const* out, int numChannels, int offset, int len);
    int64_t sampleOf(int64_t tick) const { return std::llround(double(tick) * samplesPerTick_); }

    double sampleRate_ = 48000;
    int maxBlock_ = 512;
    TrackState tracks_[kMaxTracks];
    int numTracks_ = 4;
    std::vector<float> gains_;
    Palette palette_;
    std::vector<SoundChange> plan_;
    std::unique_ptr<juce::AudioPluginInstance> delay_, reverb_;
    juce::AudioBuffer<float> delayBus_, reverbBus_;
    Biquad reverbLowCut_[2]; // the reverb's lows smear the kick and the bass, and spread them wide
    juce::MidiBuffer noMidi_;
    float delaySeconds_[2] = { 0, 0 }; // left, right: shown with their bar division

    juce::SpinLock lock_;
    std::unique_ptr<Composer> composer_;
    std::vector<Event> events_;
    size_t head_ = 0;
    int nextBar_ = 0;
    int64_t pos_ = 0, endSample_ = 0;
    double samplesPerTick_ = 1;
    std::atomic<bool> playing_ { false }, finished_ { false }, panic_ { false };

    int kickTrack_ = -1;           // its notes duck the others
    float duckEnv_ = 0, duckTarget_ = 0, duckDecay_ = 1;
    int duckRamp_ = 0;
    std::vector<float> duckCurve_;
    std::vector<std::pair<int, float>> kickHits_; // this chunk's kicks: sample, velocity

    double clickPhase_ = 0, clickFreq_ = 0, clickEnv_ = 0;
};

} // namespace liszt
