#pragma once
#include "Composer.h"
#include <array>

namespace liszt {

// The genre songs (working class curse, liquid, ecstatic arpeggios, noir, deconstructed club, halftime, doom, blackgaze),
// each named for its genre until Kerem names it. One
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
// Eleven tracks for every genre: pad, pulse, metal, tick, perc, bass, sub, arp, chord, fx, harm.
// The sub is an 808: long notes on a rhythm of their own, sliding into the next (after Ghostemane
// and Icepeak). The harm is a choir above the pad, a second harmony layer that answers it late and
// sighs on a neighbour note while a chord holds.
//
// No kick, snare or hi-hat: Kerem plays those live on his acoustic kit. The machine's drums are
// electronic percussion only. The pulse is silent: its notes only duck the mix (a ghost kick, until
// his real kick drives it); metal is tuned metallic percussion, tick the fine clicks (tuned, or
// noise from note 96 up), perc everything else (zaps, congas, glitches, tuned drums).
class GenreComposer : public Composer
{
public:
    // The percussion section (2026-10-29, Kerem: "more percussion layers, not only high-pitched, variety"):
    // ground (low strikes off his kick: log drums, deep metal), body (the groove: hand drums answering
    // his snare), mallet (tuned figures crossing the bar), texture (scrapes, rattles, crackle, held),
    // impact (one big hit where a section rises). Each one's rhythm develops phrase by phrase.
    // The riff (2026-10-30, Kerem: "gnarly guitar like stoner doom, think of it as rhythm guitar, that's my
    // style, or industrial low hits"): a rhythm guitar (or struck iron) playing the song's riff.
    enum GTrack : uint8_t { Pad, Pulse, Metal, Tick, Perc, Bass, Sub, Arp, Chord, Fx, Harm, Ground, Body, Mallet, Texture, Impact, Riff, Drone, kGenreTracks };
    // How the riff is played: doom (power chords, long, the flat second and the tritone around them),
    // chug (palm-muted sixteenths, chords on the syncopations), tremolo (blackgaze: every sixteenth
    // picked, root, fifth and ninth), sludge (one huge chord a bar, let ring), drone (re-picked whole notes), stoner (fast: muted eighths, chords
    // on the push, a run in the second bar).
    enum class RiffStyle : uint8_t { None, Doom, Chug, Tremolo, Sludge, Drone, Stoner };
    // How the summit establishes itself (not by transposing what came before): the theme comes home
    // doubled; the riff, bass and 808 lock in unison; a pedal: the low end holds the tonic while the
    // chords move over it; half time: everything at half speed, twice the weight; a new theme of its own.
    enum class Summit : uint8_t { Theme, Unison, Pedal, Halftime, NewTheme };

    // A pattern: one character per step, perBar steps to the bar, as many bars as it is long.
    //   X x o g  a hit (accent, normal, soft, ghost) on the part's own pitch
    //   1-9      metal/perc: the n-th chord tone up · tick: 2 = a noise tick · bass, sub: the n-th scale degree above the root (5 = the fifth)
    //   ^        sub: the root an octave up (the 808 slides there from the note before)
    //   -        the note before holds on · .  rest
    // "euclid" (perc only): k hits spread evenly over perBar steps, more as the music drives.
    struct Pat { const char* s = ""; int perBar = 16; };
    struct Part { Pat tier[3]; };                  // energy below .4, below .75, above
    // layers: P K S H C B U A D F (pad, pulse, metal, tick, perc, bass, sub, arp, chord, fx),
    // G Y M T I R (ground, body, mallet, texture, impact, riff), and
    // L: Kerem's drums lead here, the machine's percussion thins out. The harm sings wherever the pad
    // plays and the music has some motion (energy .4 and up).
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
        Part pulse, metal, tick, perc, bass, chord, sub, ground, body, mallet, texture;
        std::array<int, 3> arpRate {};             // arpeggio steps per bar per tier (0 silent)
        int arpLenLo = 5, arpLenHi = 7;            // its cycle, in notes: phases against the bar
        float arpDensLo = .4f, arpDensHi = .8f, arpGate = .7f;
        int arpLo = 60, arpHi = 86, percLo = 36, percHi = 55; // registers (the lead patches sound an octave down)
    int harmLo = 62, harmHi = 84;              // the choir's register
        int ext = 2;                               // chords: 0 triads, 1 sevenths, 2 ninths
        bool hook = true;                          // the arp track plays the song's hook; false: the polymetric arpeggio
        float chop = 0, gap = 0, stutter = 0;      // chances per bar, scaled by energy
        int stutterTrack = Metal, stutterDiv = 32;
        float swing = 0;                           // the off sixteenths late, in sixteenths
        float chordSweep = .2f;                    // how far the chord's filter breathes
        int arpMorph = 4;                          // the arpeggio changes a note every this many bars (at most)
        RiffStyle riff = RiffStyle::None;
        int riffLo = 33;                           // the riff's lowest root (the power chord stacks a fifth and an octave on it)
        Summit summit = Summit::Theme;
        bool ends = true;                          // end figures into the parts that are not big arrivals
        bool legato = false;                       // every riff and bass note rings into the next one (no muted notes, no gaps)
        bool gaze = false;                         // the choir holds open intervals (root, fifth, ninth) instead of the chord
    bool build = false;                        // every part ends in a three-bar unison build (2, 4, 8 hits a bar), the next one enters whole
    bool walk = false;                         // the bass walks: a contrabass on every beat (in two in the calm), its pattern unused
    bool solo = false;                         // the arp track is a soloist: the hook as the head of each part, then phrases of its own
    bool voice = false;                        // the drone track is a wordless soprano: long phrases above the lead, sung across the part changes
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
    void emitHarm(int bar, std::vector<Event>& out);
    void emitDrone(int bar, std::vector<Event>& out);
    void emitImpact(int bar, std::vector<Event>& out);
    void makeRiff();
    void emitRiff(int bar, std::vector<Event>& out);
    void powerChord(int track, int64_t t, int64_t len, int root, int fifth, float vel, std::vector<Event>& out) const;
    int over(int bar, int deg, int up) const;       // semitones from a degree to the degree `up` steps above it
    int riffRoot(int bar, int deg) const;
    // The part's end: the last bar of every section (two into a big rise) prepares the next one with
    // a figure the low instruments play together; the drums stop around it.
    void planEnds();
    bool attackBar(int bar) const { return endFig_[size_t(bar)] >= 0; }
    // a main, driving part (.6 and up, clearly louder than the one before): it starts whole, after a stop
    bool drives(int si) const { return si > 0 && sections_[size_t(si)].energy >= .6f && sections_[size_t(si)].energy > sections_[size_t(si - 1)].energy + .1f; }
    bool buildsWith(int track, int bar) const; // a build bar: the track plays it because the next part has it
    void emitAttack(int bar, std::vector<Event>& out);
    bool summitHalftime(int bar) const;
    bool summitUnison(int bar) const;
    int lowRootAt(int bar) const;                   // the bass's root: the chord's, or the tonic under a pedal
    int lowPitch(int bar, int deg, int lo, int hi, int base) const;
    struct RHit { int64_t t, len; int deg; float vel; bool palm; };
    std::vector<RHit> riffHits(int bar) const;
    int subPitch(int bar, char c) const;
    bool inMode(int bar, int pitch) const;
    void emitPart(int track, const Part& part, int bar, std::vector<Event>& out);
    void emitArp(int bar, std::vector<Event>& out);
    void emitWalk(int bar, std::vector<Event>& out);
    void emitSolo(int bar, std::vector<Event>& out);
    void emitVoice(int bar, std::vector<Event>& out);
    void emitHeld(int track, int bar, std::vector<Event>& out, int& held, int pitch);
    int tier(int bar) const;

    const Spec& spec_;
    int tonicPc_ = 0, liftBar_ = 1 << 30;
    std::vector<int> rootOfBar_;
    std::vector<int> arp_, arpHome_;                // ladder indices
    std::vector<int> arpRank_;
    std::vector<int> padHeld_, padVoices_, chordVoices_, harmHeld_, harmVoices_, droneHeld_;
    std::array<int, 4> home_ {}, away_ {};
    std::vector<bool> awayBar_;                     // the bars that play the breakdown's progression
    struct Note { int step, len, deg; float vel; }; // the hook: sixteenths over two bars, degrees above the chord's root
    std::vector<Note> hook_, hookHome_, hookSummit_;
    struct RNote { int step, len, deg; float vel; bool palm; }; // the riff: two bars of sixteenths, degrees above the chord's root
    std::vector<RNote> riff_;
    int lastTop_ = -1;                              // the line's last pitch: the next one stays near it
    std::vector<int> melA_, melB_;                  // the tremolo's top line: degrees above the chord's root, two a bar; the summit its own
    std::vector<int> endFig_;                       // per bar: the end figure it plays, or -1
    std::vector<char> leadIn_;                      // per bar: its end silent from sixteenth leadSix_ (a part change without the stop)
    int leadSix_ = 12;
    std::vector<uint32_t> drop_;                    // per bar: the tracks taken out before a big arrival
    std::vector<int> chordsHeldRiff_;
    int fxHeld_ = -1;
    int percRot_ = 0;
    int64_t arpStep_ = 0;
    // this bar's breaks: the beat each beat plays (chop), the beat left silent (gap), the beat stuttered
    std::array<int, 4> chopMap_ {};
    std::vector<bool> drummerLeads_;                // per section: the live drums lead (L)
    std::array<float, 16> accent_ {};               // the song's groove: how hard each sixteenth of the bar leans
    int gapBeat_ = -1, stutterBeat_ = -1;
    int walkLast_ = -1;                             // the walk's last note: the next bar's root is found near it
    int voiceLeft_ = 0, voiceRest_ = 0, voicePitch_ = -1, voiceDir_ = 1; // the singer's phrase, as the soloist's
    int soloLeft_ = 0, soloRest_ = 0, soloPitch_ = -1, soloDir_ = 1; // the phrase: bars left in it, bars of breath, where the line is
};

const SceneInfo* genreSceneInfo(const std::string& scene);

} // namespace liszt
