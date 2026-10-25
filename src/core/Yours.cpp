#include "Yours.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace liszt {

namespace {

constexpr uint16_t bit(int t) { return uint16_t(1u << t); }
constexpr double kTwoPi = 6.283185307179586;
using Y = YoursComposer;

bool is(const Section& s, const char* name) { return std::strcmp(s.name, name) == 0; }

} // namespace

YoursComposer::YoursComposer(uint64_t seed) : Composer(seed)
{
    static constexpr int tonics[] = { 5, 4, 7, 9, 2 }; // F E G A D: the kick sits on the tonic, 37-55 Hz
    tonicPc_ = tonics[rng_.range(0, 4)];
    bpm_ = 2.0 * rng_.range(64, 67); // 128..134
    planForm();
    planHarmony();
    makeAcid();
    acidHome_ = acid_;
}

void YoursComposer::planForm()
{
    struct T { const char* name; int bars; float energy; uint16_t layers; };
    const uint16_t drums = bit(Kick) | bit(Hats);
    static const T plan[] = {
        { "Void",       16, .15f, uint16_t(bit(Drone) | bit(Fx)) },
        { "Pulse",      32, .40f, uint16_t(bit(Drone) | drums | bit(Bass)) },
        { "Acid",       48, .58f, uint16_t(bit(Drone) | drums | bit(Bass) | bit(Acid)) },
        { "Descent",    32, .72f, uint16_t(bit(Drone) | drums | bit(Bass) | bit(Acid) | bit(Stab) | bit(Clap)) },
        { "Breakdown",  32, .35f, uint16_t(bit(Drone) | bit(Acid) | bit(Sub) | bit(Fx)) },
        { "Rise",       16, .80f, uint16_t(bit(Drone) | drums | bit(Acid) | bit(Stab) | bit(Clap) | bit(Fx)) },
        { "Possession", 64, 1.0f, uint16_t(bit(Drone) | drums | bit(Bass) | bit(Acid) | bit(Stab) | bit(Clap) | bit(Sub)) },
        { "Exorcism",   32, .55f, uint16_t(bit(Drone) | drums | bit(Bass) | bit(Stab)) },
        { "Ash",        32, .20f, uint16_t(bit(Drone) | bit(Kick) | bit(Fx)) },
    };
    int bar = 0;
    float prev = 0;
    for (auto& t : plan)
    {
        sections_.push_back({ t.name, bar, t.bars, t.energy, t.layers, std::strcmp(t.name, "Possession") == 0 });
        for (int i = 0; i < t.bars; ++i)
        {
            sectionOfBar_.push_back(int(sections_.size()) - 1);
            const float k = std::min(1.0f, float(i + 1) / (t.bars * 0.5f));
            energyOfBar_.push_back(prev + (t.energy - prev) * k);
        }
        bar += t.bars;
        prev = t.energy;
    }
}

void YoursComposer::planHarmony()
{
    // One root per region, 16 bars when calm and 8 when driven. The tonic dominates; bII is the
    // phrygian threat, bVI and iv darken, bVII leans back home. Each section starts on the tonic,
    // the Rise ends on bII so the Possession lands on i.
    static constexpr int roots[] = { 0, 1, 5, 3, 6 };
    static constexpr std::array<double, 5> w = { .40, .22, .16, .12, .10 };
    rootOfBar_.assign(sectionOfBar_.size(), 0);
    for (auto& sec : sections_)
    {
        if (is(sec, "Void") || is(sec, "Ash")) continue;
        const int region = sec.energy < .5f ? 16 : 8;
        int prev = 0;
        for (int r = 0; r * region < sec.bars; ++r)
        {
            int root = 0;
            if (r > 0) do root = roots[rng_.weighted(w)]; while (root == prev && root != 0);
            if (is(sec, "Rise") && (r + 1) * region >= sec.bars) root = 1;
            for (int b = 0; b < region && r * region + b < sec.bars; ++b) rootOfBar_[size_t(sec.startBar + r * region + b)] = root;
            prev = root;
        }
    }
}

void YoursComposer::makeAcid()
{
    // Degrees above the root: the root most, its octave, the fifth, the minor third, the
    // phrygian second, the seventh below.
    static constexpr int degs[] = { 0, 7, 4, 2, 1, -1 };
    static constexpr std::array<double, 6> w = { .40, .16, .12, .14, .10, .08 };
    for (int i = 0; i < 16; ++i)
    {
        auto& s = acid_[size_t(i)];
        s.on = i == 0 || rng_.chance(i % 4 == 0 ? .8 : i % 2 == 0 ? .6 : .5);
        s.deg = i == 0 ? 0 : degs[rng_.weighted(w)];
        s.accent = rng_.chance(i % 4 == 0 ? .2 : .35);
        s.slide = rng_.chance(.22);
    }
    std::array<std::pair<double, int>, 16> order;
    for (int i = 0; i < 16; ++i)
        order[size_t(i)] = { (acid_[size_t(i)].on ? 0 : 100) + (i % 4 == 0 ? 0 : i % 2 == 0 ? 1 : 2) + rng_.uniform() * 1.5, i };
    std::sort(order.begin(), order.end());
    for (int r = 0; r < 16; ++r) acid_[size_t(order[size_t(r)].second)].rank = r;
}

void YoursComposer::mutateAcid()
{
    static constexpr int degs[] = { 0, 7, 4, 2, 1, -1 };
    const int i = rng_.range(1, 15);
    auto& s = acid_[size_t(i)];
    switch (rng_.range(0, 3))
    {
        case 0: s.deg = degs[rng_.range(0, 5)]; break;
        case 1: s.accent = !s.accent; break;
        case 2: s.slide = !s.slide; break;
        default:
            // swap with a neighbour: the line keeps its notes and shifts its rhythm
            std::swap(s.on, acid_[size_t(i == 15 ? 14 : i + 1)].on);
            std::swap(s.deg, acid_[size_t(i == 15 ? 14 : i + 1)].deg);
            break;
    }
}

bool YoursComposer::active(int track, int bar) const
{
    if (bar < 0 || bar >= totalBars()) return false;
    auto& sec = sectionAt(bar);
    if (!(sec.layers & bit(track))) return false;
    if (track == Kick && is(sec, "Ash")) return bar - sec.startBar < 16; // the kick leaves halfway through the ending
    return true;
}

float YoursComposer::levelAt(int track, int bar) const
{
    if (!active(track, bar)) return 0;
    auto& sec = sectionAt(bar);
    const int in = bar - sec.startBar, left = sec.startBar + sec.bars - bar;
    const float fade = track == Drone || track == Fx || track == Sub ? 4.0f : 2.0f;
    const float fadeIn = active(track, bar - in - 1) || (bar == 0 && track != Drone) ? 1.0f : std::min(1.0f, (in + 1) / fade);
    const float fadeOut = active(track, bar + left) ? 1.0f : std::min(1.0f, left / fade);
    float l = std::min(fadeIn, fadeOut);
    if (track == Fx && is(sec, "Rise")) l *= .3f + .7f * float(in + 1) / float(sec.bars);
    return l;
}

int YoursComposer::pitchOf(int bar, int deg, int lo, int hi, int base) const
{
    return foldInto(base + tonicPc_ + degreeSemis(kPhrygian, rootAt(bar) + deg), lo, hi);
}

void YoursComposer::emitAutomation(int bar, std::vector<Event>& out)
{
    const int64_t t0 = int64_t(bar) * kBarTicks;
    for (int tr = 0; tr < kYoursTracks; ++tr)
        out.push_back({ t0, Event::Level, uint8_t(tr), 0, levelAt(tr, bar) });

    auto& sec = sectionAt(bar);
    const float e0 = energyAt(bar), e1 = bar + 1 < totalBars() ? energyAt(bar + 1) : e0;
    const bool possessed = is(sec, "Possession");
    for (int beat = 0; beat < 4; ++beat)
    {
        const double e = e0 + (e1 - e0) * beat / 4.0;
        const double ramp = (bar - sec.startBar + beat / 4.0) / sec.bars; // 0..1 through the section
        const double pos = bar * 4 + beat;
        const double period = possessed ? 8 : 16;
        auto put = [&](int tr, double v) { out.push_back({ t0 + beat * kPPQ, Event::Filter, uint8_t(tr), 0, float(std::clamp(v, -1.0, 1.0)) }); };
        // the acid opens over the whole piece and breathes in 16-bar waves (8 at the peak)
        put(Acid, -0.95 + 1.7 * e + .3 * (ramp - .5) + (possessed ? .5 : .38) * std::sin(kTwoPi * pos / (period * 4)));
        put(Drone, -0.6 + 0.8 * e + .15 * std::sin(kTwoPi * pos / 128 + 1.1));
        put(Stab, -0.5 + 1.0 * e + .2 * std::sin(kTwoPi * pos / 32 + 2.0));
        put(Bass, -0.3 + 0.5 * e);
        put(Fx, is(sec, "Rise") ? -1.0 + 2.0 * ramp : -0.4 + .25 * std::sin(kTwoPi * pos / 64));
    }
}

void YoursComposer::emitHeld(int track, int bar, std::vector<Event>& out, int& held, int pitch)
{
    const int64_t t0 = int64_t(bar) * kBarTicks;
    auto& sec = sectionAt(bar);
    const bool retrigger = track == Fx && bar == sec.startBar; // the riser starts again with each section
    if (held >= 0 && (pitch != held || retrigger)) { out.push_back({ t0, Event::NoteOff, uint8_t(track), uint8_t(held), 0 }); held = -1; }
    if (pitch >= 0 && held < 0) { out.push_back({ t0, Event::NoteOn, uint8_t(track), uint8_t(pitch), .8f }); held = pitch; }
    if (bar + 1 == totalBars() && held >= 0) { out.push_back({ t0 + kBarTicks, Event::NoteOff, uint8_t(track), uint8_t(held), 0 }); held = -1; }
}

void YoursComposer::emitDrone(int bar, std::vector<Event>& out)
{
    const int64_t t0 = int64_t(bar) * kBarTicks;
    std::vector<int> want;
    if (active(Drone, bar))
    {
        // root, perfect fifth and octave; the minor (or, over bII, major) third two octaves up
        // once the music is driven
        const int r = rootAt(bar);
        const int lo = pitchOf(bar, 0, 36, 47, 36);
        const int fifth = degreeSemis(kPhrygian, r + 4) - degreeSemis(kPhrygian, r);
        const int third = degreeSemis(kPhrygian, r + 2) - degreeSemis(kPhrygian, r);
        want = { lo, lo + fifth, lo + 12 };
        if (energyAt(bar) > .5f) want.push_back(lo + 24 + third);
    }
    for (int p : droneHeld_)
        if (std::find(want.begin(), want.end(), p) == want.end()) out.push_back({ t0, Event::NoteOff, Drone, uint8_t(p), 0 });
    for (int p : want)
        if (std::find(droneHeld_.begin(), droneHeld_.end(), p) == droneHeld_.end()) out.push_back({ t0, Event::NoteOn, Drone, uint8_t(p), .75f });
    droneHeld_ = want;
    if (bar + 1 == totalBars()) // let go at the very end
    {
        for (int p : droneHeld_) out.push_back({ t0 + kBarTicks, Event::NoteOff, Drone, uint8_t(p), 0 });
        droneHeld_.clear();
    }
}

void YoursComposer::emitDrums(int bar, std::vector<Event>& out)
{
    const int64_t t0 = int64_t(bar) * kBarTicks;
    auto& sec = sectionAt(bar);
    const int in = bar - sec.startBar;
    const bool lastBar = in + 1 == sec.bars;
    const float e = energyAt(bar);
    auto hit = [&](int track, int64_t t, int pitch, float vel, int len) {
        out.push_back({ t, Event::NoteOn, uint8_t(track), uint8_t(pitch), std::clamp(vel, .05f, 1.0f) });
        out.push_back({ t + len, Event::NoteOff, uint8_t(track), uint8_t(pitch), 0 });
    };

    // The kick: four on the floor on the tonic. Before the Breakdown and the Possession it holds
    // its breath for half a bar; the Rise's last bar is silent.
    if (active(Kick, bar))
    {
        const bool beforeDrop = lastBar && bar + 1 < totalBars() && (!active(Kick, bar + 1) || sectionAt(bar + 1).restoreTheme);
        const int beats = is(sec, "Rise") && lastBar ? 0 : beforeDrop ? 2 : 4;
        const int pitch = foldInto(24 + tonicPc_, 24, 35);
        for (int b = 0; b < beats; ++b) hit(Kick, t0 + b * kPPQ, pitch, b == 0 ? 1.0f : .92f, kPPQ - 20);
    }

    if (active(Hats, bar))
    {
        const bool rise = is(sec, "Rise");
        const bool ratchet = sec.restoreTheme && in % 4 == 3; // thirty-seconds on the last beat
        for (int b = 0; b < 4; ++b)
        {
            const int64_t tb = t0 + b * kPPQ;
            if (ratchet && b == 3)
            {
                for (int k = 0; k < 8; ++k) hit(Hats, tb + k * kSixteenth / 2, 42, .25f + .07f * k, 60);
                continue;
            }
            hit(Hats, tb + 2 * kSixteenth, 46, .55f + .25f * e, 90); // the open hat between the kicks
            if (rise)
            {
                const float up = .2f + .6f * float(in * 4 + b) / float(sec.bars * 4);
                for (int s : { 0, 1, 3 }) hit(Hats, tb + s * kSixteenth, 42, up * (s == 0 ? .7f : 1.0f), 60);
            }
            else if (e >= .55f)
            {
                hit(Hats, tb + kSixteenth, 42, .30f, 60);
                hit(Hats, tb + 3 * kSixteenth, 42, .48f, 60);
                if (e >= .85f) hit(Hats, tb, 42, .22f, 60);
            }
        }
    }

    if (active(Clap, bar))
    {
        if (is(sec, "Rise") && in >= 8)
        {
            // the roll: eighths, then sixteenths, then thirty-seconds, louder all the way
            const int div = in < 12 ? 2 * kSixteenth : in < 15 ? kSixteenth : kSixteenth / 2;
            for (int64_t t = 0; t < kBarTicks; t += div)
                hit(Clap, t0 + t, 39, .35f + .6f * float((in - 8) * kBarTicks + t) / float(8 * kBarTicks), 50);
        }
        else
        {
            hit(Clap, t0 + kPPQ, 39, .85f, 60);
            hit(Clap, t0 + 3 * kPPQ, 39, .9f, 60);
            if (sec.restoreTheme && rng_.chance(.3)) hit(Clap, t0 + 3 * kPPQ - kSixteenth, 39, .3f, 50);
        }
    }
}

void YoursComposer::emitBass(int bar, std::vector<Event>& out)
{
    if (!active(Bass, bar)) return;
    const int64_t t0 = int64_t(bar) * kBarTicks;
    auto& sec = sectionAt(bar);
    const float e = energyAt(bar);
    const int p = pitchOf(bar, 0, 40, 51, 36); // written an octave up: the bass patches sound an octave down
    auto note = [&](int64_t t, int pitch, float vel, int len) {
        out.push_back({ t, Event::NoteOn, Bass, uint8_t(pitch), vel });
        out.push_back({ t + len, Event::NoteOff, Bass, uint8_t(pitch), 0 });
    };
    for (int b = 0; b < 4; ++b)
    {
        const int64_t tb = t0 + b * kPPQ;
        if (e < .5f) { note(tb + 2 * kSixteenth, p, .85f, kSixteenth * 3 / 2); continue; } // offbeat eighths
        // rolling: the three sixteenths between the kicks; at the peak the last one jumps the octave
        note(tb + kSixteenth, p, .66f, kSixteenth * 3 / 4);
        note(tb + 2 * kSixteenth, p, .92f, kSixteenth * 3 / 4);
        const bool up = sec.restoreTheme && b == 3 && bar % 2 == 1;
        note(tb + 3 * kSixteenth, up ? p + 12 : p, .72f, kSixteenth * 3 / 4);
    }
}

void YoursComposer::emitAcid(int bar, std::vector<Event>& out)
{
    if (!active(Acid, bar)) return;
    auto& sec = sectionAt(bar);
    const int in = bar - sec.startBar;
    if (sec.restoreTheme && in == 0) acid_ = acidHome_;               // the riff comes back for the peak
    else if (in % 4 == 0 && rng_.chance(.35 + .5 * energyAt(bar))) { mutateAcid(); if (rng_.chance(.4)) mutateAcid(); }

    const int64_t t0 = int64_t(bar) * kBarTicks;
    const int count = std::clamp(6 + int(10 * energyAt(bar) + .5f), 6, 16);
    const bool high = sec.restoreTheme && in >= 32; // the second half of the peak: accents leap up an octave
    auto plays = [&](int i) { return i < 16 && acid_[size_t(i)].on && acid_[size_t(i)].rank < count; };
    auto pitchAt = [&](int i) {
        auto& s = acid_[size_t(i)];
        return pitchOf(bar, s.deg, 33, 64, 36) + (high && s.accent ? 12 : 0);
    };
    for (int i = 0; i < 16; ++i)
    {
        if (!plays(i)) continue;
        auto& s = acid_[size_t(i)];
        const int p = pitchAt(i);
        int len = 130;
        if (s.slide && plays(i + 1)) len = pitchAt(i + 1) == p ? kSixteenth : kSixteenth + 12; // a slide: the next note starts before this ends
        const float vel = s.accent ? 1.0f : .58f + .1f * energyAt(bar);
        out.push_back({ t0 + i * kSixteenth, Event::NoteOn, Acid, uint8_t(p), vel });
        out.push_back({ t0 + i * kSixteenth + len, Event::NoteOff, Acid, uint8_t(p), 0 });
    }
}

void YoursComposer::emitStab(int bar, std::vector<Event>& out)
{
    if (!active(Stab, bar)) return;
    auto& sec = sectionAt(bar);
    if (bar == sec.startBar) stabRot_ = rng_.range(0, 31);
    const int64_t t0 = int64_t(bar) * kBarTicks;
    // a Euclidean rhythm over two bars, against the four of the kick
    const int k = 5 + int(6 * energyAt(bar));
    const int r = rootAt(bar);
    const int root = pitchOf(bar, 0, 48, 59, 48);
    std::vector<int> chord = { root, root + degreeSemis(kPhrygian, r + 2) - degreeSemis(kPhrygian, r),
                               root + degreeSemis(kPhrygian, r + 4) - degreeSemis(kPhrygian, r) };
    if (sec.restoreTheme) chord.push_back(root + 12);
    for (int i = 0; i < 16; ++i)
    {
        const int s = (bar % 2) * 16 + i;
        if (((s + stabRot_) * k) % 32 >= k) continue;
        const float vel = i % 4 == 0 ? .9f : .62f;
        for (int p : chord)
        {
            out.push_back({ t0 + i * kSixteenth, Event::NoteOn, Stab, uint8_t(p), vel });
            out.push_back({ t0 + i * kSixteenth + 160, Event::NoteOff, Stab, uint8_t(p), 0 });
        }
    }
}

void YoursComposer::generateBar(int bar, std::vector<Event>& out)
{
    if (bar < 0 || bar >= totalBars()) return;
    emitAutomation(bar, out);
    emitDrone(bar, out);
    emitDrums(bar, out);
    emitBass(bar, out);
    emitAcid(bar, out);
    emitStab(bar, out);
    emitHeld(Sub, bar, out, subHeld_, active(Sub, bar) ? pitchOf(bar, 0, 24, 35, 24) : -1);
    emitHeld(Fx, bar, out, fxHeld_, active(Fx, bar) ? foldInto(48 + tonicPc_, 48, 59) : -1);
}

std::string YoursComposer::describe() const
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double secs = totalBars() * 4 * 60.0 / bpm_;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s phrygian · %.0f bpm · %d bars · %d:%02d", names[tonicPc_], bpm_, totalBars(), int(secs) / 60, int(secs) % 60);
    return buf;
}

} // namespace liszt
