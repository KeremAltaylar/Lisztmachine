#pragma once
#include "Music.h"
#include <string>
#include <vector>

namespace liszt {

constexpr int kPPQ = 960;                 // ticks per quarter note
constexpr int kSixteenth = kPPQ / 4;
constexpr int kBarTicks = 4 * kPPQ;       // 4/4 throughout this scene

enum Track : uint8_t { Pad, Bass, Seq, Lead, kNumTracks };
inline const char* kTrackNames[kNumTracks] = { "pad", "bass", "seq", "lead" };

struct Event
{
    enum Type : uint8_t { NoteOff, NoteOn, Level, Filter }; // order = dispatch order at equal ticks
    int64_t tick;
    Type type;
    uint8_t track;
    uint8_t pitch;
    float value; // NoteOn: velocity 0..1 · Level: gain 0..1 · Filter: cutoff offset -1..1

    bool operator<(const Event& o) const
    {
        if (tick != o.tick) return tick < o.tick;
        if (type != o.type) return type < o.type;
        if (track != o.track) return track < o.track;
        return pitch < o.pitch;
    }
};

struct Section
{
    const char* name;
    int startBar, bars;
    float energy;   // reached halfway through the section
    uint8_t layers; // bit per Track
    bool restoreTheme; // the original sequences come back here
};

struct Step
{
    bool on = false;
    int deg = 0;      // scale degrees above the current chord root
    bool accent = false;
    float gate = 0.6f; // note length as a fraction of a sixteenth
    int rank = 0;      // density order: low ranks play first as energy rises
};

// Berlin-school scene: drone pad, sequenced bass, polymetric upper sequence and a
// motif-based lead, over a slow modal progression shaped by a 10-minute energy arc.
class Composer
{
public:
    explicit Composer(uint64_t seed);

    // Appends every event that starts in `bar` (lead phrases and note-offs may reach later).
    // Bars must be requested in order from 0; the sequences mutate as they go.
    void generateBar(int bar, std::vector<Event>& out);

    double bpm() const { return bpm_; }
    int totalBars() const { return int(sectionOfBar_.size()); }
    int tonicPc() const { return tonicPc_; }
    const Mode& modeAt(int bar) const { return *modeOfBar_[size_t(bar)]; }
    int chordAt(int bar) const { return chordOfBar_[size_t(bar)]; }
    float energyAt(int bar) const { return energyOfBar_[size_t(bar)]; }
    const Section& sectionAt(int bar) const { return sections_[size_t(sectionOfBar_[size_t(bar)])]; }
    const std::vector<Section>& sections() const { return sections_; }
    std::string describe() const; // key, mode, tempo

    // Pitch ranges, also used by the tests.
    static constexpr int kRange[kNumTracks][2] = { { 48, 76 }, { 28, 52 }, { 52, 81 }, { 60, 88 } };

private:
    void planForm();
    void planHarmony();
    void makeBassPattern();
    void makeSeqPattern();
    void makeMotif();
    void mutate(std::vector<Step>& pattern, double chance, bool isBass);
    float levelAt(int track, int bar) const;
    bool active(int track, int bar) const;
    int chordRootPitch(int track, int bar) const;
    int stepPitch(int track, int bar, int deg) const;
    void emitPad(int bar, std::vector<Event>& out);
    void emitSequence(int track, int bar, std::vector<Event>& out);
    void emitLeadPhrase(int bar, std::vector<Event>& out);
    void emitAutomation(int bar, std::vector<Event>& out);

    Rng rng_;
    double bpm_ = 116;
    int tonicPc_ = 4;
    const Mode* homeMode_ = &kAeolian;
    const Mode* shiftMode_ = &kDorian;

    std::vector<Section> sections_;
    std::vector<int> sectionOfBar_, chordOfBar_;
    std::vector<const Mode*> modeOfBar_;
    std::vector<float> energyOfBar_;

    std::vector<Step> bass_, bassHome_, seq_, seqHome_;

    struct MotifNote { int start, dur, deg; };
    std::vector<MotifNote> motif_;

    std::array<int, 4> padVoices_ {};
    int padChord_ = -1;
};

} // namespace liszt
