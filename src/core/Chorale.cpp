#include "Chorale.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace liszt {

namespace {

constexpr uint8_t bit(int t) { return uint8_t(1u << t); }
constexpr int k16 = kSixteenth; // ticks
constexpr int kLeadingTone = 11, kDominant = 7;

// ---------------------------------------------------------------- the harmonic vocabulary
// A phrase is written as Roman numerals in minor: NAME[7|M7|add9|h7|o7][/inversion]:beats[!cadence]
// e.g. "iih7/1:2" is a half-diminished ii in first inversion for two beats; "!a" marks the
// authentic cadence the phrase ends on (h half, a authentic, d deceptive, p plagal).
using Phrase = const char*;
enum Kind { Ant, Cons, ConsDec, ConsPlag, Euph, SeqA, SeqB, Pedal, AntSlow, ConsSlow, EuphSlow, Final };

const std::vector<Phrase>& templates(Kind k)
{
    static const std::vector<Phrase> ant = {
        "i:4 VI:4 iv:4 V:4!h", "i:4 VII:4 VI:4 V:4!h", "i:4 iv/1:4 iih7/1:4 V:4!h", "i:4 III:4 iv:4 V:4!h",
        "i:2 i/1:2 iv:4 VI:4 V:4!h", "i:4 VI:4 bII/1:4 V:4!h", "i:4 iv:2 VI:2 iih7/1:4 V:4!h",
    };
    static const std::vector<Phrase> cons = {
        "i:4 VI:4 iih7/1:2 V7:2 i:4!a", "i:4 iv:4 V7:4 i:4!a", "VI:4 iv:4 V7:4 i:4!a", "i/1:4 iv:4 i/2:2 V7:2 i:4!a",
        "III:4 VI:4 iv:2 V7:2 i:4!a", "i:4 VII:2 III:2 iv:2 V7:2 i:4!a", "VI:4 bII/1:4 V7:4 i:4!a",
    };
    static const std::vector<Phrase> dec = {
        "i:4 iv:4 V7:4 VI:4!d", "i:4 VI:4 iih7/1:2 V7:2 VI:4!d", "i/1:4 iv:4 V7:4 VIM7:4!d", "III:4 iv:4 V7:4 VI:4!d",
    };
    static const std::vector<Phrase> plag = { "i:4 VI:4 iv:4 i:4!p", "i:4 iv7:4 iv:4 i:4!p", "VI:4 iv/1:4 iv:4 i:4!p" };
    static const std::vector<Phrase> euph = {
        "i:4 VI:4 III:4 VII:4", "iadd9:4 VIM7:4 IIIadd9:4 VII:4", "VI:4 VII:4 i:4 i/1:4", "i:4 VI:4 III:4 VII/1:4",
        "VIM7:4 VII:4 iadd9:4 III:4",
    };
    static const std::vector<Phrase> seqA = { "i:4 iv:4 VII:4 III:4", "i7:4 iv7:4 VII7:4 IIIM7:4" };
    static const std::vector<Phrase> seqB = { "VI:4 iih7:4 V7:4 i:4!a", "VIM7:4 iih7:4 V7:4 i:4!a" };
    static const std::vector<Phrase> pedal = { "V:4 i/2:4 iv/2:4 V7:4!h", "V:4 viio7:4 i/2:4 V7:4!h", "V:4 iv/2:4 viio7:4 V7:4!h" };
    static const std::vector<Phrase> fin = { "VI:8 iv:8 V7:8 i:8!a", "iv:8 VI:8 V7:8 i:8!a" };
    switch (k)
    {
        case Ant: case AntSlow: return ant;
        case Cons: case ConsSlow: return cons;
        case ConsDec: return dec;
        case ConsPlag: return plag;
        case Euph: case EuphSlow: return euph;
        case SeqA: return seqA;
        case SeqB: return seqB;
        case Pedal: return pedal;
        case Final: return fin;
    }
    return ant;
}

struct Parsed { int root; ChoraleComposer::Quality q; int inversion; int beats; ChoraleComposer::Cadence cad; };

Parsed parseToken(const std::string& tok)
{
    using C = ChoraleComposer;
    static const std::pair<const char*, int> names[] = {
        { "bII", 1 }, { "vii", 11 }, { "VII", 10 }, { "VI", 8 }, { "IV", 5 }, { "III", 3 },
        { "iv", 5 }, { "ii", 2 }, { "V", 7 }, { "v", 7 }, { "i", 0 },
    };
    Parsed p { 0, C::Min, 0, 4, C::None };
    std::string s = tok;
    if (auto bang = s.find('!'); bang != std::string::npos)
    {
        char c = s[bang + 1];
        p.cad = c == 'h' ? C::Half : c == 'a' ? C::Authentic : c == 'd' ? C::Deceptive : C::Plagal;
        s = s.substr(0, bang);
    }
    auto colon = s.find(':');
    p.beats = std::atoi(s.c_str() + colon + 1);
    s = s.substr(0, colon);
    if (auto slash = s.find('/'); slash != std::string::npos) { p.inversion = std::atoi(s.c_str() + slash + 1); s = s.substr(0, slash); }
    std::string name, suffix;
    for (auto& [n, r] : names)
        if (s.rfind(n, 0) == 0) { name = n; p.root = r; suffix = s.substr(std::strlen(n)); break; }
    const bool upper = name[0] == 'V' || name[0] == 'I' || name == "bII";
    if (suffix == "7") p.q = upper ? C::Dom7 : C::Min7;
    else if (suffix == "M7") p.q = C::Maj7;
    else if (suffix == "add9") p.q = upper ? C::MajAdd9 : C::MinAdd9;
    else if (suffix == "h7") p.q = C::HalfDim7;
    else if (suffix == "o7") p.q = C::Dim7;
    else p.q = upper ? C::Maj : C::Min;
    return p;
}

bool hasSeventh(ChoraleComposer::Quality q)
{
    using C = ChoraleComposer;
    return q == C::Min7 || q == C::Dom7 || q == C::Maj7 || q == C::HalfDim7 || q == C::Dim7;
}

int pc(int p) { return ((p % 12) + 12) % 12; }

// Rhythms for one chord's worth of melody, in sixteenths, by the chord's length.
const std::vector<std::vector<int>>& rhythmsFor(int len, int character)
{
    // character 0: calm and sparse · 1: moving · 2: dense (the climax)
    static const std::vector<std::vector<int>> r4 = { { 4 }, { 2, 2 } };
    static const std::vector<std::vector<int>> r8[3] = { { { 8 }, { 6, 2 } }, { { 8 }, { 4, 4 }, { 6, 2 } }, { { 4, 4 }, { 6, 2 }, { 2, 2, 4 } } };
    static const std::vector<std::vector<int>> r16[3] = {
        { { 16 }, { 12, 4 }, { 8, 8 } },
        { { 8, 8 }, { 12, 4 }, { 8, 4, 4 }, { 4, 4, 8 }, { 6, 2, 8 } },
        { { 4, 4, 4, 4 }, { 6, 2, 4, 4 }, { 8, 4, 4 }, { 4, 4, 8 }, { 6, 2, 8 } },
    };
    static const std::vector<std::vector<int>> r32[3] = { { { 32 }, { 24, 8 } }, { { 16, 16 }, { 24, 8 } }, { { 16, 8, 8 }, { 8, 8, 16 } } };
    static std::vector<std::vector<int>> other;
    if (len == 4) return r4;
    if (len == 8) return r8[character];
    if (len == 16) return r16[character];
    if (len == 32) return r32[character];
    other = { { len } };
    return other;
}

} // namespace

std::vector<int> ChoraleComposer::chordPcs(const Chord& c) const
{
    static const std::vector<int> iv[] = {
        { 0, 3, 7 }, { 0, 4, 7 }, { 0, 3, 6 }, { 0, 3, 7, 10 }, { 0, 4, 7, 10 }, { 0, 4, 7, 11 },
        { 0, 3, 6, 10 }, { 0, 3, 6, 9 }, { 0, 3, 7, 14 }, { 0, 4, 7, 14 },
    };
    std::vector<int> out;
    for (int i : iv[c.quality]) out.push_back(pc(tonicPc_ + c.root + i));
    return out;
}

std::vector<int> ChoraleComposer::scaleOf(const Chord& c) const
{
    // Natural minor; around the dominant the raised seventh (harmonic minor) and, so the step to
    // it is not an augmented second, the raised sixth; over a major IV the dorian sixth.
    std::vector<int> s = { 0, 2, 3, 5, 7, 8, 10 };
    if (c.root == kDominant || c.root == kLeadingTone) s = { 0, 2, 3, 5, 7, 9, 11 };
    else if (c.root == 5 && (c.quality == Maj || c.quality == MajAdd9)) s = { 0, 2, 3, 5, 7, 9, 10 };
    for (auto& x : s) x = pc(tonicPc_ + x);
    return s;
}

bool ChoraleComposer::active(int track, int bar) const
{
    return bar >= 0 && bar < totalBars() && (sectionAt(bar).layers & bit(track));
}

ChoraleComposer::ChoraleComposer(uint64_t seed) : Composer(seed ^ 0xC0A1E5ull)
{
    static constexpr int tonics[] = { 2, 9, 4, 7, 0, 5, 11 }; // D A E G C F B
    tonicPc_ = tonics[rng_.range(0, 6)];
    bpm_ = rng_.range(80, 88);
    swingTicks_ = int(k16 * (0.06 + 0.1 * rng_.uniform())); // 53 to 58 % swing

    planForm();
    composeMelody(nullptr);
    // The theme comes back: the Return's melody leans on the Theme's, chord for chord.
    std::vector<int> bias(chords_.size(), -1);
    int themeSec = -1, returnSec = -1;
    for (size_t s = 0; s < sections_.size(); ++s)
    {
        if (std::strcmp(sections_[s].name, "Theme") == 0) themeSec = int(s);
        if (std::strcmp(sections_[s].name, "Return") == 0) returnSec = int(s);
    }
    std::vector<int> themeIdx, returnIdx;
    for (size_t i = 0; i < chords_.size(); ++i)
    {
        if (chords_[i].section == themeSec) themeIdx.push_back(int(i));
        if (chords_[i].section == returnSec) returnIdx.push_back(int(i));
    }
    for (size_t k = 0; k < std::min(themeIdx.size(), returnIdx.size()); ++k)
        bias[size_t(returnIdx[k])] = voicings_[size_t(themeIdx[k])][3];
    composeMelody(&bias);
    voiceLowerParts();
    writeVoices();
    writeRhythm();
    writeLevels();
    finish();
}

// ---------------------------------------------------------------- the form
void ChoraleComposer::planForm()
{
    struct T { const char* name; float energy; uint8_t layers; std::vector<Kind> phrases; };
    const uint8_t voices = bit(Pad), micro = bit(Seq), drums = bit(Bass), piano = bit(Lead);
    const std::vector<T> plan = {
        { "Invocation", .12f, voices, { AntSlow, ConsSlow } },
        { "Pulse", .30f, uint8_t(voices | micro), { Ant, Cons, Ant, ConsPlag } },
        { "Theme", .55f, uint8_t(voices | micro | drums | piano), { Ant, Cons, Ant, Cons, Ant, Cons, Ant, Cons } },
        { "Episode", .68f, uint8_t(voices | micro | drums | piano), { SeqA, SeqB, Euph, Euph, SeqA, SeqB, Ant, Cons } },
        { "Breakdown", .35f, uint8_t(voices | micro | piano), { Pedal, Pedal, Ant, ConsDec } },
        { "Return", .78f, uint8_t(voices | micro | drums | piano), { Ant, ConsDec, Ant, Cons, Ant, ConsDec, Ant, Cons } },
        { "Apotheosis", 1.0f, uint8_t(voices | micro | drums | piano), { Euph, Euph, Euph, Euph, Euph, Euph, Ant, Cons } },
        { "Dissolution", .22f, uint8_t(voices | micro | piano), { AntSlow, EuphSlow, ConsSlow, Final } },
    };

    // Phrases recur where the form says the theme recurs: the Theme's choices are kept and the
    // Return takes the same ones (its deceptive cadences aside).
    std::vector<int> themeChoices;
    int bar = 0, at16 = 0;
    float prevEnergy = 0;
    for (size_t s = 0; s < plan.size(); ++s)
    {
        auto& t = plan[s];
        const int startBar = bar;
        const bool theme = std::strcmp(t.name, "Theme") == 0, ret = std::strcmp(t.name, "Return") == 0;
        for (size_t ph = 0; ph < t.phrases.size(); ++ph)
        {
            const Kind kind = t.phrases[ph];
            const auto& list = templates(kind);
            int choice = rng_.range(0, int(list.size()) - 1);
            if (theme) themeChoices.push_back(choice);
            if (ret && ph < themeChoices.size() && kind != ConsDec) choice = themeChoices[ph] % int(list.size());
            const int scale = (kind == AntSlow || kind == ConsSlow || kind == EuphSlow) ? 2 : 1;

            std::vector<Parsed> tokens;
            std::string str = list[size_t(choice)];
            for (size_t p = 0; p < str.size();)
            {
                size_t q = str.find(' ', p);
                if (q == std::string::npos) q = str.size();
                tokens.push_back(parseToken(str.substr(p, q - p)));
                p = q + 1;
            }
            int phraseLen = 0;
            for (auto& tk : tokens) phraseLen += tk.beats * 4 * scale;
            const int phraseStart = at16;
            for (auto& tk : tokens)
            {
                Chord c;
                c.root = tk.root; c.quality = tk.q; c.inversion = tk.inversion;
                c.start = at16; c.len = tk.beats * 4 * scale; c.cadence = tk.cad;
                c.pedal = kind == Pedal;
                c.phraseStart = phraseStart; c.phraseLen = phraseLen; c.section = int(s);
                chords_.push_back(c);
                at16 += c.len;
            }
            bar += phraseLen / 16;
        }
        const int bars = bar - startBar;
        sections_.push_back({ t.name, startBar, bars, t.energy, t.layers, theme || ret });
        for (int i = 0; i < bars; ++i)
        {
            sectionOfBar_.push_back(int(sections_.size()) - 1);
            float k = std::min(1.0f, float(i + 1) / (bars * 0.5f));
            energyOfBar_.push_back(prevEnergy + (t.energy - prevEnergy) * k);
        }
        prevEnergy = t.energy;
    }
}

// ---------------------------------------------------------------- the melody: one note per chord, by search
// Each chord gets the soprano note it starts with. The search scores an arch per phrase (rising
// to about 60 % of it, falling to the cadence), mostly stepwise motion, leaps recovered by a
// step back, and the cadence degrees: the tonic at an authentic cadence, the second or the
// leading tone over the dominant.
void ChoraleComposer::composeMelody(const std::vector<int>* themeBias)
{
    const int n = int(chords_.size());
    voicings_.assign(size_t(n), Voicing { 0, 0, 0, 0 });
    std::vector<std::vector<int>> cand(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k)
    {
        auto pcs = chordPcs(chords_[size_t(k)]);
        const int hi = sections_[size_t(chords_[size_t(k)].section)].energy > .9f ? 81 : 79;
        for (int p = 62; p <= hi; ++p)
            if (std::find(pcs.begin(), pcs.end(), pc(p)) != pcs.end()) cand[size_t(k)].push_back(p);
    }

    auto nodeCost = [&](int k, int p) {
        const auto& c = chords_[size_t(k)];
        const float energy = sections_[size_t(c.section)].energy;
        const double pos = double(c.start - c.phraseStart) / c.phraseLen;
        const double base = 64.0 + 6.0 * energy;
        const double target = base + 6.0 * std::sin(3.14159265 * std::pow(std::max(pos, 0.0), 0.75));
        double cost = 0.035 * (p - target) * (p - target);
        const int deg = pc(p - tonicPc_);
        const bool last = c.start + c.len == c.phraseStart + c.phraseLen;
        if (last && c.cadence == Authentic) cost += deg == 0 ? 0 : 45;
        if (last && c.cadence == Plagal) cost += deg == 0 ? 0 : (deg == 3 || deg == 7) ? 4 : 20;
        if (last && c.cadence == Deceptive) cost += deg == 0 ? 0 : 6;
        if (last && c.cadence == Half) cost += (deg == 2 || deg == 11) ? 0 : deg == 7 ? 2 : 12;
        // the dominant just before an authentic cadence: second or leading tone, to resolve home
        if (k + 1 < n && chords_[size_t(k + 1)].cadence == Authentic && c.root == kDominant)
            cost += (deg == 2 || deg == 11) ? 0 : deg == 5 ? 1 : 6;
        if (themeBias && (*themeBias)[size_t(k)] >= 0) cost += p == (*themeBias)[size_t(k)] ? -12 : 0;
        return cost;
    };
    auto stepCost = [](int a, int b) {
        const int d = std::abs(b - a);
        return d == 0 ? 1.4 : d <= 2 ? 0.0 : d <= 4 ? 1.0 : d == 5 ? 2.5 : d <= 7 ? 3.5 : 6.0 + 0.5 * (d - 7);
    };
    // The bass note of every chord is already known (its inversion, or the pedal): a melody that
    // stands a fifth or an octave above it twice running, while both move, is a parallel the lower
    // voices could not undo. Measured: 72 of 4585 chord changes before this.
    std::vector<int> bassPc(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k)
    {
        const auto& c = chords_[size_t(k)];
        const auto pcs = chordPcs(c);
        bassPc[size_t(k)] = c.pedal ? pc(tonicPc_ + kDominant) : pcs[size_t(std::min(c.inversion, int(pcs.size()) - 1))];
    }
    auto outerCost = [&](int k, int q, int p) {
        const int before = pc(q - bassPc[size_t(k - 1)]), after = pc(p - bassPc[size_t(k)]);
        return (p != q && bassPc[size_t(k)] != bassPc[size_t(k - 1)] && before == after && (after == 0 || after == 7)) ? 30.0 : 0.0;
    };

    // State: (this note, previous note), so a leap can be recovered by stepping back.
    struct Cell { double cost = std::numeric_limits<double>::infinity(); int from = -1; };
    std::vector<std::vector<Cell>> dp(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k)
    {
        const size_t sz = cand[size_t(k)].size(), psz = k > 0 ? cand[size_t(k - 1)].size() : 1;
        dp[size_t(k)].assign(sz * psz, {});
        for (size_t i = 0; i < sz; ++i)
        {
            const int p = cand[size_t(k)][i];
            const double nc = nodeCost(k, p);
            if (k == 0) { dp[0][i] = { nc + 0.02 * (p - 66) * (p - 66), -1 }; continue; }
            for (size_t j = 0; j < psz; ++j)
            {
                const int q = cand[size_t(k - 1)][j];
                Cell best;
                const size_t ppsz = k > 1 ? cand[size_t(k - 2)].size() : 1;
                for (size_t h = 0; h < ppsz; ++h)
                {
                    const auto& prev = dp[size_t(k - 1)][j * ppsz + h];
                    if (!std::isfinite(prev.cost)) continue;
                    double c = prev.cost + stepCost(q, p) + outerCost(k, q, p);
                    if (k > 1)
                    {
                        const int r = cand[size_t(k - 2)][h];
                        const int leap = q - r, move = p - q;
                        if (std::abs(leap) >= 5 && (leap > 0) == (move > 0) && move != 0) c += 4.0; // a leap goes on: penalized
                        if (std::abs(leap) >= 5 && (leap > 0) != (move > 0) && std::abs(move) <= 2 && move != 0) c -= 0.6; // recovered
                    }
                    if (c < best.cost) best = { c, int(h) };
                }
                dp[size_t(k)][i * psz + j] = { best.cost + nc, best.from };
            }
        }
    }
    // Trace back from the cheapest final state.
    const int last = n - 1;
    size_t bi = 0;
    double bc = std::numeric_limits<double>::infinity();
    for (size_t s = 0; s < dp[size_t(last)].size(); ++s)
        if (dp[size_t(last)][s].cost < bc) bc = dp[size_t(last)][s].cost, bi = s;
    for (int k = last; k >= 0; --k)
    {
        const size_t psz = k > 0 ? cand[size_t(k - 1)].size() : 1;
        const size_t i = bi / psz, j = bi % psz;
        voicings_[size_t(k)][3] = cand[size_t(k)][i];
        if (k > 0)
        {
            const size_t ppsz = k > 1 ? cand[size_t(k - 2)].size() : 1;
            bi = j * ppsz + size_t(std::max(0, dp[size_t(k)][bi].from));
        }
    }
}

// ---------------------------------------------------------------- bass, tenor and alto, by search
void ChoraleComposer::voiceLowerParts()
{
    const int n = int(chords_.size());
    struct V { int b, t, a; double node; };
    std::vector<std::vector<V>> cand(static_cast<size_t>(n));
    const int lt = pc(tonicPc_ + kLeadingTone);

    for (int k = 0; k < n; ++k)
    {
        const auto& c = chords_[size_t(k)];
        const auto pcs = chordPcs(c);
        const int s = voicings_[size_t(k)][3];
        const int bassPc = c.pedal ? pc(tonicPc_ + kDominant) : pcs[size_t(std::min(c.inversion, int(pcs.size()) - 1))];
        for (int b = kVoiceRange[0][0]; b <= kVoiceRange[0][1]; ++b)
        {
            if (pc(b) != bassPc) continue;
            for (int t = kVoiceRange[1][0]; t <= kVoiceRange[1][1]; ++t)
            {
                if (std::find(pcs.begin(), pcs.end(), pc(t)) == pcs.end() || t <= b || t - b > 24) continue;
                for (int a = kVoiceRange[2][0]; a <= kVoiceRange[2][1]; ++a)
                {
                    if (std::find(pcs.begin(), pcs.end(), pc(a)) == pcs.end() || a <= t || a >= s || a - t > 12 || s - a > 12) continue;
                    // Which chord tones are present, and how often.
                    int count[4] = {};
                    for (int p : { b, t, a, s })
                        for (size_t r = 0; r < pcs.size(); ++r)
                            if (pc(p) == pcs[r]) ++count[r];
                    double node = 0;
                    if (count[1] == 0) node += 80;                                     // no third
                    if (count[0] == 0 && !c.pedal) node += 40;                         // no root
                    if (pcs.size() == 4 && count[3] == 0) node += hasSeventh(c.quality) ? 15 : 8;
                    if (count[2] == 0) node += 1.5;
                    if ((c.quality == Maj || c.quality == Dom7) && count[1] > 1) node += 6;
                    if (count[1] > 1 && pcs[1] == lt) node += 60;                     // doubled leading tone
                    if (c.root == kLeadingTone && count[0] > 1) node += 60;
                    if (pcs.size() == 4 && count[3] > 1) node += 40;                   // doubled seventh or ninth
                    for (int r = 0; r < 4; ++r) if (count[r] > 2) node += 20;
                    if (pcs.size() == 3 && count[0] < 2 && count[2] > 1) node += 2;    // double the root, not the fifth
                    node += 0.08 * (std::abs(t - 57) + std::abs(a - 64)) + 0.03 * std::abs(b - 47);
                    cand[size_t(k)].push_back({ b, t, a, node });
                }
            }
        }
        if (cand[size_t(k)].empty()) // a corner the rules cannot reach: keep the nearest fit
            cand[size_t(k)].push_back({ s - 24, s - 15, s - 8, 500 });
    }

    auto motion = [&](int k, const V& u, const V& v) {
        const auto& cu = chords_[size_t(k - 1)];
        const auto& cv = chords_[size_t(k)];
        const int su = voicings_[size_t(k - 1)][3], sv = voicings_[size_t(k)][3];
        const int U[4] = { u.b, u.t, u.a, su }, W[4] = { v.b, v.t, v.a, sv };
        double c = std::abs(v.t - u.t) + std::abs(v.a - u.a) + 0.35 * std::abs(v.b - u.b);
        for (int i = 1; i <= 2; ++i) if (std::abs(W[i] - U[i]) > 4) c += 1.5 * (std::abs(W[i] - U[i]) - 4);
        if (std::abs(v.b - u.b) > 12) c += 5;
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
            {
                const int before = pc(U[j] - U[i]), after = pc(W[j] - W[i]);
                const int mi = W[i] - U[i], mj = W[j] - U[j];
                if (mi != 0 && mj != 0 && (mi > 0) == (mj > 0) && before == after && (after == 7 || after == 0))
                    c += 120; // parallel fifths or octaves
            }
        {   // hidden fifths and octaves between the outer voices, when the soprano leaps into them
            const int after = pc(sv - v.b), mb = v.b - u.b, ms = sv - su;
            if ((after == 7 || after == 0) && mb != 0 && ms != 0 && (mb > 0) == (ms > 0) && std::abs(ms) > 2) c += 25;
        }
        for (int i = 0; i < 3; ++i) // a voice passing the old place of its neighbour
            if (W[i] > U[i + 1] || W[i + 1] < U[i]) c += 8;
        // tendencies: the leading tone rises into the tonic chord, a seventh falls by step
        const auto pu = chordPcs(cu);
        const bool toTonic = cv.root == 0;
        for (int i = 0; i < 3; ++i)
        {
            if (cu.pedal && i == 0) continue;
            if (toTonic && pc(U[i]) == lt && W[i] - U[i] != 1) c += i == 2 ? 10 : 16;
            if (hasSeventh(cu.quality) && pu.size() == 4 && pc(U[i]) == pu[3])
            {
                const int m = W[i] - U[i];
                const auto pv = chordPcs(cv);
                const bool common = m == 0 && std::find(pv.begin(), pv.end(), pc(U[i])) != pv.end();
                if (!(m == -1 || m == -2 || common)) c += 18;
            }
            if (i > 0 && W[i] == U[i]) c -= 0.5; // a held common tone
        }
        return c;
    };

    std::vector<std::vector<double>> cost(static_cast<size_t>(n));
    std::vector<std::vector<int>> from(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k)
    {
        const auto& cv = cand[size_t(k)];
        cost[size_t(k)].assign(cv.size(), std::numeric_limits<double>::infinity());
        from[size_t(k)].assign(cv.size(), 0);
        for (size_t i = 0; i < cv.size(); ++i)
        {
            if (k == 0) { cost[0][i] = cv[i].node; continue; }
            const auto& cu = cand[size_t(k - 1)];
            for (size_t j = 0; j < cu.size(); ++j)
            {
                const double c = cost[size_t(k - 1)][j] + motion(k, cu[j], cv[i]);
                if (c < cost[size_t(k)][i]) cost[size_t(k)][i] = c, from[size_t(k)][i] = int(j);
            }
            cost[size_t(k)][i] += cv[i].node;
        }
    }
    size_t bi = size_t(std::min_element(cost[size_t(n - 1)].begin(), cost[size_t(n - 1)].end()) - cost[size_t(n - 1)].begin());
    for (int k = n - 1; k >= 0; --k)
    {
        const auto& v = cand[size_t(k)][bi];
        voicings_[size_t(k)][0] = v.b;
        voicings_[size_t(k)][1] = v.t;
        voicings_[size_t(k)][2] = v.a;
        bi = size_t(from[size_t(k)][bi]);
    }
}

// ---------------------------------------------------------------- the voices, decorated and played
void ChoraleComposer::writeVoices()
{
    const int n = int(chords_.size());
    // Per voice the list of (pitch, start16, length16); later turned into notes with human timing.
    struct VN { int pitch, start, len; bool tie; };
    std::vector<VN> voice[4];
    Rng r(rng_.next());

    auto scaleStep = [&](const Chord& c, int p, int dir) {
        auto sc = scaleOf(c);
        for (int q = p + dir; std::abs(q - p) <= 3; q += dir)
            if (std::find(sc.begin(), sc.end(), pc(q)) != sc.end()) return q;
        return p + dir;
    };
    auto scaleDistance = [&](const Chord& c, int from, int to) {
        int d = 0, p = from;
        while (p != to && d < 24) { p = scaleStep(c, p, to > p ? 1 : -1); ++d; if ((to - p) * (to - from) < 0) break; }
        return d;
    };

    for (int k = 0; k < n; ++k)
    {
        const auto& c = chords_[size_t(k)];
        const auto& sec = sections_[size_t(c.section)];
        const int character = sec.energy > .9f ? 2 : sec.energy > .45f ? 1 : 0;
        const bool themed = sec.restoreTheme;
        const int next = k + 1 < n ? voicings_[size_t(k + 1)][3] : voicings_[size_t(k)][3];

        // ---- soprano
        const auto& rs = rhythmsFor(c.len, character);
        // In the theme, the rhythm is chosen by the chord's place in its section, so the Return repeats it.
        int pick;
        if (themed) pick = int(((uint64_t(c.start - sec.startBar * 16) * 2654435761ull) >> 7) % rs.size());
        else pick = r.range(0, int(rs.size()) - 1);
        const auto& rhythm = rs[size_t(pick)];
        int at = c.start;
        int cur = voicings_[size_t(k)][3];
        bool firstDone = false;
        // A 4-3 suspension over the dominant at a cadence: hold the note before, resolve down.
        const bool cadential = c.root == kDominant && (c.cadence == Half || (k + 1 < n && (chords_[size_t(k + 1)].cadence == Authentic || chords_[size_t(k + 1)].cadence == Deceptive)));
        if (cadential && !voice[3].empty() && c.len >= 8)
        {
            const int prevP = voice[3].back().pitch, d = prevP - cur;
            if ((d == 1 || d == 2) && r.chance(.7))
            {
                voice[3].push_back({ prevP, at, c.len / 2, true });
                voice[3].push_back({ cur, at + c.len / 2, c.len - c.len / 2, false });
                firstDone = true;
            }
        }
        if (!firstDone)
        {
            const bool strong = (c.start % 8) == 0;
            const bool appoggiatura = rhythm.size() >= 2 && strong && character >= 1 && r.chance(character == 2 ? .3 : .15);
            for (size_t i = 0; i < rhythm.size(); ++i)
            {
                int p;
                if (i == 0) p = appoggiatura ? scaleStep(c, cur, 1) : cur;          // a sigh: the step above, falling
                else if (i == 1 && appoggiatura) p = cur;
                else
                {
                    const int remaining = int(rhythm.size()) - int(i);
                    const int dist = scaleDistance(c, cur, next);
                    if (dist > remaining) { int q = next; for (int s = 0; s < remaining; ++s) q = scaleStep(c, q, next > cur ? -1 : 1); p = q; }
                    else if (next == cur) p = scaleStep(c, cur, r.chance(.6) ? 1 : -1); // a neighbour
                    else p = scaleStep(c, cur, next > cur ? 1 : -1);                     // passing toward the next note
                    p = std::clamp(p, kVoiceRange[3][0], kVoiceRange[3][1]);
                }
                voice[3].push_back({ p, at, rhythm[i], false });
                cur = (i == 0 && appoggiatura) ? voicings_[size_t(k)][3] : p;
                at += rhythm[i];
            }
        }

        // ---- alto, tenor, bass
        for (int v = 0; v < 3; ++v)
        {
            const int p = voicings_[size_t(k)][size_t(v)];
            int start = c.start, len = c.len;
            // A suspension into this chord: the previous note held over the barline, then resolved down.
            if (k > 0 && v > 0)
            {
                const int prevP = voicings_[size_t(k - 1)][size_t(v)], d = prevP - p;
                const auto pcs = chordPcs(c);
                const bool dissonant = std::find(pcs.begin(), pcs.end(), pc(prevP)) == pcs.end();
                const bool chain = chords_[size_t(k - 1)].pedal == false && sec.energy > .6f && std::strcmp(sec.name, "Episode") == 0;
                const double chance = chain ? .7 : .25 + .2 * sec.energy;
                if ((d == 1 || d == 2) && dissonant && c.len >= 8 && r.chance(chance))
                {
                    voice[v].push_back({ prevP, start, len / 2, true });
                    start += len / 2; len -= len / 2;
                }
            }
            // A passing tone at the end of this chord, when the voice moves by a third into the next.
            if (k + 1 < n && (v > 0 || !c.pedal) && len >= 8)
            {
                const int np = voicings_[size_t(k + 1)][size_t(v)], d = np - p;
                if ((std::abs(d) == 3 || std::abs(d) == 4) && r.chance(v == 0 ? .5 : .35))
                {
                    const int pass = scaleStep(c, p, d > 0 ? 1 : -1);
                    if (pass != np)
                    {
                        const int tail = len >= 16 ? 4 : 2;
                        voice[v].push_back({ p, start, len - tail, false });
                        voice[v].push_back({ pass, start + len - tail, tail, false });
                        continue;
                    }
                }
            }
            // A long chord: the bass may leap the octave halfway.
            if (v == 0 && !c.pedal && len >= 16 && r.chance(.2) && p + 12 <= kVoiceRange[0][1])
            {
                voice[0].push_back({ p, start, len / 2, false });
                voice[0].push_back({ p + 12, start + len / 2, len - len / 2, false });
                continue;
            }
            voice[v].push_back({ p, start, len, false });
        }
    }

    // Into notes: legato (each note to the next), ties merged, human timing and phrase dynamics.
    static constexpr int lean[4] = { 2, 0, 0, -6 }; // the soprano leads a little
    for (int v = 0; v < 4; ++v)
    {
        auto& L = voice[v];
        for (size_t i = 0; i < L.size(); ++i)
        {
            // merge ties and repeated inner notes into one long note
            int end16 = L[i].start + L[i].len;
            size_t j = i + 1;
            while (j < L.size() && L[j].pitch == L[i].pitch && (L[j].tie || v < 3)) { end16 = L[j].start + L[j].len; ++j; }
            const int bar = std::min(totalBars() - 1, L[i].start / 16);
            const float energy = energyAt(bar);
            auto& c = *std::find_if(chords_.rbegin(), chords_.rend(), [&](const Chord& ch) { return ch.start <= L[i].start; });
            const double pos = double(L[i].start - c.phraseStart) / c.phraseLen;
            float vel = float((0.36 + 0.4 * energy) * (0.86 + 0.24 * std::sin(3.14159265 * pos)));
            vel += v == 3 ? 0.06f : v == 0 ? 0.02f : -0.05f;
            const int64_t t0 = int64_t(L[i].start) * k16 + lean[v] + r.range(-5, 5);
            const int64_t t1 = int64_t(end16) * k16 + (j < L.size() && L[j].pitch != L[i].pitch ? 10 : -6);
            if (active(Pad, bar)) notes_.push_back({ Pad, L[i].pitch, std::max<int64_t>(0, t0), t1, std::clamp(vel, .05f, 1.0f) });
            // the piano doubles the melody, an octave up at the peak, and breaks chords there
            if (v == 3 && active(Lead, bar))
            {
                const int pp = L[i].pitch + (energy > .9f ? 12 : 0);
                notes_.push_back({ Lead, pp, std::max<int64_t>(0, t0 + 4), std::max(t0 + 60, t1 - 40), std::clamp(vel + (energy > .9f ? -.06f : .04f), .05f, 1.0f) });
            }
            i = j - 1;
        }
    }
    for (int k = 0; k < n; ++k)
    {
        const auto& c = chords_[size_t(k)];
        const int bar = c.start / 16;
        const auto& sec = sections_[size_t(c.section)];
        if (!active(Lead, bar) || c.len < 16 || sec.energy < .6f || !r.chance(.5)) continue;
        const auto& vo = voicings_[size_t(k)];
        const int arp[4] = { vo[1] + 12, vo[2] + 12, vo[3] + 12, vo[2] + 12 };
        for (int i = 0; i < 4; ++i)
        {
            const int64_t t = int64_t(c.start + c.len / 2 + 2 * i) * k16 + (i % 2 ? swingTicks_ / 2 : 0);
            if (arp[i] <= 96) notes_.push_back({ Lead, arp[i], t, t + 2 * k16 - 20, .2f + .04f * float(i % 2 == 0) });
        }
    }
}

// ---------------------------------------------------------------- the rhythm section
void ChoraleComposer::writeRhythm()
{
    Rng r(rng_.next());
    enum : int { kKick = 36, kGhost = 36, kBack = 43, kLowTom = 40, kSub = 31 };
    enum : int { kTick = 84, kClick = 72, kAccent = 79, kLow = 67 };
    // Deep drum patterns, 16 steps. Step 0 (the kick) and step 8 (the backbeat) never move: they
    // are what keeps the time under everything else. K kick, k ghost kick, t low tom.
    static const char* bank[] = {
        "K.......T.......", "K.......T.....k.", "K.....k.T.......", "K..k....T..K....",
        "K......KT...k..t", "K.k.....T.k..K..", "K..K..k.T....k.t", "K..k..K.T.k..t.k",
    };
    std::string pattern = bank[0];
    int crossPeriod = 5;

    for (int bar = 0; bar < totalBars(); ++bar)
    {
        const auto& sec = sectionAt(bar);
        const float e = energyAt(bar);
        const int inSec = bar - sec.startBar;
        const bool lastBarOfSection = inSec == sec.bars - 1 && bar + 1 < totalBars();
        const int64_t b0 = int64_t(bar) * kBarTicks;
        auto stepTick = [&](int step) { return b0 + int64_t(step) * k16 + (step % 2 ? swingTicks_ : 0); };

        if (inSec == 0) crossPeriod = r.chance(.5) ? 5 : 7;

        // ---- micro percussion: a click on every beat, then swing, ratchets and cross-accents
        if (active(Seq, bar))
        {
            const bool drop = lastBarOfSection && r.chance(.6); // a breath before the next section
            for (int step = 0; step < 16; ++step)
            {
                if (drop && step >= 12) break;
                const bool beat = step % 4 == 0, eighth = step % 2 == 0;
                if (beat)
                {
                    const bool one = step == 0;
                    const int p = one ? (inSec == 0 ? kAccent + 5 : kAccent) : kTick;
                    notes_.push_back({ Seq, p, b0 + step * k16, b0 + step * k16 + 90, one ? .72f : .55f });
                    continue;
                }
                const double density = .12 + .72 * e;
                if (r.chance(eighth ? density * 1.15 : density * .8))
                {
                    const int64_t t = stepTick(step) + (eighth ? 0 : r.range(-6, 6));
                    const float vel = float(eighth ? .4 + .08 * r.uniform() : .24 + .1 * r.uniform());
                    notes_.push_back({ Seq, eighth ? kClick : (r.chance(.5) ? kTick : kClick + 2), t, t + 70, vel });
                }
            }
            // cross-accents every 5 or 7 sixteenths, running across the barlines
            for (int step = 0; step < 16 && e > .25f; ++step)
                if ((bar * 16 + step) % crossPeriod == 0 && step % 4 != 0 && !(drop && step >= 12))
                    notes_.push_back({ Seq, kLow, stepTick(step), stepTick(step) + 80, .38f + .12f * e });
            // ratchets: one sixteenth split into 3 to 6 hits, its pitch sweeping
            if (std::strcmp(sec.name, "Invocation") != 0 && r.chance(.1 + .5 * e))
            {
                static constexpr std::array<int, 6> spots = { 15, 7, 14, 11, 3, 13 };
                const int step = spots[size_t(r.weighted(std::array<double, 6> { .35, .2, .15, .1, .1, .1 }))];
                const int hits = r.range(3, 6), dir = r.chance(.5) ? 1 : -1;
                int p = r.range(66, 80);
                for (int h = 0; h < hits; ++h, p += dir * r.range(1, 3))
                {
                    const int64_t t = b0 + step * k16 + h * (k16 / hits);
                    notes_.push_back({ Seq, std::clamp(p, kMicroPitchRange[0], kMicroPitchRange[1]), t, t + std::max(20, k16 / hits - 12),
                                       float(dir > 0 ? .22 + .08 * h : .55 - .07 * h) });
                }
            }
        }

        // ---- deep drums
        if (active(Bass, bar))
        {
            if (inSec % 4 == 0)
            {   // a new four bars: a pattern by energy, then a mutation of its weak steps
                const int lo = e < .5f ? 0 : e < .8f ? 2 : 4;
                pattern = bank[r.range(lo, lo + 3 > 7 ? 7 : lo + 3)];
                if (r.chance(.4))
                {
                    const int s = r.range(1, 15);
                    if (s != 8) pattern[size_t(s)] = pattern[size_t(s)] == '.' ? 'k' : '.';
                }
            }
            const bool fill = inSec % 8 == 7 && e > .4f;
            for (int step = 0; step < 16; ++step)
            {
                if (fill && step >= 8) break;
                const char x = pattern[size_t(step)];
                if (x == '.') continue;
                int p = x == 'T' ? kBack : x == 't' ? kLowTom : kKick;
                float vel = x == 'K' ? .9f : x == 'T' ? .76f : x == 't' ? .5f : .45f;
                int64_t t = step == 0 || step == 8 ? b0 + step * k16 : stepTick(step) + (x == 'k' ? r.range(4, 12) : 0); // ghosts lay back
                notes_.push_back({ Bass, p, t, t + 160, vel });
            }
            if (fill)
            {   // a tom fill over beats 3 and 4: sixteenths or sextuplets, falling, louder
                static constexpr int toms[] = { 48, 45, 43, 41, 40, 38, 36 };
                const bool sextuplets = r.chance(.45);
                const int count = sextuplets ? 12 : 8, spacing = sextuplets ? kPPQ / 6 : k16;
                for (int h = 0; h < count; ++h)
                {
                    const int64_t t = b0 + 2 * kPPQ + h * spacing;
                    const int p = toms[std::min(6, h * 7 / count)];
                    notes_.push_back({ Bass, p, t, t + spacing - 10, .45f + .4f * float(h) / count });
                }
            }
            const bool arrival = inSec == 0 && (std::strcmp(sec.name, "Theme") == 0 || std::strcmp(sec.name, "Return") == 0 || std::strcmp(sec.name, "Apotheosis") == 0);
            if (arrival) notes_.push_back({ Bass, kSub, b0, b0 + 2 * kPPQ, .9f });
        }
    }
}

// ---------------------------------------------------------------- levels and the hand-over to the engine
void ChoraleComposer::writeLevels()
{
    for (int bar = 0; bar < totalBars(); ++bar)
        for (int tr = 0; tr < kNumTracks; ++tr)
        {
            float level = 0;
            if (active(tr, bar))
            {
                const auto& sec = sectionAt(bar);
                const int in = bar - sec.startBar, left = sec.startBar + sec.bars - bar;
                const float fadeIn = active(tr, sec.startBar - 1) ? 1.0f : std::min(1.0f, (in + 1) / 2.0f);
                const float fadeOut = active(tr, sec.startBar + sec.bars) || bar + 1 == totalBars() ? 1.0f : std::min(1.0f, (left - 0.5f) / 1.5f);
                level = std::min(fadeIn, fadeOut);
                if (tr == Pad) level *= 0.7f + 0.3f * energyAt(bar); // calm sections sit lower (measured: the Dissolution was louder than the Theme)
            }
            levels_.push_back({ int64_t(bar) * kBarTicks, Event::Level, uint8_t(tr), 0, level });
        }
}

void ChoraleComposer::finish()
{
    // Notes of one pitch on one track that overlap are merged, so a key is never struck twice
    // while held; then each note becomes its on and off, filed under the bar it starts in.
    std::sort(notes_.begin(), notes_.end(), [](const Note& a, const Note& b) {
        if (a.track != b.track) return a.track < b.track;
        if (a.pitch != b.pitch) return a.pitch < b.pitch;
        return a.start < b.start;
    });
    std::vector<Note> merged;
    for (auto& nt : notes_)
    {
        if (!merged.empty() && merged.back().track == nt.track && merged.back().pitch == nt.pitch && nt.start < merged.back().end)
        {
            merged.back().end = std::max(merged.back().end, nt.end);
            merged.back().vel = std::max(merged.back().vel, nt.vel);
        }
        else
            merged.push_back(nt);
    }
    byBar_.assign(size_t(totalBars()), {});
    const int64_t endTick = int64_t(totalBars()) * kBarTicks;
    for (auto& nt : merged)
    {
        if (nt.start >= endTick) continue;
        const int bar = int(nt.start / kBarTicks);
        const int64_t end = std::min(std::max(nt.end, nt.start + 10), endTick + 2 * kPPQ);
        byBar_[size_t(bar)].push_back({ nt.start, Event::NoteOn, uint8_t(nt.track), uint8_t(nt.pitch), nt.vel });
        byBar_[size_t(bar)].push_back({ end, Event::NoteOff, uint8_t(nt.track), uint8_t(nt.pitch), 0 });
    }
    for (auto& l : levels_) byBar_[size_t(l.tick / kBarTicks)].push_back(l);
    notes_.clear();
}

void ChoraleComposer::generateBar(int bar, std::vector<Event>& out)
{
    if (bar < 0 || bar >= totalBars()) return;
    out.insert(out.end(), byBar_[size_t(bar)].begin(), byBar_[size_t(bar)].end());
}

std::string ChoraleComposer::describe() const
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double secs = totalBars() * 4 * 60.0 / bpm_;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s minor chorale · %.0f bpm · %d bars · %d:%02d", names[tonicPc_], bpm_, totalBars(), int(secs) / 60, int(secs) % 60);
    return buf;
}

} // namespace liszt
