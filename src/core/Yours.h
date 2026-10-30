#pragma once
#include "Composer.h"
#include <array>

namespace liszt {

// "Let All Be Yours": drony techno in a phrygian minor. A four-on-the-floor kick tuned to the
// tonic, a rolling bass between the kicks, an acid line (sixteen steps with accents and slides,
// mutating every four bars, its filter swept over the whole form), dark syncopated stabs on a
// Euclidean grid against the beat, hats and claps, a held drone, a sine sub and a riser.
//
// Nine tracks (see sceneInfo("letallbeyours")): drone, kick, bass, acid, stab, hats, clap, sub, fx.
//
// 2026-10-30 (Kerem: "in the style of Yanamaste, Marrøn or Ben Klock"; and no kick, snare or hi-hat,
// he plays those): hypnotic techno. The kick is a silent ghost that only pumps the mix; the hats
// track plays tuned metal ticks wheeling in threes against the bar; the clap track is iron:
// industrial low hits off his backbeat. Every part's end is a figure the bass, the stabs and the
// iron play together while the ticks stop.
class YoursComposer : public Composer
{
public:
    enum YTrack : uint8_t { Drone, Kick, Bass, Acid, Stab, Hats, Clap, Sub, Fx, kYoursTracks };
    explicit YoursComposer(uint64_t seed);
    void generateBar(int bar, std::vector<Event>& out) override;
    std::string describe() const override;

    int tonicPc() const { return tonicPc_; }
    int rootAt(int bar) const { return rootOfBar_[size_t(bar)]; } // a phrygian degree: 0 i, 1 bII, 3 iv, 5 bVI, 6 bVII
    bool active(int track, int bar) const;

    static constexpr int kRange[kYoursTracks][2] = {
        { 36, 76 }, { 24, 36 }, { 40, 64 }, { 33, 77 }, { 46, 74 }, { 72, 95 }, { 28, 52 }, { 22, 40 }, { 48, 72 },
    };

private:
    struct AcidStep { bool on = false; int deg = 0; bool accent = false, slide = false; int rank = 0; };
    void planForm();
    void planHarmony();
    void makeAcid();
    void mutateAcid();
    float levelAt(int track, int bar) const;
    int pitchOf(int bar, int deg, int lo, int hi, int base) const;
    void emitAutomation(int bar, std::vector<Event>& out);
    void emitDrone(int bar, std::vector<Event>& out);
    void emitDrums(int bar, std::vector<Event>& out);
    void emitBass(int bar, std::vector<Event>& out);
    void emitAcid(int bar, std::vector<Event>& out);
    void emitStab(int bar, std::vector<Event>& out);
    void emitHeld(int track, int bar, std::vector<Event>& out, int& held, int pitch);
    const char* endFigure(int bar) const;

    int tonicPc_ = 5;
    std::vector<int> rootOfBar_;
    std::array<AcidStep, 16> acid_ {}, acidHome_ {};
    std::vector<int> droneHeld_;
    int subHeld_ = -1, fxHeld_ = -1;
    int stabRot_ = 0;
};

} // namespace liszt
