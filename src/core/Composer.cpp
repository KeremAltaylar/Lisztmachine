#include "Composer.h"
#include "Chorale.h"
#include "Yours.h"
#include "Genres.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace liszt {

namespace {

constexpr uint8_t bit(int t) { return uint8_t(1u << t); }
constexpr uint8_t kAll = 0x1F;

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
    tonicPc_ = tonics[rng_.range(0, 5)];
    // a minor home, melancholic; the shift is the one lighter moment (aeolian to dorian, phrygian to aeolian)
    homeMode_ = rng_.chance(.6) ? &kAeolian : &kPhrygian;
    shiftMode_ = homeMode_ == &kAeolian ? &kDorian : &kAeolian;
    bpm_ = 2.0 * rng_.range(51, 56); // 102..112: slower, so the sequences breathe

    planForm();
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
        { "Drift",     16, .15f, bit(Pad), false },
        { "Pulse",     16, .30f, uint8_t(bit(Pad) | bit(Bass)), false },
        { "Motion",    32, .50f, uint8_t(bit(Pad) | bit(Bass) | bit(Seq)), false },
        { "Song",      32, .62f, kAll, false },
        { "Haze",      16, .35f, uint8_t(bit(Pad) | bit(Seq)), false },
        { "Return",    32, .72f, uint8_t(bit(Pad) | bit(Bass) | bit(Seq)), true },
        { "Theme",     32, .82f, kAll, true },
        { "Shift",     32, .70f, kAll, false },
        { "Summit",    32, 1.0f, kAll, true },
        { "Dissolve",  32, .30f, uint8_t(bit(Pad) | bit(Seq)), false },
        { "Afterglow", 16, .05f, bit(Pad), false },
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

bool BerlinComposer::active(int track, int bar) const
{
    return bar >= 0 && bar < totalBars() && (sectionAt(bar).layers & bit(track));
}

float BerlinComposer::levelAt(int track, int bar) const
{
    if (!active(track, bar)) return 0;
    auto& sec = sectionAt(bar);
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
        out.push_back({ t0, Event::Level, uint8_t(tr), 0, levelAt(tr, bar) });

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
        for (int v : padVoices_) out.push_back({ at, Event::NoteOff, Pad, uint8_t(v), 0 });
        padChord_ = -1;
    };

    if (!active(Pad, bar)) { if (padChord_ >= 0) release(t); return; }

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

        const float vel = .45f + .2f * energyAt(bar);
        for (int p : padVoices_)
            if (wasSounding && std::find(next.begin(), next.end(), p) == next.end())
                out.push_back({ t, Event::NoteOff, Pad, uint8_t(p), 0 });
        for (int p : next)
            if (!wasSounding || std::find(padVoices_.begin(), padVoices_.end(), p) == padVoices_.end())
                out.push_back({ t, Event::NoteOn, Pad, uint8_t(p), vel });
        padVoices_ = next;
        padChord_ = c;
    }

    if (bar == totalBars() - 1) release(t + kBarTicks);
}

void BerlinComposer::emitSequence(int track, int bar, std::vector<Event>& out)
{
    const auto& pat = track == Bass ? bass_ : seq_;
    const float e = energyAt(bar);
    const int len = int(pat.size());
    const int limit = std::max(1, int(std::lround((track == Bass ? .5 + .5 * e : .45 + .55 * e) * len)));
    const bool summit = std::strcmp(sectionAt(bar).name, "Summit") == 0;
    const char* fig = endFigure(bar);
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
    const int stop = fig ? 4 : 16; // the upper sequence stops after the figure's first beat

    for (int i = 0; i < stop; ++i)
    {
        // The upper sequence runs in its own length (10, 12, 14) against the 16-step bar.
        const auto& s = pat[size_t(track == Bass ? i : (bar * 16 + i) % len)];
        if (!s.on || s.rank >= limit) continue;
        const int64_t t = int64_t(bar) * kBarTicks + i * kSixteenth;
        const int64_t off = t + int64_t(s.gate * kSixteenth);
        float vel = std::clamp(float(.5 + .35 * e + (s.accent ? .15 : 0) + .06 * (rng_.uniform() - .5)), .05f, 1.0f);
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
    const int phrase = (bar - sec.startBar) / 4;
    if (phrase % 3 == 2 && energyAt(bar) < .8f) return; // breathe: every third phrase rests

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
            out.push_back({ t, Event::NoteOn, Lead, uint8_t(p), vel });
            out.push_back({ t + int64_t(n.dur * 0.92), Event::NoteOff, Lead, uint8_t(p), 0 });
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
    emitGuitar(bar, out);
}

const char* BerlinComposer::endFigure(int bar) const
{
    // The last bar of every part but the final one; into a rise a figure that pushes, into a fall one
    // that stops. All start on the downbeat (the bass keeps its spine), all on the chord they are in.
    if (bar + 1 >= totalBars() || sectionOfBar_[size_t(bar + 1)] == sectionOfBar_[size_t(bar)]) return nullptr;
    static const char* up[] = { "X.......x.x.xXXX", "X..x..x..x..x..x", "X.......x...X..." };
    static const char* down[] = { "X..x..x.X.......", "X.x...x.x...X.X." };
    const int si = sectionOfBar_[size_t(bar)];
    return sectionAt(bar + 1).energy > sectionAt(bar).energy ? up[si % 3] : down[si % 2];
}

void BerlinComposer::emitGuitar(int bar, std::vector<Event>& out)
{
    // Power chords on the chord's root (the mode's own fifth over it), let ring: one every other bar
    // while the music is calm, on one and three as it drives, and at the summit a muted pair leading
    // into each bar. A part's last bar plays its figure. Under .5 the engine's guitar mutes the string.
    if (!active(Guitar, bar)) return;
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

std::string BerlinComposer::describe() const
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    double secs = totalBars() * 4 * 60.0 / bpm_;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s %s (shift: %s) · %.0f bpm · %d bars · %d:%02d", names[tonicPc_],
                  homeMode_->name, shiftMode_->name, bpm_, totalBars(), int(secs) / 60, int(secs) % 60);
    return buf;
}

const SceneInfo& sceneInfo(const std::string& scene)
{
    static const SceneInfo berlin {
        "berlin", 5, { "pad", "bass", "seq", "lead", "guitar" }, { 0, 1, 2, 3, 1 },
        { { .15f, .35f }, { .05f, .15f }, { .35f, .25f }, { .30f, .35f }, { .05f, .15f } }, { -1, -1, -1, -1, -1 }, {}, false,
    };
    // The chorale: each voice its own track (and its own delay), the voices change sound as one.
    static const SceneInfo chorale {
        "chorale", 10, { "soprano", "alto", "tenor", "bass", "sub", "drums", "micro", "blips", "piano", "guitar" },
        { 0, 0, 0, 0, 1, 1, 2, 2, 3, 1 },
        { { 0, .30f }, { 0, .30f }, { 0, .30f }, { 0, .26f }, { 0, .04f }, { .04f, .12f }, { .22f, .12f }, { .28f, .22f }, { .15f, .28f }, { .12f, .20f } },
        { -1, 0, 0, 0, -1, -1, -1, -1, -1, -1 },
        { 1, 1, 1, 1, 0, 0, 0, 0, 2, 1 }, // the piano keeps its oscillator tuning: rounding it put its patches an octave off
        true,
    };
    // Let All Be Yours: the drone, the drums and the bass dry; the acid and the stabs into the echoes.
    static const SceneInfo yours {
        "letallbeyours", 9, { "drone", "kick", "bass", "acid", "stab", "hats", "clap", "sub", "fx" },
        { 0, 1, 1, 3, 2, 2, 2, 1, 0 },
        { { 0, .30f }, { 0, .03f }, { 0, .05f }, { .20f, .15f }, { .15f, .25f }, { .05f, .10f }, { .10f, .25f }, { 0, 0 }, { .10f, .35f } },
        { -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        { 1, 0, 1, 1, 1, 0, 0, 0, 0 },
        true,
    };
    if (auto* g = genreSceneInfo(scene)) return *g;
    return scene == "chorale" ? chorale : scene == "letallbeyours" ? yours : berlin;
}

std::unique_ptr<Composer> makeComposer(const std::string& scene, uint64_t seed)
{
    if (scene == "chorale") return std::make_unique<ChoraleComposer>(seed);
    if (scene == "letallbeyours") return std::make_unique<YoursComposer>(seed);
    if (auto* g = GenreComposer::find(scene)) return std::make_unique<GenreComposer>(*g, seed);
    return std::make_unique<BerlinComposer>(seed);
}

} // namespace liszt
