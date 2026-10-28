#pragma once
#include "Composer.h"
#include <array>

namespace liszt {

// Songs 4-11, one per genre (jungle, footwork, ecstatic arpeggios, dub techno, clicks & glitch,
// gqom, deconstructed club, halftime), each named for its genre until Kerem names it. One
// composer plays them all from a table per genre (Genres.cpp): the form, a drum machine of step
// patterns per energy tier (each part on its own grid, so triplets run against sixteenths),
// chopped breaks, gaps and stutters, a sub, a bass and chord line from patterns.
//
// The writing is after Burial, Swans, Blanck Mass, Aphex Twin and Amon Tobin: one progression per
// song that keeps coming back (another for the breakdowns), chords of sevenths and ninths voiced
// so each voice moves as little as it can, and one two-bar hook that the song develops: alone and
// slowed in the calm, answered by its sequence a third up, turned upside down in the breakdowns,
// doubled in octaves at the summit, where it comes home as it was first heard.
//
// Ten tracks for every genre: pad, pulse, metal, tick, perc, bass, sub, arp, chord, fx.
//
// No kick, snare or hi-hat: Kerem plays those live on his acoustic kit. The machine's drums are
// electronic percussion only. The pulse is silent: its notes only duck the mix (a ghost kick, until
// his real kick drives it); metal is tuned metallic percussion, tick the fine clicks (tuned, or
// noise from note 96 up), perc everything else (zaps, congas, glitches, tuned drums).
class GenreComposer : public Composer
{
public:
    enum GTrack : uint8_t { Pad, Pulse, Metal, Tick, Perc, Bass, Sub, Arp, Chord, Fx, kGenreTracks };

    // A pattern: one character per step, perBar steps to the bar, as many bars as it is long.
    //   X x o g  a hit (accent, normal, soft, ghost) on the part's own pitch
    //   1-9      metal/perc: the n-th chord tone up · tick: 2 = a noise tick · bass: the n-th scale degree above the root (5 = the fifth)
    //   -        the note before holds on · .  rest
    // "euclid" (perc only): k hits spread evenly over perBar steps, more as the music drives.
    struct Pat { const char* s = ""; int perBar = 16; };
    struct Part { Pat tier[3]; };                  // energy below .4, below .75, above
    // layers: P K S H C B U A D F (pad, pulse, metal, tick, perc, bass, sub, arp, chord, fx), and
    // L: Kerem's drums lead here, the machine's percussion thins out
    struct Sec { const char* name; int bars; float energy; const char* layers; };
    struct Spec
    {
        const char* scene;
        const char* title;
        int bpmLo, bpmHi;                          // even
        const Mode* mode;
        std::array<int, 5> tonics;
        std::array<std::array<int, 4>, 3> progs;   // four chord roots (scale degrees): one is home, another the breakdowns'
        int preRoot = 6;                           // the last chord before the peak
        std::array<Sec, 10> form;                  // name nullptr ends it
        Part pulse, metal, tick, perc, bass, chord;
        std::array<int, 3> arpRate {};             // arpeggio steps per bar per tier (0 silent)
        int arpLenLo = 5, arpLenHi = 7;            // its cycle, in notes: phases against the bar
        float arpDensLo = .4f, arpDensHi = .8f, arpGate = .7f;
        int arpLo = 60, arpHi = 86, percLo = 36, percHi = 55; // registers (the lead patches sound an octave down)
        int ext = 2;                               // chords: 0 triads, 1 sevenths, 2 ninths
        bool hook = true;                          // the arp track plays the song's hook; false: the polymetric arpeggio
        float chop = 0, gap = 0, stutter = 0;      // chances per bar, scaled by energy
        int stutterTrack = Metal, stutterDiv = 32;
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
    // halfway through the summit the song lifts a whole tone and stays there
    int tonicAt(int bar) const { return (tonicPc_ + (bar >= liftBar_ ? 2 : 0)) % 12; }
    int rootAt(int bar) const { return rootOfBar_[size_t(bar)]; }
    const std::array<int, 4>& homeProgression() const { return home_; }
    bool active(int track, int bar) const;
    std::array<int, 2> range(int track) const;

private:
    void planForm();
    void planHarmony();
    void makeArp();
    void makeHook();
    void emitHook(int bar, std::vector<Event>& out);
    std::vector<int> chordTones(int bar, bool rootless) const; // pitch classes
    std::vector<int> voice(int bar, int n, int lo, int hi, bool rootless, std::vector<int>& prev) const;
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
    int tonicPc_ = 0, liftBar_ = 1 << 30;
    std::vector<int> rootOfBar_;
    std::vector<int> arp_, arpHome_;                // ladder indices
    std::vector<int> arpRank_;
    std::vector<int> padHeld_, padVoices_, chordVoices_;
    std::array<int, 4> home_ {}, away_ {};
    std::vector<bool> awayBar_;                     // the bars that play the breakdown's progression
    struct Note { int step, len, deg; float vel; }; // the hook: sixteenths over two bars, degrees above the chord's root
    std::vector<Note> hook_, hookHome_;
    int subHeld_ = -1, fxHeld_ = -1;
    int percRot_ = 0;
    int64_t arpStep_ = 0;
    // this bar's breaks: the beat each beat plays (chop), the beat left silent (gap), the beat stuttered
    std::array<int, 4> chopMap_ {};
    std::vector<bool> drummerLeads_;                // per section: the live drums lead (L)
    std::array<float, 16> accent_ {};               // the song's groove: how hard each sixteenth of the bar leans
    int gapBeat_ = -1, stutterBeat_ = -1;
};

const SceneInfo* genreSceneInfo(const std::string& scene);

} // namespace liszt
