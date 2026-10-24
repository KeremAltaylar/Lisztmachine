#pragma once
#include "Composer.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>

namespace liszt {

struct TrackSound
{
    const char* patch; // Surge XT factory patch, "Category/Name"
    float trimDb;      // static balance between the tracks
};

// The sound of the Berlin scene. One Surge XT instance per track.
inline const TrackSound kBerlinSounds[kNumTracks] = {
    { "Pads/MKS-70 Warm Pad", -4 },
    { "Sequences/Tyskland", -1 },
    { "Sequences/Phase 1", -4 },
    { "Leads/Moogy Saw", -5 },
};

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

    std::atomic<int> bar { 0 };
    std::atomic<double> seconds { 0 }; // playhead
    std::atomic<float> peakPre { 0 }, peakPost { 0 }; // master, before and after the limiter
    struct Meter { std::atomic<float> peak { 0 }; std::atomic<double> sumSquares { 0 }; std::atomic<int64_t> samples { 0 }; };
    Meter meters[kNumTracks];
    float clickLevel = 0.5f;
    PeakLimiter limiter;
    uint8_t enabledTracks = 0x0F; // bit per Track; the renderer's --solo
    juce::String patchOverride[kNumTracks]; // set before load(); empty = the scene's own sound

private:
    struct TrackState
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::AudioProcessorParameter* cutoff = nullptr;
        float cutoffBase = 0.5f, filterTarget = 0, filterNow = 0, trim = 1;
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
