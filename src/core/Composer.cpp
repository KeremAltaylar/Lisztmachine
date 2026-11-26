#include "Composer.h"
#include "Genres.h"
#include "Improv.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace liszt {

namespace {

constexpr uint8_t bit(int t) { return uint8_t(1u << t); }
constexpr uint8_t kAll = 0x7F;

// Chord-root transitions in the minor modes, rows/cols = i ii III iv v VI VII.
// ii is never a target; its row only exists so every row is a distribution.
constexpr std::array<std::array<double, 7>, 7> kNext = { {
    { 0, 0, .10, .25, .10, .30, .25 },
    { .5, 0, 0, 0, .5, 0, 0 },
    { 0, 0, 0, .30, 0, .40, .30 },
    { .40, 0, 0, 0, .15, .15, .30 },
    { .60, 0, 0, 0, 0, .40, 0 },
    { .30, 0, .10, .25, 0, 0, .35 },
    { .55, 0, .20, 0, 0, .25, 0 },
} };

// The chord-tone ladder the upper sequence walks on, in degrees above the chord root.
constexpr std::array<int, 7> kLadder = { 0, 2, 4, 7, 9, 11, 14 };

template <class T>
void rankByPriority(std::vector<Step>& p, Rng& rng, T priority)
{
    std::vector<std::pair<double, int>> order;
    for (int i = 0; i < int(p.size()); ++i)
        order.push_back({ priority(i) + rng.uniform(), i });
    std::sort(order.begin(), order.end());
    for (int r = 0; r < int(order.size()); ++r)
        p[size_t(order[size_t(r)].second)].rank = r;
}

} // namespace

BerlinComposer::BerlinComposer(uint64_t seed) : Composer(seed)
{
    static constexpr int tonics[] = { 4, 9, 2, 7, 0, 5 }; // E A D G C F
    leadRng_ = Rng(seed * 0x9E3779B97F4A7C15ull + 7);
    booms_ = Sentence(seed);
    tonicPc_ = tonics[rng_.range(0, 5)];
    // a minor home, melancholic; the shift is the one lighter moment (aeolian to dorian, phrygian to aeolian)
    homeMode_ = rng_.chance(.6) ? &kAeolian : &kPhrygian;
    shiftMode_ = homeMode_ == &kAeolian ? &kDorian : &kAeolian;
    bpm_ = 2.0 * rng_.range(51, 56); // 102..112: slower, so the sequences breathe

    planForm();
    std::vector<bool> stops(sections_.size(), false); // a loud part into a big one
    for (size_t si = 1; si < sections_.size(); ++si) stops[si] = sections_[si - 1].energy >= .45f && sections_[si].energy >= .6f;
    turns_ = planTurns(stops, false);
    planHarmony();
    makeBassPattern();
    makeSeqPattern();
    makeMotif();
    bassHome_ = bass_;
    seqHome_ = seq_;
}

void BerlinComposer::planForm()
{
    struct T { const char* name; int bars; float energy; uint16_t layers; bool theme; };
    static const T plan[] = {
        // 2026-11-02 (Kerem: "shorten Berlin"): 288 bars to 176, about six and a half minutes; the Return
        // is gone and the quiet parts are half as long. The soloist answers the lead and has the Shift to itself.
        // 2026-11-04 (Kerem: "shorten the songs"): 176 to 144: the Song, the Theme and the Summit 24 bars, the Shift 8.
        { "Drift",      8, .15f, uint8_t(bit(Pad) | bit(Solo)), false },
        { "Pulse",      8, .30f, uint8_t(bit(Pad) | bit(Bass) | bit(Solo)), false },
        { "Motion",    16, .50f, uint8_t(bit(Pad) | bit(Bass) | bit(Seq) | bit(Solo)), false },
        { "Song",      24, .62f, kAll, false },
        { "Haze",       8, .35f, uint8_t(bit(Pad) | bit(Seq) | bit(Solo)), false },
        { "Theme",     24, .82f, kAll, true },
        { "Shift",      8, .70f, uint8_t(kAll & ~bit(Lead)), false },
        { "Summit",    24, 1.0f, kAll, true },
        { "Dissolve",  16, .30f, uint8_t(bit(Pad) | bit(Seq) | bit(Solo)), false },
        { "Afterglow",  8, .05f, uint8_t(bit(Pad) | bit(Solo)), false },
    };

    int bar = 0;
    float prevEnergy = 0;
    for (auto& t : plan)
    {
        sections_.push_back({ t.name, bar, t.bars, t.energy, t.layers, t.theme });
        const Mode* mode = std::string(t.name) == "Shift" ? shiftMode_ : homeMode_;
        for (int i = 0; i < t.bars; ++i)
        {
            sectionOfBar_.push_back(int(sections_.size()) - 1);
            modeOfBar_.push_back(mode);
            float k = std::min(1.0f, float(i + 1) / (t.bars * 0.5f));
            energyOfBar_.push_back(prevEnergy + (t.energy - prevEnergy) * k);
        }
        bar += t.bars;
        prevEnergy = t.energy;
    }
}

void BerlinComposer::planHarmony()
{
    chordOfBar_.assign(sectionOfBar_.size(), 0);
    for (size_t s = 0; s < sections_.size(); ++s)
    {
        auto& sec = sections_[s];
        if (s + 1 == sections_.size()) continue; // the ending holds the tonic

        // Harmonic rhythm follows energy: eight bars per chord when calm, two at the peak.
        int region = std::min(sec.bars, sec.energy < .4f ? 8 : sec.energy < .75f ? 4 : 2);
        int count = sec.bars / region;
        int prev = 0;
        for (int r = 0; r < count; ++r)
        {
            int c = r == 0 ? 0
                  : r == count - 1 ? (rng_.chance(.65) ? 6 : 4) // cadence into the next section's i
                  : rng_.weighted(kNext[size_t(prev)]);
            // A diminished triad has no place in this music: dorian's vi° becomes IV,
            // phrygian's v° becomes VII.
            const Mode& m = *modeOfBar_[size_t(sec.startBar)];
            if (degreeSemis(m, c + 4) - degreeSemis(m, c) != 7)
                c = c == 4 ? 6 : 3;
            for (int b = 0; b < region; ++b)
                chordOfBar_[size_t(sec.startBar + r * region + b)] = c;
            prev = c;
        }
    }
}

void BerlinComposer::makeBassPattern()
{
    static constexpr std::array<int, 3> beatDegs = { 0, 4, 7 };
    static constexpr std::array<double, 3> beatW = { .55, .25, .20 };
    static constexpr std::array<int, 5> offDegs = { 0, 7, 4, 2, -1 };
    static constexpr std::array<double, 5> offW = { .35, .30, .15, .10, .10 };

    bass_.assign(16, {});
    for (int i = 0; i < 16; ++i)
    {
        auto& s = bass_[size_t(i)];
        bool beat = i % 4 == 0, eighth = i % 2 == 0;
        s.on = i == 0 || rng_.chance(beat ? .95 : eighth ? .8 : .55);
        s.deg = i == 0 ? 0 : beat ? beatDegs[size_t(rng_.weighted(beatW))] : offDegs[size_t(rng_.weighted(offW))];
        s.accent = i == 0 || (beat && rng_.chance(.3));
        s.gate = float(.45 + .35 * rng_.uniform());
    }
    rankByPriority(bass_, rng_, [](int i) { return i == 0 ? -10.0 : i % 4 == 0 ? -2.0 : i % 2 == 0 ? -1.0 : 0.0; });
}

void BerlinComposer::makeSeqPattern()
{
    static constexpr std::array<int, 4> lengths = { 12, 10, 14, 16 };
    int len = lengths[size_t(rng_.weighted(std::array<double, 4> { .35, .2, .2, .25 }))];
    seq_.assign(size_t(len), {});
    int rung = rng_.range(0, 2);
    for (int i = 0; i < len; ++i)
    {
        auto& s = seq_[size_t(i)];
        if (i > 0)
        {
            int step = rng_.chance(.15) ? 2 : 1;
            rung += rng_.chance(.5) ? step : -step;
            if (rung < 0) rung = -rung;
            if (rung > 6) rung = 12 - rung;
        }
        s.deg = kLadder[size_t(rung)];
        if (i % 2 == 1 && rng_.chance(.2)) s.deg += rng_.chance(.5) ? 1 : -1; // passing tone, weak step only
        s.on = i == 0 || rng_.chance(.9);
        s.accent = i == 0 || rng_.chance(.2);
        s.gate = float(.35 + .35 * rng_.uniform());
    }
    rankByPriority(seq_, rng_, [](int i) { return i == 0 ? -10.0 : i % 2 == 0 ? -1.0 : 0.0; });
}

void BerlinComposer::makeMotif()
{
    // Rhythms in sixteenths, each filling two bars: long notes, the occasional pickup.
    static const std::vector<std::vector<int>> rhythms = {
        { 12, 4, 8, 8 }, { 8, 4, 4, 16 }, { 6, 2, 8, 16 }, { 16, 8, 8 }, { 8, 8, 4, 4, 8 }, { 4, 4, 8, 16 },
    };
    static constexpr std::array<int, 5> steps = { -2, -1, 1, 2, 3 };
    static constexpr std::array<double, 5> stepW = { .15, .30, .30, .15, .10 };

    auto& r = rhythms[size_t(rng_.range(0, int(rhythms.size()) - 1))];
    int at = 0;
    int deg = std::array<int, 3> { 0, 2, 4 }[size_t(rng_.range(0, 2))];
    for (size_t k = 0; k < r.size(); ++k)
    {
        if (k > 0)
        {
            int s = steps[size_t(rng_.weighted(stepW))];
            bool rising = k < r.size() / 2; // an arch: up, then home
            if ((rising && s < 0) || (!rising && s > 0))
                if (rng_.chance(.5)) s = -s;
            deg += s;
        }
        motif_.push_back({ at * kSixteenth, r[k] * kSixteenth, deg });
        at += r[k];
    }
}

void BerlinComposer::mutate(std::vector<Step>& p, double chance, bool isBass)
{
    if (!rng_.chance(chance)) return;
    int i = rng_.range(1, int(p.size()) - 1); // step 0 is the spine and never moves
    auto& s = p[size_t(i)];
    if (rng_.chance(.4))
        s.on = !s.on;
    else if (isBass)
        s.deg = std::array<int, 4> { 0, 7, 4, 2 }[size_t(rng_.range(0, 3))];
    else
        s.deg = kLadder[size_t(rng_.range(0, 6))];
}

std::vector<Turn> Composer::planTurns(const std::vector<bool>& stopInto, bool pedal) const
{
    std::vector<Turn> t(sections_.size(), Turn::None);
    int hole = -1;
    float most = 0;
    for (size_t si = 1; si < sections_.size(); ++si)
    {
        const float d = sections_[si].energy - sections_[si - 1].energy; // a fifth of the range either way is level
        t[si] = stopInto[si] ? Turn::Stop : d > .21f ? Turn::Pickup : d < -.21f ? Turn::Echo : pedal ? Turn::Pedal : Turn::Thin;
        if (t[si] == Turn::Pickup && d > most) most = d, hole = int(si);
    }
    if (hole > 0) t[size_t(hole)] = Turn::Hole;
    return t;
}

bool BerlinComposer::active(int track, int bar) const
{
    return bar >= 0 && bar < totalBars() && (sectionAt(bar).layers & bit(track));
}

float BerlinComposer::levelAt(int track, int bar) const
{
    if (track == Boom) return 1; // its hits follow the parts themselves
    if (!active(track, bar)) return 0;
    auto& sec = sectionAt(bar);
    if (sec.energy >= .6f) return 1; // a big part starts on its whole sound and ends on the stop
    int in = bar - sec.startBar, left = sec.startBar + sec.bars - bar;
    float fadeIn = active(track, sec.startBar - 1) ? 1.0f : std::min(1.0f, (in + 1) / 4.0f);
    float fadeOut = active(track, sec.startBar + sec.bars) ? 1.0f : std::min(1.0f, (left - 1) / 4.0f);
    return std::min(fadeIn, fadeOut);
}

int BerlinComposer::chordRootPitch(int track, int bar) const
{
    int semis = degreeSemis(modeAt(bar), chordAt(bar));
    if (track == Bass) { int p = 36 + tonicPc_ + semis; return p > 45 ? p - 12 : p; }
    int p = 60 + tonicPc_ + semis;
    return p > 67 ? p - 12 : p;
}

int BerlinComposer::stepPitch(int track, int bar, int deg) const
{
    auto& m = modeAt(bar);
    int c = chordAt(bar);
    int p = chordRootPitch(track, bar) + degreeSemis(m, c + deg) - degreeSemis(m, c);
    return foldInto(p, kRange[track][0], kRange[track][1]);
}

void BerlinComposer::emitAutomation(int bar, std::vector<Event>& out)
{
    const int64_t t0 = int64_t(bar) * kBarTicks;
    for (int tr = 0; tr < kNumTracks; ++tr)
        // a part entering a big part is at full level by its downbeat (the level glides over a bar)
        out.push_back({ t0, Event::Level, uint8_t(tr), 0, bigStart(bar + 1) && active(tr, bar + 1) ? 1.0f : levelAt(tr, bar) });

    // Slow filter sweeps, each a whole number of bars long (16 or 32), riding on energy.
    struct Sweep { int track; double periodBars, amp, phase; };
    static constexpr Sweep sweeps[] = { { Pad, 32, .20, 1.3 }, { Bass, 32, .15, 0 }, { Seq, 16, .35, 0 } };
    const float e0 = energyAt(bar), e1 = bar + 1 < totalBars() ? energyAt(bar + 1) : e0;
    for (auto& s : sweeps)
        for (int beat = 0; beat < 4; ++beat)
        {
            double e = e0 + (e1 - e0) * beat / 4.0;
            double pos = (bar * 4 + beat) / (s.periodBars * 4);
            double v = (e - .5) * 1.1 + s.amp * std::sin(6.283185307179586 * pos + s.phase);
            out.push_back({ t0 + beat * kPPQ, Event::Filter, uint8_t(s.track), 0, float(std::clamp(v, -1.0, 1.0)) });
        }
}

void BerlinComposer::emitPad(int bar, std::vector<Event>& out)
{
    const int64_t t = int64_t(bar) * kBarTicks;
    auto release = [&](int64_t at) {
        for (size_t v = 0; v < 4; ++v) if (!padGone_[v]) out.push_back({ at, Event::NoteOff, Pad, uint8_t(padVoices_[v]), 0 });
        padChord_ = -1;
        padGone_ = {};
    };

    if (!active(Pad, bar)) { if (padChord_ >= 0) release(t); return; }
    const bool loud = bigStart(bar) || stopBar(bar);
    if (loud && padChord_ >= 0) release(t); // struck again, all of it

    const int c = chordAt(bar);
    if (c != padChord_)
    {
        auto& m = modeAt(bar);
        const bool wasSounding = padChord_ >= 0;
        if (!wasSounding) padVoices_ = { 55, 59, 62, 67 };

        // Voice-lead: each voice takes the nearest pitch of its new chord tone; common tones hold.
        const std::array<int, 4> degs = { c, c + 2, c + 4, energyAt(bar) > .5f ? c + 6 : c + 7 };
        std::array<int, 4> next {};
        std::array<int, 4> old = padVoices_;
        std::sort(old.begin(), old.end());
        for (size_t v = 0; v < 4; ++v)
        {
            int pc = (tonicPc_ + degreeSemis(m, degs[v])) % 12;
            int best = -1;
            for (int p = kRange[Pad][0]; p <= kRange[Pad][1]; ++p)
                if (p % 12 == pc && std::find(next.begin(), next.begin() + long(v), p) == next.begin() + long(v)
                    && (best < 0 || std::abs(p - old[v]) < std::abs(best - old[v])))
                    best = p;
            next[v] = best;
        }

        const float vel = loud ? .9f : .45f + .2f * energyAt(bar);
        for (size_t v = 0; v < 4; ++v)
            if (wasSounding && !padGone_[v] && std::find(next.begin(), next.end(), padVoices_[v]) == next.end())
                out.push_back({ t, Event::NoteOff, Pad, uint8_t(padVoices_[v]), 0 });
        padGone_ = {};
        for (int p : next)
            if (!wasSounding || std::find(padVoices_.begin(), padVoices_.end(), p) == padVoices_.end())
                out.push_back({ t, Event::NoteOn, Pad, uint8_t(p), vel });
        padVoices_ = next;
        padChord_ = c;
        padStruck_ = bar;
    }
    else if (bar - padStruck_ >= 4)
    {
        // The sampled horns and tuba fade out within 14 s: a chord held longer is struck again every
        // four bars, or a long chord in the calm parts went silent (20 s of nothing in the Pulse).
        const float vel = .45f + .2f * energyAt(bar);
        for (int p : padVoices_) out.push_back({ t, Event::NoteOff, Pad, uint8_t(p), 0 });
        for (int p : padVoices_) out.push_back({ t, Event::NoteOn, Pad, uint8_t(p), vel });
        padStruck_ = bar;
    }

    if (stopBar(bar)) release(t + kHit - 20);
    else if (bar == totalBars() - 1) release(t + kBarTicks);
    else if (turnAt(bar) == Turn::Echo && chordAt(bar + 1) != c)
    {   // the pivot: through the silence only the notes the next chord shares ring on (if it shares any)
        auto& nm = modeAt(bar + 1);
        const int nc = chordAt(bar + 1);
        auto shared = [&](int p) { for (int d : { 0, 2, 4, 6 }) if ((tonicPc_ + degreeSemis(nm, nc + d)) % 12 == p % 12) return true; return false; };
        if (std::any_of(padVoices_.begin(), padVoices_.end(), shared))
            for (size_t v = 0; v < 4; ++v)
                if (!shared(padVoices_[v])) { out.push_back({ t + 12 * kSixteenth, Event::NoteOff, Pad, uint8_t(padVoices_[v]), 0 }); padGone_[v] = true; }
    }
}

void BerlinComposer::emitSequence(int track, int bar, std::vector<Event>& out)
{
    const auto& pat = track == Bass ? bass_ : seq_;
    const float e = energyAt(bar);
    const int len = int(pat.size());
    const int limit = std::max(1, int(std::lround((track == Bass ? .5 + .5 * e : .45 + .55 * e) * len)));
    const bool summit = std::strcmp(sectionAt(bar).name, "Summit") == 0;
    const char* fig = endFigure(bar);
    if (turnAt(bar) == Turn::Thin) // thinning into the next part: the upper sequence gone, the bass one long root
    {
        if (track == Bass)
        {
            const int p = stepPitch(Bass, bar, 0);
            out.push_back({ int64_t(bar) * kBarTicks, Event::NoteOn, Bass, uint8_t(p), .7f });
            out.push_back({ int64_t(bar + 1) * kBarTicks - 20, Event::NoteOff, Bass, uint8_t(p), 0 });
        }
        return;
    }
    if (fig && track == Bass)
    {
        const int p = stepPitch(Bass, bar, 0);
        for (int i = 0; i < 16; ++i)
        {
            if (fig[i] == '.') continue;
            int next = i + 1;
            while (next < 16 && fig[next] == '.') ++next;
            const int64_t t = int64_t(bar) * kBarTicks + i * kSixteenth;
            out.push_back({ t, Event::NoteOn, Bass, uint8_t(p), fig[i] == 'X' ? .95f : .8f });
            out.push_back({ t + std::min(next - i, 3) * kSixteenth - 20, Event::NoteOff, Bass, uint8_t(p), 0 });
        }
        return;
    }
    if (track == Seq && stopBar(bar))
    {
        const int64_t t = int64_t(bar) * kBarTicks;
        const int p = stepPitch(Seq, bar, 0);
        out.push_back({ t, Event::NoteOn, Seq, uint8_t(p), .95f });
        out.push_back({ t + kHit - 20, Event::NoteOff, Seq, uint8_t(p), 0 });
        return;
    }
    const int stop = turnAt(bar) == Turn::Hole ? 0 : fig ? 4 : 16; // the upper sequence stops after the figure's first beat

    for (int i = 0; i < stop; ++i)
    {
        // The upper sequence runs in its own length (10, 12, 14) against the 16-step bar.
        const auto& s = pat[size_t(track == Bass ? i : (bar * 16 + i) % len)];
        const bool downbeat = i == 0 && track == Seq && bigStart(bar); // a big part's first downbeat always sounds
        if (!downbeat && (!s.on || s.rank >= limit)) continue;
        const int64_t t = int64_t(bar) * kBarTicks + i * kSixteenth;
        const int64_t off = t + int64_t((downbeat && !s.on ? .9f : s.gate) * kSixteenth);
        float vel = std::clamp(float(.5 + .35 * e + (s.accent ? .15 : 0) + .06 * (rng_.uniform() - .5) + (i == 0 && bigStart(bar) ? .35 : 0)), .05f, 1.0f);
        int p = stepPitch(track, bar, s.deg);
        out.push_back({ t, Event::NoteOn, uint8_t(track), uint8_t(p), vel });
        out.push_back({ off, Event::NoteOff, uint8_t(track), uint8_t(p), 0 });
        if (summit && s.accent && track == Seq)
        {
            int p2 = stepPitch(track, bar, s.deg + 7);
            if (p2 != p)
            {
                out.push_back({ t, Event::NoteOn, uint8_t(track), uint8_t(p2), vel * .8f });
                out.push_back({ off, Event::NoteOff, uint8_t(track), uint8_t(p2), 0 });
            }
        }
    }
}

void BerlinComposer::emitLeadPhrase(int bar, std::vector<Event>& out)
{
    auto& sec = sectionAt(bar);
    if (soloFloor(bar)) return; // the soloist's turn

    const bool summit = std::strcmp(sec.name, "Summit") == 0;
    for (int half = 0; half < 2; ++half)
    {
        const int b0 = bar + 2 * half;
        const bool invert = half == 1 && rng_.chance(.3);
        const int tweak = rng_.chance(.25) ? rng_.range(1, int(motif_.size()) - 1) : -1;
        for (size_t k = 0; k < motif_.size(); ++k)
        {
            auto n = motif_[k];
            const int nb = b0 + n.start / kBarTicks;
            const int c = chordAt(nb);
            int rel = invert ? motif_[0].deg - (n.deg - motif_[0].deg) : n.deg;
            if (int(k) == tweak) rel += rng_.chance(.5) ? 1 : -1;
            int deg = c + rel;

            // Strong beats (1 and 3) land on chord tones; the answer cadences on the root.
            const bool last = half == 1 && k + 1 == motif_.size();
            if (last || n.start % (2 * kPPQ) == 0)
            {
                int best = deg, bestDist = 99;
                for (int d = deg - 3; d <= deg + 3; ++d)
                {
                    int r = ((d - c) % 7 + 7) % 7;
                    if ((last ? r == 0 : (r == 0 || r == 2 || r == 4)) && std::abs(d - deg) < bestDist)
                        best = d, bestDist = std::abs(d - deg);
                }
                deg = best;
            }

            int p = 60 + tonicPc_ + degreeSemis(modeAt(nb), deg) + (summit ? 12 : 0);
            p = foldInto(p, kRange[Lead][0], kRange[Lead][1]);
            const int64_t t = int64_t(b0) * kBarTicks + n.start;
            const float vel = std::clamp(.55f + .3f * energyAt(nb) + (k == 0 ? .08f : 0.0f), 0.0f, 1.0f);
            int64_t off = t + int64_t(n.dur * 0.92);
            if (!clip(t, off)) continue;
            out.push_back({ t, Event::NoteOn, Lead, uint8_t(p), vel });
            out.push_back({ off, Event::NoteOff, Lead, uint8_t(p), 0 });
        }
    }
}

void BerlinComposer::generateBar(int bar, std::vector<Event>& out)
{
    if (bar < 0 || bar >= totalBars()) return;
    auto& sec = sectionAt(bar);
    if (bar == sec.startBar && sec.restoreTheme)
    {
        bass_ = bassHome_;
        seq_ = seqHome_;
    }
    else
    {
        mutate(bass_, .06, true);
        mutate(seq_, .12 * (.5 + energyAt(bar)), false);
    }

    emitAutomation(bar, out);
    emitPad(bar, out);
    if (active(Bass, bar)) emitSequence(Bass, bar, out);
    if (active(Seq, bar)) emitSequence(Seq, bar, out);
    if (active(Lead, bar) && (bar - sec.startBar) % 4 == 0) emitLeadPhrase(bar, out);
    emitSolo(bar, out);
    emitBoom(bar, out);
    emitGuitar(bar, out);
    emitSub(bar, out);
    const int64_t t0 = int64_t(bar) * kBarTicks;
    if (turnAt(bar) == Turn::Hole) emitHint(bar, out);
    if (turnAt(bar) == Turn::Echo) // the beat before the silence goes into the echoes
        for (int tr : { Seq, Lead, Guitar, Solo }) out.push_back({ t0 + 8 * kSixteenth, Event::Throw, uint8_t(tr), 0, 1.0f });
    if (const int64_t s = silenceFrom(bar); s >= 0)
    {
        // a stop is silent, all of it; the other changes keep a part sounding (Kerem, 2026-11-06: "one
        // component can remain"), and the falls and the hole let the tails ring
        const uint8_t tails = turnAt(bar) == Turn::Hole ? 2 | 4 : turnAt(bar) == Turn::Echo ? 2 : 0; // the hole: what holds on, softly
        bool any = false;
        for (int tr = 0; tr < kNumTracks; ++tr)
            if (keeps(bar, tr)) { out.push_back({ s, Event::Hush, uint8_t(tr), uint8_t(1 | tails), 0 }); any = true; }
        if (!any) out.push_back({ s, Event::Hush, 0, tails, 0 });
        out.push_back({ t0 + kBarTicks, Event::Hush, 0, 0, 1 });
    }
}

const char* BerlinComposer::endFigure(int bar) const
{
    // The last bar of every part but the final one: what the bass and the guitar play together. All start
    // on the downbeat (the bass keeps its spine), all on the chord they are in, straight (no threes).
    switch (turnAt(bar))
    {
        case Turn::Stop: return "X...............";
        case Turn::Pickup: return "X.......x.x.....";
        case Turn::Echo: return sectionOfBar_[size_t(bar)] % 2 ? "X...x.x.X......." : "X.x.X...X......."; // the last into the echoes
        case Turn::Hole: return "................";
        default: return nullptr;
    }
}

void BerlinComposer::emitGuitar(int bar, std::vector<Event>& out)
{
    // Power chords on the chord's root (the mode's own fifth over it), let ring: one every other bar
    // while the music is calm, on one and three as it drives, and at the summit a muted pair leading
    // into each bar. A part's last bar plays its figure. Under .5 the engine's guitar mutes the string.
    if (!active(Guitar, bar) || turnAt(bar) == Turn::Thin) return;
    const int64_t t0 = int64_t(bar) * kBarTicks, six = kSixteenth;
    auto& m = modeAt(bar);
    const int c = chordAt(bar);
    const int root = foldInto(24 + tonicPc_ + degreeSemis(m, c), 33, 44), fifth = degreeSemis(m, c + 4) - degreeSemis(m, c);
    auto chord = [&](int64_t t, int64_t len, float vel) {
        for (int iv : { 0, fifth, 12 })
        {
            if (vel < .5f && iv == 12) continue;
            out.push_back({ t, Event::NoteOn, Guitar, uint8_t(root + iv), vel });
            out.push_back({ t + len, Event::NoteOff, Guitar, uint8_t(root + iv), 0 });
        }
    };
    if (const char* fig = endFigure(bar))
    {
        for (int i = 0; i < 16; ++i)
        {
            if (fig[i] == '.') continue;
            int next = i + 1;
            while (next < 16 && fig[next] == '.') ++next;
            chord(t0 + i * six, std::min(next - i, 3) * six - 20, fig[i] == 'X' ? .95f : .8f);
        }
        return;
    }
    if (bigStart(bar)) { chord(t0, kBarTicks - 20, .95f); return; }
    const float e = energyAt(bar);
    const bool summit = std::strcmp(sectionAt(bar).name, "Summit") == 0;
    const int in = bar - sectionAt(bar).startBar;
    if (e < .7f) { if (in % 2 == 0 || (bar > 0 && chordAt(bar - 1) != c)) chord(t0, kBarTicks - 20, .55f + .3f * e); }
    else
    {
        chord(t0, 2 * kPPQ - 20, .9f);
        chord(t0 + 2 * kPPQ, (summit ? 6 : 8) * six - 20, .8f);
        if (summit) { chord(t0 + 14 * six, six - 20, .38f); chord(t0 + 15 * six, six - 20, .42f); }
    }
}

bool BerlinComposer::stopBar(int bar) const
{
    // Kerem, 2026-11-02: "attacks that establish silence at the end of the parts which are just big, and
    // those main parts start on a big sound": one attack of the whole band on the downbeat of the last bar,
    // then nothing until the next part's downbeat; since 2026-11-07 only a loud part going into a big one.
    return turnAt(bar) == Turn::Stop;
}

Turn BerlinComposer::turnAt(int bar) const
{
    if (bar < 0 || bar + 1 >= totalBars() || sectionOfBar_[size_t(bar + 1)] == sectionOfBar_[size_t(bar)]) return Turn::None;
    return turns_[size_t(sectionOfBar_[size_t(bar + 1)])];
}

bool BerlinComposer::keeps(int bar, int track) const
{
    // through a part change's silence the pad holds on, and in the hole the lead hints at the theme; a fall
    // leaves only the echoes (Kerem, 2026-11-07: the held pad blurred the start of the quiet part); the
    // soloist walks through a rise into the next part (2026-11-09)
    const Turn t = turnAt(bar);
    return (track == Pad && (t == Turn::Pickup || t == Turn::Hole)) || (track == Lead && t == Turn::Hole)
        || (track == Solo && t == Turn::Pickup);
}

bool BerlinComposer::bigStart(int bar) const
{
    if (bar < 0 || bar >= totalBars()) return false;
    auto& sec = sectionAt(bar);
    return bar == sec.startBar && sec.energy >= .6f;
}

int64_t BerlinComposer::silenceFrom(int bar) const
{
    // after the stop's hit; the last beat of a rise or a fall; the whole bar of the hole
    const int64_t t0 = int64_t(bar) * kBarTicks;
    switch (turnAt(bar))
    {
        case Turn::Stop: return t0 + kHit;
        case Turn::Pickup: case Turn::Echo: return t0 + 12 * kSixteenth;
        case Turn::Hole: return t0;
        default: return -1;
    }
}

bool BerlinComposer::clip(int64_t on, int64_t& off) const
{
    // A note that would begin in a silence is dropped; one ringing into it ends where the silence begins.
    const int b = int(on / kBarTicks);
    if (const int64_t s = silenceFrom(b); s >= 0 && on >= s) return false;
    for (int x = b; x <= int(off / kBarTicks) && x < totalBars(); ++x)
        if (const int64_t s = silenceFrom(x); s >= 0) off = std::min(off, s - 20);
    return off > on;
}

void BerlinComposer::emitHint(int bar, std::vector<Event>& out)
{
    // the hole before the theme comes back: its first notes (as many as the bar holds, three at most), the
    // lead alone over the pad while Kerem fills; on the beats chord tones, as the lead plays them
    const int64_t t0 = int64_t(bar) * kBarTicks;
    const int c = chordAt(bar);
    for (size_t k = 0; k < motif_.size() && k < 3 && motif_[k].start < kBarTicks; ++k)
    {
        int deg = c + motif_[k].deg;
        if (motif_[k].start % (2 * kPPQ) == 0)
        {
            int best = deg, bestDist = 99;
            for (int d = deg - 3; d <= deg + 3; ++d)
                if (const int r = ((d - c) % 7 + 7) % 7; (r == 0 || r == 2 || r == 4) && std::abs(d - deg) < bestDist) best = d, bestDist = std::abs(d - deg);
            deg = best;
        }
        const int p = foldInto(60 + tonicPc_ + degreeSemis(modeAt(bar), deg), kRange[Lead][0], kRange[Lead][1]);
        const int64_t t = t0 + motif_[k].start;
        out.push_back({ t, Event::NoteOn, Lead, uint8_t(p), k == 0 ? .6f : .52f });
        out.push_back({ std::min<int64_t>(t + int64_t(motif_[k].dur * 0.92), t0 + kBarTicks - 20), Event::NoteOff, Lead, uint8_t(p), 0 });
    }
}

void BerlinComposer::emitSub(int bar, std::vector<Event>& out)
{
    // The sub (Kerem, 2026-11-02: "sub bass root notes on the long parts, good fat sub bass"): through the
    // big parts the chord's root, an octave under the bass (E1 to E2), held from one chord change to the
    // next, struck where a part begins and cut with the band at the stop.
    const int64_t t = int64_t(bar) * kBarTicks;
    auto release = [&](int64_t at) { if (subNote_ >= 0) out.push_back({ at, Event::NoteOff, Sub, uint8_t(subNote_), 0 }); subNote_ = -1; };
    // after a fall the quiet part lands on one sub note, its root struck on the downbeat and held the bar
    // (Kerem, 2026-11-07: the start of the part after the echoes was not sharp enough)
    const bool land = turnAt(bar - 1) == Turn::Echo;
    if (!active(Sub, bar) && !land) { release(t); return; }
    const int p = foldInto(24 + tonicPc_ + degreeSemis(modeAt(bar), chordAt(bar)), kRange[Sub][0], kRange[Sub][1]);
    const bool begin = bar == sectionAt(bar).startBar;
    if (p != subNote_ || begin || land)
    {
        release(t);
        out.push_back({ t, Event::NoteOn, Sub, uint8_t(p), begin ? 1.0f : .85f });
        subNote_ = p;
    }
    if (stopBar(bar)) release(t + kHit - 20);
    else if (bar == totalBars() - 1 || !active(Sub, bar)) release(t + kBarTicks);
}

bool BerlinComposer::soloFloor(int bar) const
{
    // Where the lead plays too, they take turns by four-bar phrase: the soloist answers every third
    // phrase, every other one at the summit. Without the lead the floor is the soloist's.
    if (!active(Solo, bar)) return false;
    if (!active(Lead, bar)) return true;
    auto& sec = sectionAt(bar);
    const int phrase = (bar - sec.startBar) / 4;
    return std::strcmp(sec.name, "Summit") == 0 ? phrase % 2 == 1 : phrase % 3 == 2;
}

void BerlinComposer::emitBoom(int bar, std::vector<Event>& out)
{
    // the low hits from the Motion on (the quiet ends of the song are without them), none in a silence
    auto& sec = sectionAt(bar);
    if (sec.energy < .3f || bar < 4) return;
    const int64_t t0 = int64_t(bar) * kBarTicks, quiet = silenceFrom(bar);
    auto& m = modeAt(bar);
    const bool last = bar + 1 == sec.startBar + sec.bars && bar + 1 < totalBars(); // a part's last bar closes the sentence: its pickup leads in
    for (auto& h : booms_.bar(last ? 7 : bar - sec.startBar, energyAt(bar)))
    {
        if (quiet >= 0 && t0 + h.at >= quiet - 20) continue;
        const int p = foldInto(24 + tonicPc_ + degreeSemis(m, chordAt(bar) + (h.fifth ? 4 : 0)), kRange[Boom][0], kRange[Boom][1]);
        out.push_back({ t0 + h.at, Event::NoteOn, Boom, uint8_t(p), h.vel });
        out.push_back({ t0 + h.at + kPPQ / 4, Event::NoteOff, Boom, uint8_t(p), 0 });
    }
}

void BerlinComposer::emitSolo(int bar, std::vector<Event>& out)
{
    // The soloist (Kerem, 2026-11-02: "add a lead soloist"; 2026-11-09: continuous, connecting the parts):
    // the Soloist's line through every part from the fourth bar, in long notes under the lead's motif
    // phrases and on its own where the motif rests (the Shift, the Dissolve, the quiet parts).
    if (bar < 4) return;
    auto& sec = sectionAt(bar);
    auto& m = modeAt(bar);
    auto tones = [&](int b) {
        const int c = chordAt(b);
        std::vector<int> ct;
        for (int d : { 0, 2, 4 }) ct.push_back((tonicPc_ + degreeSemis(modeAt(b), c + d)) % 12);
        return ct;
    };
    const int64_t t0 = int64_t(bar) * kBarTicks;
    solo_.play({ t0, energyAt(bar), sectionOfBar_[size_t(bar)], bar - sec.startBar, sec.bars, std::strcmp(sec.name, "Summit") == 0, false,
                 active(Lead, bar) && !soloFloor(bar), bar + 1 == totalBars(), tones(bar), tones(std::min(bar + 1, totalBars() - 1)),
                 [&](int p) { return inMode(m, tonicPc_, p); }, turnAt(bar) == Turn::Pickup ? -1 : silenceFrom(bar) },
               leadRng_, out);
}

std::string BerlinComposer::describe() const
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    double secs = totalBars() * 4 * 60.0 / bpm_;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s %s (shift: %s) · %.0f bpm · %d bars · %d:%02d", names[tonicPc_],
                  homeMode_->name, shiftMode_->name, bpm_, totalBars(), int(secs) / 60, int(secs) % 60);
    return buf;
}

Sentence::Sentence(uint64_t seed)
{
    Rng rng(seed * 0xD1B54A32D192ED03ull + 11);
    const int64_t e8 = kPPQ / 2;                                    // an eighth
    static const int offs[] = { 3, 5, 6, 10, 11, 12, 14 };          // where the motif answers its downbeat, in eighths over two bars
    std::vector<int> motif = { 0 };
    for (int n = rng.range(1, 2); int(motif.size()) <= n;)
        if (const int o = offs[rng.range(0, 6)]; std::find(motif.begin(), motif.end(), o) == motif.end()) motif.push_back(o);
    std::sort(motif.begin(), motif.end());
    auto put = [&](int pair, int at, float vel, bool fifth) { bars_[size_t(pair * 2 + at / 8)].push_back({ (at % 8) * e8, vel, fifth }); };
    for (int pair = 0; pair < 3; ++pair)
    {
        auto m = motif;
        if (pair == 1) m.back() = m.back() + (m.back() < 15 && rng.chance(.5) ? 1 : -1);                  // the motif, a note moved
        if (pair == 2) for (int o = rng.range(4, 7); ; o = (o + 3) % 16) if (std::find(m.begin(), m.end(), o) == m.end()) { m.push_back(o); break; } // grown
        for (size_t k = 0; k < m.size(); ++k) put(pair, m[k], k == 0 ? (pair == 0 ? 1.0f : .85f) : .62f, pair == 2 && k + 1 == m.size());
    }
    put(3, 0, .9f, false);                                           // the close: one hit, then the pickup into the next
    put(3, 12, .6f, true);
    put(3, 14, .75f, false);
    for (auto& b : bars_)
    {   // sorted, and one hit to a place (the moved note may land on another)
        std::sort(b.begin(), b.end(), [](auto& x, auto& y) { return x.at < y.at || (x.at == y.at && x.vel > y.vel); });
        b.erase(std::unique(b.begin(), b.end(), [](auto& x, auto& y) { return x.at == y.at; }), b.end());
    }
}

std::vector<Sentence::Hit> Sentence::bar(int in, float energy) const
{
    const int k = in % 8;
    std::vector<Hit> hits;
    for (auto& h : bars_[size_t(k)])
    {
        const bool head = h.at == 0 && k % 2 == 0, pickup = k == 7 && h.at > 0;
        if (energy < .35f && !head && !pickup) continue;              // the calm: the pairs' downbeats and the pickup
        if (energy < .6f && k >= 4 && k < 6 && !head) continue;       // the grown pair only where the music moves
        hits.push_back({ h.at, h.vel * (.55f + .45f * energy), h.fifth });
    }
    return hits;
}

void Soloist::play(const Bar& b, Rng& rng, std::vector<Event>& out)
{
    const int64_t q = kPPQ;
    const float e = b.energy;
    const bool lyric = sing_ || b.theme;
    if (b.part != part_)
    {   // a new part: a rhythm and a register of its own
        part_ = b.part;
        centre_ = lo_ + int(float(hi_ - lo_) * (b.summit ? .75f : b.away ? .25f : .3f + .4f * e));
        cell_.clear();
        if (lyric)
        {
            static const int64_t kLong[][3] = { { 0, -1, -1 }, { 0, 2 * kPPQ, -1 }, { 0, 3 * kPPQ, -1 }, { 0, 2 * kPPQ, 3 * kPPQ }, { 0, 3 * kPPQ / 2, 3 * kPPQ } };
            for (int64_t x : kLong[e < .35f ? rng.range(0, 2) : e < .7f ? rng.range(1, 3) : rng.range(2, 4)]) if (x >= 0) cell_.push_back(x);
        }
        else
        {   // a quarter, eighth or sixteenth grid, some of its steps, always the downbeat
            const int grid = e < .35f ? 4 : e < .7f ? 8 : 16, n = e < .35f ? rng.range(2, 3) : e < .7f ? rng.range(4, 6) : rng.range(8, 11);
            std::vector<int> steps;
            for (int i = 1; i < grid; ++i) steps.push_back(i);
            for (int i = int(steps.size()) - 1; i > 0; --i) std::swap(steps[size_t(i)], steps[size_t(rng.range(0, i))]);
            cell_.push_back(0);
            for (int i = 0; i < n - 1; ++i) cell_.push_back(steps[size_t(i)] * (kBarTicks / grid));
            std::sort(cell_.begin(), cell_.end());
        }
    }

    if (b.stopAt >= 0 && b.stopAt - b.t0 < kPPQ) { left_ = 0; return; } // the whole bar is a silence
    // this bar's rhythm: the part's, now and then a note left out
    std::vector<int64_t> on = cell_;
    if (on.size() > 2 && rng.chance(.25)) on.erase(on.begin() + rng.range(1, int(on.size()) - 1));
    const bool walk = b.in + 1 == b.bars && !b.final && b.stopAt < 0;
    const bool start = left_ == 0;
    if (start) left_ = rng.range(2, lyric ? 3 : 4);
    const bool breath = --left_ == 0 && !walk && !b.final;
    int64_t end = kBarTicks;
    if (b.stopAt >= 0)
    {   // up to the silence as the part plays it, the last note held into it
        end = b.stopAt - b.t0;
        std::erase_if(on, [&](int64_t t) { return t > 0 && t > end - kPPQ / 2; });
        left_ = 0;
    }
    else if (b.final) on = { 0 };
    else if (breath) { std::erase_if(on, [&](int64_t t) { return t > 0 && t >= 3 * q; }); end = 3 * q; }
    else if (walk)
    {   // through the change: the first half as the part plays it, then a run into the next part's chord
        std::erase_if(on, [&](int64_t t) { return t >= 3 * q; });
        const int64_t d = lyric ? q : e < .7f ? q / 2 : q / 4;
        for (int64_t t = 3 * q; t < kBarTicks; t += d) on.push_back(t);
    }

    auto has = [](const std::vector<int>& ct, int p) { return std::find(ct.begin(), ct.end(), (p % 12 + 12) % 12) != ct.end(); };
    auto toCT = [&](const std::vector<int>& ct, int p, int d) {
        for (int k = 0; k < 7; ++k) { if (has(ct, p + d * k)) return p + d * k; if (has(ct, p - d * k)) return p - d * k; }
        return p;
    };
    auto step = [&](int p, int d) { int x = p + d; for (int k = 0; k < 12 && !b.inMode(x); ++k) x += d; return x; };
    auto fold = [&](int p) { while (p > hi_) p -= 12; while (p < lo_) p += 12; return p; };

    int p = pitch_ < 0 ? toCT(b.chord, centre_, 1) : pitch_;
    // the run's goal: a chord tone of what comes next, on the side of the line's centre; the run steps up (or down) to it
    const int goal = walk ? fold(toCT(b.next, p + (p < centre_ ? 2 : -2), p < centre_ ? 1 : -1)) : -1;
    const int runs = walk ? int(std::count_if(on.begin(), on.end(), [&](int64_t t) { return t >= 3 * q; })) : 0;
    int run = 0;
    for (size_t i = 0; i < on.size(); ++i)
    {
        const int64_t t = on[i];
        const bool beat = t % (lyric ? 2 * q : q) == 0;
        if (walk && t >= 3 * q)
        {
            const int s = goal >= p ? 1 : -1;
            p = goal;
            for (int k = runs - run++; k > 0; --k) p = step(p, -s);
        }
        else if (i == 0 && (start || b.in == 0)) p = toCT(b.chord, p, dir_);       // a phrase, and a part, begin on a chord tone
        else
        {
            if (p > centre_ + 6 || p > hi_ - 3) dir_ = -1;
            else if (p < centre_ - 6 || p < lo_ + 3) dir_ = 1;
            else if (rng.chance(.15)) dir_ = -dir_;
            if (rng.chance(lyric ? .15 : .1)) { p = toCT(b.chord, p + dir_ * rng.range(4, 7), dir_); dir_ = -dir_; } // a leap, then back
            else { const int n = step(p, dir_); p = beat ? toCT(b.chord, n, dir_) : n; }
        }
        p = fold(p);
        const int64_t next = i + 1 < on.size() ? on[i + 1] : end;
        int64_t len = next - t - 20;
        if (!lyric && e > .5f && !beat && i + 1 < on.size() && rng.chance(.2)) len = std::min<int64_t>(len, q / 3);
        const float vel = std::clamp(.5f + .35f * e + (beat ? .06f : 0.0f) + (b.summit ? .05f : 0.0f) + float(rng.uniform() * .06 - .03), .2f, 1.0f);
        out.push_back({ b.t0 + t, Event::NoteOn, uint8_t(track_), uint8_t(p), vel });
        out.push_back({ b.t0 + t + std::max<int64_t>(30, len), Event::NoteOff, uint8_t(track_), uint8_t(p), 0 });
    }
    pitch_ = p;
}

const SceneInfo& sceneInfo(const std::string& scene)
{
    static const SceneInfo berlin {
        "berlin", 8, { "pad", "bass", "seq", "lead", "guitar", "solo", "sub", "boom" }, { 0, 1, 2, 3, 1, 3, 1, 1 },
        { { .15f, .35f }, { .05f, .15f }, { .35f, .25f }, { .30f, .35f }, { .05f, .15f }, { .30f, .40f }, { 0, .03f }, { 0, .12f } }, { -1, -1, -1, -1, -1, -1, -1, -1 }, {}, false,
    };
    if (int k; improvScene(scene, k)) return *genreSceneInfo(GenreComposer::all()[0]->scene); // the genre tracks
    if (auto* g = genreSceneInfo(scene)) return *g;
    return berlin;
}

namespace {
// A song after bars of silence at its own tempo, a part called Solo (the concert's opening: the drums alone, the
// visuals already the song's). The song itself is composed exactly as without it.
class AfterSilence : public Composer
{
public:
    AfterSilence(std::unique_ptr<Composer> song, double seconds) : Composer(0), song_(std::move(song))
    {
        bpm_ = song_->bpm();
        n_ = std::max(1, int(std::lround(seconds * bpm_ / 240.0)));
        sections_.push_back({ "Solo", 0, n_, 0.0f, 0u, false });
        for (auto sec : song_->sections()) { sec.startBar += n_; sections_.push_back(sec); }
        sectionOfBar_.assign(size_t(n_), 0);
        energyOfBar_.assign(size_t(n_), 0.0f);
        for (int b = 0; b < song_->totalBars(); ++b)
        {
            sectionOfBar_.push_back(1 + int(&song_->sectionAt(b) - song_->sections().data()));
            energyOfBar_.push_back(song_->energyAt(b));
        }
    }
    void generateBar(int bar, std::vector<Event>& out) override
    {
        if (bar < n_) return;
        const size_t from = out.size();
        song_->generateBar(bar - n_, out);
        for (size_t k = from; k < out.size(); ++k) out[k].tick += int64_t(n_) * kBarTicks;
    }
    std::string describe() const override { return song_->describe() + " · after " + std::to_string(n_) + " bars of solo"; }

private:
    std::unique_ptr<Composer> song_;
    int n_ = 1;
};
// The concert's way from one song into the next (Kerem, 2026-11-25: "the metronome changes while the other song
// continues ... when the metronome becomes the new one then the next song starts, but some of the elements may enter
// before the song starts"; and Berlin into Working Class Curse: "a slight silence and then starts with the click"):
// bars with no beat before the song, at its tempo. The song before has played to its end and rings out here while the
// click glides from its tempo into this one (the old engine's glideClick), landing a bar before the song: one bar of
// the new click alone, steady, then the song. What enters early is held, nothing on a beat: the pad on the song's first
// chord from the first bar, at once (no dip after the old song's end), growing to halfway; the drone, root and fifth,
// from halfway. At the song's first bar each hands over: to the song's own part if the song plays that track there,
// else it fades out over that bar. The song itself is composed exactly as without the bridge.
class Bridge : public Composer
{
public:
    Bridge(std::unique_ptr<GenreComposer> song, double seconds) : Composer(0), song_(std::move(song))
    {
        bpm_ = song_->bpm();
        n_ = std::max(2, 2 * int(std::ceil(seconds * bpm_ / 480.0)));
        sections_.push_back({ "Bridge", 0, n_, 0.0f, 0u, false, true });
        for (auto sec : song_->sections()) { sec.startBar += n_; sections_.push_back(sec); }
        sectionOfBar_.assign(size_t(n_), 0);
        energyOfBar_.assign(size_t(n_), 0.0f);
        for (int b = 0; b < song_->totalBars(); ++b)
        {
            sectionOfBar_.push_back(1 + int(&song_->sectionAt(b) - song_->sections().data()));
            energyOfBar_.push_back(song_->energyAt(b));
        }
        const auto& m = song_->modeAt(0);
        const int root = song_->rootAt(0), r = foldInto(song_->tonicAt(0) + degreeSemis(m, root), 48, 59);
        voices_[0] = { GenreComposer::Pad, 0, { r, r + degreeSemis(m, root + 2) - degreeSemis(m, root), r + degreeSemis(m, root + 4) - degreeSemis(m, root) } };
        const int d = foldInto(r, 40, 51);
        voices_[1] = { GenreComposer::Drone, n_ / 2, { d, d + 7, -1 } };
    }
    void generateBar(int bar, std::vector<Event>& out) override
    {
        const int64_t t0 = int64_t(bar) * kBarTicks;
        if (bar < n_)
        {
            for (auto& v : voices_)
            {
                if (bar < v.from) continue;
                if (bar == v.from)
                    for (int p : v.pitches) if (p >= 0) out.push_back({ t0, Event::NoteOn, uint8_t(v.track), uint8_t(p), .6f });
                for (int beat = 0; beat < 4; ++beat) // up from .7 to 1 over the first half of its time here (at .45-.8 the
                {                                    // bridge into Ecstatic fell to -37 dB)
                    const double u = ((bar - v.from) * 4 + beat) / std::max(1.0, (n_ - v.from) * 2.0);
                    out.push_back({ t0 + beat * kPPQ, Event::Level, uint8_t(v.track), 0, float(.7 + .3 * std::min(1.0, u)) });
                }
            }
            return;
        }
        const size_t from = out.size();
        song_->generateBar(bar - n_, out);
        for (size_t k = from; k < out.size(); ++k) out[k].tick += int64_t(n_) * kBarTicks;
        if (bar != n_) return;
        for (auto& v : voices_) // the hand-over, over the song's first bar
        {
            float to = 0;
            for (size_t k = from; k < out.size(); ++k)
                if (out[k].type == Event::Level && out[k].track == v.track && out[k].tick == t0) { to = out[k].value; out[k].value = 1.0f; }
            const bool plays = song_->active(v.track, 0);
            for (int p : v.pitches) if (p >= 0) out.push_back({ plays ? t0 : t0 + kBarTicks, Event::NoteOff, uint8_t(v.track), uint8_t(p), 0 });
            for (int beat = 1; beat < 4; ++beat)
                out.push_back({ t0 + beat * kPPQ, Event::Level, uint8_t(v.track), 0, float(1.0 + (to - 1.0) * beat / 3.0) });
        }
    }
    std::string describe() const override { return song_->describe() + " · after a bridge of " + std::to_string(n_) + " bars"; }

private:
    struct Voice { int track, from; std::array<int, 3> pitches; };
    std::unique_ptr<GenreComposer> song_;
    int n_ = 2;
    std::array<Voice, 2> voices_ {};
};
// Low Hertz (Kerem, 2026-11-25: "instead of Halftime a drone, but establishing low hertz pulses, in harmony, 3-4 voices
// all playing low notes, like ti ti ti ti but in different speeds, like 20 hertz, 10 hertz, and pad drones on top of it,
// an ambient sound-artish song"). Exactly 60 bpm, so a voice's pulses a beat are its rate in hertz and every rate meets
// the others again on each beat. Four low voices of one chord, root, fifth, third and ninth, each struck again and
// again at its own rate (4-20 Hz), each swelling and fading on its own slow period so the four drift in and out of
// phase; the rates rise through the piece and fall back. The chord moves once every four bars (16 s): i, VI, iv, VII.
// Over them the pad holds the chord, the choir an open fifth high up, and the drone the tonic underneath. No stops, no
// fills, no soloist: the parts flow into each other, each voice fading in over two bars and out over one.
class PulseComposer : public Composer
{
public:
    PulseComposer(uint64_t seed, double bridgeSeconds) : Composer(seed)
    {
        bpm_ = 60;
        tonic_ = std::array<int, 4> { 2, 1, 4, 11 }[size_t(rng_.range(0, 3))];
        if (bridgeSeconds > 0) // the concert's bridge (Bridge): the pad alone, the drone from halfway, no pulse
            add({ "Bridge", std::max(2, 2 * int(std::ceil(bridgeSeconds * bpm_ / 480.0))), 0.0f, { 0, 0, 0, 0, 0 }, false, true }, true);
        add({ "Hum", 8, .15f, { 4, 0, 0, 0, 2 }, false, false });
        add({ "Lattice", 8, .35f, { 6, 8, 0, 0, 3 }, true, false });   // 2026-11-26 (Kerem): a minute shorter
        add({ "Interference", 12, .60f, { 10, 12, 15, 0, 4 }, true, true });
        add({ "Standing Wave", 12, .90f, { 10, 12, 15, 20, 5 }, true, true });
        add({ "Afterimage", 8, .30f, { 0, 0, 5, 6, 2 }, true, true });
        add({ "Residue", 6, .10f, { 0, 0, 0, 0, 1 }, false, true });
    }
    void generateBar(int bar, std::vector<Event>& out) override
    {
        const int si = sectionOfBar_[size_t(bar)];
        const auto& p = parts_[size_t(si)];
        const auto& sec = sections_[size_t(si)];
        const int64_t t0 = int64_t(bar) * kBarTicks;
        const int in = bar - sec.startBar, left = sec.startBar + sec.bars - bar;
        const bool first = bar == 0, last = bar + 1 == totalBars();
        const int chordIdx = bar / 4, rootDeg = std::array<int, 4> { 0, 5, 3, 6 }[size_t(chordIdx % 4)];
        const bool newChord = bar % 4 == 0 || first;
        auto pc = [&](int deg) { return tonic_ + degreeSemis(kAeolian, rootDeg + deg); };
        auto note = [&](int track, int pitch, int64_t at, int64_t len, float vel) {
            out.push_back({ at, Event::NoteOn, uint8_t(track), uint8_t(pitch), std::clamp(vel, .05f, 1.0f) });
            out.push_back({ at + len, Event::NoteOff, uint8_t(track), uint8_t(pitch), 0 });
        };
        // the four pulsing voices: track, register, chord degree, its swell's period in seconds
        // and a fifth (Kerem, 2026-11-25: "lower sub hertz ti ti ti as well"): the root in the sub, 33-62 Hz, slower (1-5 a
        // second) so each low hit stays apart
        static constexpr int kTrack[5] = { GenreComposer::Ground, GenreComposer::Perc, GenreComposer::Body, GenreComposer::Metal, GenreComposer::Sub };
        static constexpr int kLo[5] = { 28, 35, 40, 48, 24 }, kHi[5] = { 40, 47, 55, 62, 35 }, kDeg[5] = { 0, 4, 2, 8, 0 };
        static constexpr double kSwell[5] = { 13, 17, 23, 29, 11 };
        for (int v = 0; v < 5; ++v)
        {
            out.push_back({ t0, Event::Level, uint8_t(kTrack[v]), 0, 1.0f });
            const int rate = p.rate[size_t(v)];
            if (rate <= 0) continue;
            // in over two bars where the part before did not have it, out over the last bar where the next will not
            const bool before = si > 0 && parts_[size_t(si - 1)].rate[size_t(v)] > 0;
            const bool after = si + 1 < int(parts_.size()) && parts_[size_t(si + 1)].rate[size_t(v)] > 0;
            const int pitch = foldInto(kLo[v] + ((pc(kDeg[v]) - kLo[v]) % 12 + 12) % 12, kLo[v], kHi[v]);
            const int64_t step = kPPQ / rate;
            const int n = 4 * rate;
            for (int k = 0; k < n; ++k)
            {
                const int64_t at = t0 + k * step;
                const double u = double(k) / n, sec = (bar + u) * 4.0;
                float g = 1;
                if (!before && in < 2) g *= float((in + u) / 2.0);
                if (!after && left == 1) g *= float(1.0 - u);
                const double swell = 0.55 + 0.45 * std::pow(std::sin(6.283185307179586 * sec / kSwell[v] + v * 1.3), 2.0);
                const float accent = k % rate == 0 ? 1.15f : 1.0f; // the beat, where every rate meets
                note(kTrack[v], pitch, at, step / 2, float((0.35 + 0.45 * p.energy) * swell) * accent * g);
            }
        }
        // the pad: the chord held, re-voiced at each change (root, fifth, ninth, the third above)
        const float fadeIn = std::min(1.0f, float(bar + 1) / 3.0f), fadeOut = std::min(1.0f, float(totalBars() - bar) / 3.0f);
        out.push_back({ t0, Event::Level, uint8_t(GenreComposer::Pad), 0, std::min(fadeIn, fadeOut) * (.75f + .25f * p.energy) });
        if (newChord)
        {
            const int r = foldInto(48 + ((pc(0) - 48) % 12 + 12) % 12, 48, 59);
            for (int d : { 0, 4, 8, 9 }) note(GenreComposer::Pad, r + degreeSemis(kAeolian, rootDeg + d) - degreeSemis(kAeolian, rootDeg),
                                            t0, std::min<int64_t>(4, totalBars() - bar) * kBarTicks - 20, .6f);
        }
        // the choir: an open fifth high up, from the parts that have it, swelling in
        out.push_back({ t0, Event::Level, uint8_t(GenreComposer::Harm), 0, p.harm ? std::min(1.0f, float(in + 1) / 4.0f) * fadeOut : 0.0f });
        if (p.harm && (newChord || in == 0))
        {
            const int len = std::min({ 4 - bar % 4, sec.startBar + sec.bars - bar });
            const int h = foldInto(67 + ((pc(0) - 67) % 12 + 12) % 12, 67, 78);
            note(GenreComposer::Harm, h, t0, len * kBarTicks - 20, .5f);
            note(GenreComposer::Harm, h + 7, t0, len * kBarTicks - 20, .4f);
        }
        // the drone: the tonic underneath, held through each part that has it
        out.push_back({ t0, Event::Level, uint8_t(GenreComposer::Drone), 0, p.drone ? std::min(1.0f, float(in + 1) / 2.0f) * fadeOut : 0.0f });
        if (p.drone && in == 0)
            note(GenreComposer::Drone, foldInto(36 + ((tonic_ - 36) % 12 + 12) % 12, 36, 47), t0, sec.bars * kBarTicks - 20, .6f);
        if (last) for (int t = 0; t < kGenreTracksN; ++t) out.push_back({ t0 + kBarTicks, Event::Level, uint8_t(t), 0, 0.0f });
    }
    std::string describe() const override
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int s = int(std::lround(totalBars() * 240.0 / bpm_));
        char b[96];
        std::snprintf(b, sizeof b, "Low Hertz · %s aeolian · 60 bpm · %d bars · %d:%02d", names[tonic_], totalBars(), s / 60, s % 60);
        return b;
    }

private:
    static constexpr int kGenreTracksN = GenreComposer::kGenreTracks;
    struct Part { const char* name; int bars; float energy; std::array<int, 5> rate; bool harm, drone; };
    void add(const Part& p, bool aside = false)
    {
        const int start = totalBars();
        sections_.push_back({ p.name, start, p.bars, p.energy, 0u, false, aside });
        parts_.push_back(p);
        for (int b = 0; b < p.bars; ++b) { sectionOfBar_.push_back(int(sections_.size()) - 1); energyOfBar_.push_back(p.energy); }
    }
    int tonic_ = 2;
    std::vector<Part> parts_;
};
// A genre song with its own copy of the table (the composer keeps a reference to it): the concert's stretched Seed.
struct SpecHolder { GenreComposer::Spec spec; };
class StretchedSeed : private SpecHolder, public GenreComposer
{
public:
    StretchedSeed(const GenreComposer::Spec& s, uint64_t seed) : SpecHolder { s }, GenreComposer(SpecHolder::spec, seed) {}
};
} // namespace

std::unique_ptr<Composer> makeComposer(const std::string& scene, uint64_t seed, double silence, double seedSeconds, double bridge)
{
    std::unique_ptr<Composer> c;
    if (int k; improvScene(scene, k)) c = std::make_unique<GenreComposer>(improvSong(seed, k).spec, seed * 7919 + uint64_t(k) + 1);
    else if (scene == "hertz") c = std::make_unique<PulseComposer>(seed, bridge); // composes its own bridge
    else if (auto* g = GenreComposer::find(scene))
    {
        std::unique_ptr<GenreComposer> song;
        if (seedSeconds > 0) // the first part about that long, at the song's middle tempo (its own is drawn within a few %)
        {
            auto spec = *g;
            spec.form[0].bars = std::max(spec.form[0].bars, int(std::lround(seedSeconds * 0.5 * (spec.bpmLo + spec.bpmHi) / 240.0)));
            song = std::make_unique<StretchedSeed>(spec, seed);
        }
        else song = std::make_unique<GenreComposer>(*g, seed);
        if (bridge > 0) c = std::make_unique<Bridge>(std::move(song), bridge);
        else c = std::move(song);
    }
    else c = std::make_unique<BerlinComposer>(seed);
    if (silence > 0) c = std::make_unique<AfterSilence>(std::move(c), silence);
    return c;
}

} // namespace liszt
