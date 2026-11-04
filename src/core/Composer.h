#pragma once
#include "Music.h"
#include <memory>
#include <string>
#include <vector>

namespace liszt {

constexpr int kPPQ = 960;                 // ticks per quarter note
constexpr int kSixteenth = kPPQ / 4;
constexpr int kBarTicks = 4 * kPPQ;       // 4/4 throughout this scene

// The Berlin scene's tracks. Other scenes name their own (see SceneInfo).
enum Track : uint8_t { Pad, Bass, Seq, Lead, Guitar, Solo, Sub, kNumTracks };
inline const char* kTrackNames[kNumTracks] = { "pad", "bass", "seq", "lead", "guitar", "solo", "sub" };

constexpr int kMaxTracks = 20;

// What a scene asks of the engine: its tracks by name, what each is to the visuals
// (0 harmony, 1 bass and drums, 2 rhythm, 3 melody), its sends to the shared delay and
// reverb, which tracks change sound together (the first one's plan; -1 its own), and how pure
// each track's tuning is: 0 as the patch is, 1 pure (one voice per oscillator, no detune, whole
// semitones, no drift, no pitch-moving effects), 2 pure but the oscillators keep their tuning
// (for patches whose tuning is part of the sound, like the pianos).
struct SceneInfo
{
    const char* name;
    int numTracks;
    const char* trackNames[kMaxTracks];
    int role[kMaxTracks];
    float sends[kMaxTracks][2];
    int group[kMaxTracks];
    uint8_t pure[kMaxTracks];
    bool pureBuses;              // the shared delay and reverb without pitch modulation
    int trackOf(const std::string& n) const
    {
        for (int t = 0; t < numTracks; ++t) if (n == trackNames[t]) return t;
        return -1;
    }
};
const SceneInfo& sceneInfo(const std::string& scene);

struct Event
{
    enum Type : uint8_t { NoteOff, NoteOn, Level, Filter, Throw, Hush }; // order = dispatch order at equal ticks
    int64_t tick;
    Type type;
    uint8_t track;
    uint8_t pitch;
    float value; // NoteOn: velocity 0..1 · Level: gain 0..1 · Filter: cutoff offset -1..1 · Throw: beats into the echo · Hush: 0 silence, 1 open

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
    uint32_t layers; // bit per track
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

// A scene: a whole piece, composed bar by bar. The engine plays whichever scene it is given.
class Composer
{
public:
    virtual ~Composer() = default;

    // Appends every event that starts in `bar` (phrases and note-offs may reach later).
    // Bars must be requested in order from 0.
    virtual void generateBar(int bar, std::vector<Event>& out) = 0;
    virtual std::string describe() const = 0; // key, mode, tempo, length

    double bpm() const { return bpm_; }
    int totalBars() const { return int(sectionOfBar_.size()); }
    float energyAt(int bar) const { return energyOfBar_[size_t(bar)]; }
    const Section& sectionAt(int bar) const { return sections_[size_t(sectionOfBar_[size_t(bar)])]; }
    const std::vector<Section>& sections() const { return sections_; }

protected:
    explicit Composer(uint64_t seed) : rng_(seed * 0x2545F4914F6CDD1Dull + 1) {}

    Rng rng_;
    double bpm_ = 116;
    std::vector<Section> sections_;
    std::vector<int> sectionOfBar_;
    std::vector<float> energyOfBar_;
};

// A scene by name (kScenes); anything else is Berlin. The genre songs are named for their genre
// until Kerem names them.
std::unique_ptr<Composer> makeComposer(const std::string& scene, uint64_t seed);
inline const char* kScenes[] = { "berlin", "stoner", "jungle", "ecstatic", "noir", "deconstructed", "halftime",
                                 "doom", "blackgaze" };
inline const char* kSceneTitles[] = { "Berlin", "Working Class Curse", "Liquid", "Ecstatic Arpeggios", "Noir", "Deconstructed Club",
                                      "Halftime", "Doom", "Blackgaze" };

// Berlin-school scene: drone pad, sequenced bass, polymetric upper sequence and a
// motif-based lead, over a slow modal progression shaped by a 10-minute energy arc.
// 2026-10-30 (Kerem: "more melancholy and organic"): a minor home (aeolian or phrygian, the shift the
// one lighter moment), slower, bowed and sampled instruments, a guitar's power chords let ring under
// the sequences, and every part's end prepared by a figure the bass and the guitar play together.
class BerlinComposer : public Composer
{
public:
    explicit BerlinComposer(uint64_t seed);
    void generateBar(int bar, std::vector<Event>& out) override;
    std::string describe() const override;

    int tonicPc() const { return tonicPc_; }
    const Mode& modeAt(int bar) const { return *modeOfBar_[size_t(bar)]; }
    int chordAt(int bar) const { return chordOfBar_[size_t(bar)]; }

    // Pitch ranges, also used by the tests.
    static constexpr int kRange[kNumTracks][2] = { { 48, 76 }, { 28, 52 }, { 52, 81 }, { 60, 88 }, { 28, 64 }, { 62, 86 }, { 28, 40 } };
    const char* endFigure(int bar) const;           // the part's end: the figure its last bar plays, or nullptr
    bool stopBar(int bar) const;                    // the last bar of a big part: one attack together, then silence
    static constexpr int64_t kHit = 3 * kSixteenth; // how long that attack rings
    int64_t silenceFrom(int bar) const;             // where this bar falls silent until the next downbeat, or -1

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
    void emitGuitar(int bar, std::vector<Event>& out);
    bool soloFloor(int bar) const;
    bool bigStart(int bar) const;
    bool clip(int64_t on, int64_t& off) const;
    void emitSub(int bar, std::vector<Event>& out);
    void emitSolo(int bar, std::vector<Event>& out);
    void emitAutomation(int bar, std::vector<Event>& out);

    int tonicPc_ = 4;
    const Mode* homeMode_ = &kAeolian;
    const Mode* shiftMode_ = &kDorian;

    std::vector<int> chordOfBar_;
    std::vector<const Mode*> modeOfBar_;

    std::vector<Step> bass_, bassHome_, seq_, seqHome_;

    struct MotifNote { int start, dur, deg; };
    std::vector<MotifNote> motif_;

    std::array<int, 4> padVoices_ {};
    int padChord_ = -1, padStruck_ = 0; // the chord sounding, and the bar it was struck
    int subNote_ = -1; // the sub's held root
    int soloLeft_ = 0, soloRest_ = 0, soloPitch_ = -1, soloDir_ = 1; // the solo phrase: bars left, bars of breath, where the line is
};

} // namespace liszt
