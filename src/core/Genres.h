#pragma once
#include "Composer.h"
#include <array>

namespace liszt {

// Songs 4-11, one per genre (jungle, footwork, ecstatic arpeggios, dub techno, clicks & glitch,
// gqom, deconstructed club, halftime), each named for its genre until Kerem names it. One
// composer plays them all from a table per genre (Genres.cpp): the form, the harmony, a drum
// machine of step patterns per energy tier (each part on its own grid, so triplets run against
// sixteenths), chopped breaks, gaps and stutters, a held drone, a sub, a bass and chord line from
// patterns, and a polymetric arpeggio cycling the chord tones.
//
// Ten tracks for every genre: pad, kick, snare, hats, perc, bass, sub, arp, chord, fx.
class GenreComposer : public Composer
{
public:
    enum GTrack : uint8_t { Pad, Kick, Snare, Hats, Perc, Bass, Sub, Arp, Chord, Fx, kGenreTracks };

    // A pattern: one character per step, perBar steps to the bar, as many bars as it is long.
    //   X x o g  a hit (accent, normal, soft, ghost) on the part's own pitch
    //   1-9      drums: the part's n-th pitch · bass: the n-th scale degree above the root (5 = the fifth)
    //   -        the note before holds on · .  rest
    // "euclid" (perc only): k hits spread evenly over perBar steps, more as the music drives.
    struct Pat { const char* s = ""; int perBar = 16; };
    struct Part { Pat tier[3]; };                  // energy below .4, below .75, above
    struct Sec { const char* name; int bars; float energy; const char* layers; }; // layers: P K S H C B U A D F
    struct Spec
    {
        const char* scene;
        const char* title;
        int bpmLo, bpmHi;                          // even
        const Mode* mode;
        std::array<int, 5> tonics;
        std::array<int, 5> roots;                  // chord roots (scale degrees) and their weights
        std::array<double, 5> rootW;
        int preRoot = 6;                           // the last chord before the peak
        int regionCalm = 16, regionDriven = 8;     // bars per chord
        std::array<Sec, 10> form;                  // name nullptr ends it
        Part kick, snare, hats, perc, bass, chord;
        std::array<int, 3> arpRate {};             // arpeggio steps per bar per tier (0 silent)
        int arpLenLo = 5, arpLenHi = 7;            // its cycle, in notes: phases against the bar
        float arpDensLo = .4f, arpDensHi = .8f, arpGate = .7f;
        int arpLo = 60, arpHi = 88, percLo = 36, percHi = 55; // registers
        int chordSize = 3;                         // 3 triads, 4 sevenths
        float chop = 0, gap = 0, stutter = 0;      // chances per bar, scaled by energy
        int stutterTrack = Snare, stutterDiv = 32;
        float swing = 0;                           // the off sixteenths late, in sixteenths
        float chordSweep = .2f;                    // how far the chord's filter breathes
    };

    static const Spec* find(const std::string& scene);
    static const std::vector<const Spec*>& all();

    GenreComposer(const Spec& spec, uint64_t seed);
    void generateBar(int bar, std::vector<Event>& out) override;
    std::string describe() const override;

    const Spec& spec() const { return spec_; }
    int tonicPc() const { return tonicPc_; }
    int rootAt(int bar) const { return rootOfBar_[size_t(bar)]; }
    bool active(int track, int bar) const;
    std::array<int, 2> range(int track) const;

private:
    void planForm();
    void planHarmony();
    void makeArp();
    float levelAt(int track, int bar) const;
    int pitchOf(int bar, int deg, int lo, int hi, int base) const;
    int ladderPitch(int bar, int idx, int lo, int hi) const;
    void emitAutomation(int bar, std::vector<Event>& out);
    void emitPad(int bar, std::vector<Event>& out);
    void emitPart(int track, const Part& part, int bar, std::vector<Event>& out);
    void emitArp(int bar, std::vector<Event>& out);
    void emitHeld(int track, int bar, std::vector<Event>& out, int& held, int pitch);
    int tier(int bar) const;

    const Spec& spec_;
    int tonicPc_ = 0;
    std::vector<int> rootOfBar_;
    std::vector<int> arp_, arpHome_;                // ladder indices
    std::vector<int> arpRank_;
    std::vector<int> padHeld_;
    int subHeld_ = -1, fxHeld_ = -1;
    int percRot_ = 0;
    int64_t arpStep_ = 0;
    // this bar's breaks: the beat each beat plays (chop), the beat left silent (gap), the beat stuttered
    std::array<int, 4> chopMap_ {};
    int gapBeat_ = -1, stutterBeat_ = -1;
};

const SceneInfo* genreSceneInfo(const std::string& scene);

} // namespace liszt
