#pragma once
#include "Composer.h"
#include <array>

namespace liszt {

// A four-voice chorale in a dark minor, wrapped in IDM rhythm, over a 10-minute form.
//
// The voices are written the way a chorale is harmonized: a melody first (an arch per phrase,
// mostly stepwise, cadences on the right degrees, the theme returning), then bass, tenor and
// alto found by search over every voicing of every chord, scored by the classical rules (no
// parallel fifths or octaves, leading tones rise, sevenths fall, nothing missing, good
// spacing), then decorated with suspensions, passing tones and appoggiaturas.
//
// Around them: micro impulses that keep one on every beat, with Euclidean patterns on changing
// grids (sixteenths, triplets, quintuplets, sextuplets, septuplets, thirty-seconds), cross-accents
// and ratchets; pitched impulse sentences in sixteenths, eighths and dotted eighths around the
// melody; sparse deep drums; a sine sub under the climax.
//
// Nine tracks (see sceneInfo("chorale")): each voice its own, a sine sub under the climax,
// sparse deep drums, micro impulses, pitched impulse sentences, and the piano doubling the melody.
class ChoraleComposer : public Composer
{
public:
    enum CTrack : uint8_t { Soprano, Alto, Tenor, BassVoice, Sub, Drums, Micro, Blips, Piano, kChoraleTracks };
    explicit ChoraleComposer(uint64_t seed);
    void generateBar(int bar, std::vector<Event>& out) override;
    std::string describe() const override;

    enum Quality : uint8_t { Min, Maj, Dim, Min7, Dom7, Maj7, HalfDim7, Dim7, MinAdd9, MajAdd9 };
    enum Cadence : uint8_t { None, Half, Authentic, Deceptive, Plagal };
    struct Chord
    {
        int root = 0;            // semitones above the home tonic
        Quality quality = Min;
        int inversion = 0;       // which chord tone is in the bass: 0 root, 1 third, 2 fifth, 3 seventh
        int start = 0, len = 16; // in sixteenths from the start of the piece
        Cadence cadence = None;  // this chord ends a phrase with this cadence
        bool pedal = false;      // the bass holds the dominant under it
        int phraseStart = 0, phraseLen = 16, section = 0;
    };
    using Voicing = std::array<int, 4>; // bass, tenor, alto, soprano at the chord's start

    const std::vector<Chord>& chords() const { return chords_; }
    const std::vector<Voicing>& voicings() const { return voicings_; }
    int tonicPc() const { return tonicPc_; }
    std::vector<int> chordPcs(const Chord& c) const; // root, third, fifth, seventh or ninth

    static constexpr int kVoiceRange[4][2] = { { 36, 62 }, { 48, 69 }, { 55, 76 }, { 60, 81 } };
    static constexpr int kDrumPitchRange[2] = { 28, 52 };
    static constexpr int kMicroPitchRange[2] = { 55, 108 };

private:
    struct Note { int track; int pitch; int64_t start, end; float vel; };
    void planForm();
    void composeMelody(const std::vector<int>* themeBias);
    void voiceLowerParts();
    void writeVoices();
    void writeRhythm();
    void writeSentences(Rng& r);
    void writeLevels();
    void finish();
    bool active(int track, int bar) const;
    std::vector<int> scaleOf(const Chord& c) const; // the scale passing tones walk on

    int tonicPc_ = 2;
    int swingTicks_ = 30;
    std::vector<Chord> chords_;
    std::vector<Voicing> voicings_;
    std::vector<Note> notes_;
    std::vector<std::vector<Event>> byBar_;
    std::vector<Event> levels_;
};

} // namespace liszt
