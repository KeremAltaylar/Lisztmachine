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
struct Palette
{
    std::vector<PaletteEntry> parts[kNumTracks];
    juce::String load(const juce::File& file); // returns an error or empty
};
juce::File findPalette(const juce::String& name); // sounds/<name>.txt, searched upward from the executable

// When each part changes sound, and to which palette entry.
struct SoundChange { int bar, track, entry; };
// Calm sections take darker patches, the peak the brightest; parts change at alternate
// sections and a few bars apart, never all at once. Deterministic per seed.
std::vector<SoundChange> planSounds(const Composer& composer, const Palette& palette, uint64_t seed);

// Sends into the shared delay and reverb, per part.
inline constexpr float kSends[kNumTracks][2] = { { 0.15f, 0.35f }, { 0.05f, 0.15f }, { 0.35f, 0.25f }, { 0.30f, 0.35f } };

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

class Engine
{
public:
    Engine();
    ~Engine();

    // Message thread. Creates two Surge instances per part and the shared buses.
    juce::String load(double sampleRate, int maxBlock, const Palette& palette);
    // Message thread. A new concert from this seed, from bar 0 or from any later bar.
    void start(uint64_t seed, int startBar = 0);
    std::string scene = "berlin"; // which composer start() uses
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
    Meter meters[kNumTracks];
    Meter delayMeter, reverbMeter; // what the shared buses return to the mix
    bool effects = true;           // false: no delay or reverb (the renderer's --dry)
    std::atomic<int> changesMade { 0 }, missedChanges { 0 };
    float clickLevel = 0.5f;
    PeakLimiter limiter;
    uint8_t enabledTracks = 0x0F; // bit per Track; the renderer's --solo

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
    };
    juce::String loadInto(Slot& slot, int track, int entry);
    void changeSound(int track, int at);

    void renderChunk(float* const* out, int numChannels, int offset, int len);
    void renderClick(float* const* out, int numChannels, int offset, int len);
    int64_t sampleOf(int64_t tick) const { return std::llround(double(tick) * samplesPerTick_); }

    double sampleRate_ = 48000;
    int maxBlock_ = 512;
    TrackState tracks_[kNumTracks];
    std::vector<float> gains_;
    Palette palette_;
    std::vector<SoundChange> plan_;
    std::unique_ptr<juce::AudioPluginInstance> delay_, reverb_;
    juce::AudioBuffer<float> delayBus_, reverbBus_;
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

    double clickPhase_ = 0, clickFreq_ = 0, clickEnv_ = 0;
};

} // namespace liszt
