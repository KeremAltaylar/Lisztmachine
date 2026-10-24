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

struct TrackSound
{
    const char* patch;           // Surge XT factory patch, "Category/Name"
    float trimDb;                // static balance between the tracks
    std::vector<FxSpec> inserts; // in series, after the synth
    float delaySend, reverbSend; // 0..1 into the shared delay and reverb
};

// Dark and wet: guitar-like distortion on the lead, grit on the bass, every part into a
// long filtered feedback delay and a big damped hall.
inline const TrackSound kBerlinSounds[kNumTracks] = {
    { "Pads/Yeti Funeral", -5.1f,
      { { "Chorus", { { "Modulation Rate", "0.15 Hz" }, { "Modulation Depth", "45 %" }, { "EQ High Cut", "3000 Hz" },
                      { "Output Mix", "60 %" } } } },
      0.15f, 0.45f },
    { "Basses/Lord Sawtooth", -0.6f,
      { { "Distortion", { { "Distortion Model", "Hard" }, { "Distortion Drive", "14 dB" }, { "Pre-EQ Gain", "6 dB" },
                          { "Pre-EQ Frequency", "220 Hz" }, { "Post-EQ High Cut", "2500 Hz" }, { "Output Gain", "-21.2 dB" } } } },
      0.10f, 0.20f },
    { "Plucks/Mystic", -8.1f,
      { { "Tape", { { "Hysteresis Drive", "70 %" }, { "Degrade Depth", "30 %" }, { "Degrade Amount", "25 %" },
                    { "Output Mix", "100 %" } } } },
      0.45f, 0.25f },
    { "Leads/Synth Guitar 2", -8.8f,
      { { "Distortion", { { "Distortion Model", "Hard" }, { "Distortion Drive", "24 dB" }, { "Pre-EQ Gain", "9 dB" },
                          { "Pre-EQ Frequency", "900 Hz" }, { "Pre-EQ Bandwidth", "1.5 octaves" },
                          { "Post-EQ High Cut", "4200 Hz" }, { "Post-EQ Gain", "-4 dB" }, { "Post-EQ Frequency", "2500 Hz" },
                          { "Output Gain", "-11.7 dB" } } },
        { "Tape", { { "Hysteresis Drive", "60 %" }, { "Output Mix", "100 %" } } } },
      0.35f, 0.40f },
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

    // Message thread. Loads one Surge per track with its patch. Returns an error or empty.
    juce::String load(double sampleRate, int maxBlock);
    // Message thread. A new concert from this seed, starting at bar 0.
    void start(uint64_t seed);
    void stop();

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
    bool effects = true;           // false: no inserts, no sends (the renderer's --dry)
    float clickLevel = 0.5f;
    PeakLimiter limiter;
    uint8_t enabledTracks = 0x0F; // bit per Track; the renderer's --solo
    juce::String patchOverride[kNumTracks]; // set before load(); empty = the scene's own sound

private:
    struct TrackState
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::vector<std::unique_ptr<juce::AudioPluginInstance>> inserts;
        juce::AudioProcessorParameter* cutoff = nullptr;
        float cutoffBase = 0.5f, filterTarget = 0, filterNow = 0, trim = 1, delaySend = 0, reverbSend = 0;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
    };

    void renderChunk(float* const* out, int numChannels, int offset, int len);
    void renderClick(float* const* out, int numChannels, int offset, int len);
    int64_t sampleOf(int64_t tick) const { return std::llround(double(tick) * samplesPerTick_); }

    double sampleRate_ = 48000;
    int maxBlock_ = 512;
    TrackState tracks_[kNumTracks];
    std::vector<float> gains_;
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
