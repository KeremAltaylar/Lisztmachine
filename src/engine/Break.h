#pragma once
// Copied from BreakMachine (github.com/KeremAltaylar/BreakMachine, src/Break.h at 8047d24): its slicer, classes,
// phrase generator and player, unchanged. Lisztmachine plays it as a sampled instrument (BreakInstrument, Engine.cpp).
// JUCE-free core of BreakMachine: onset slicing, the drum-hit envelope, kick/snare/hat
// classification, the phrase generator and the sample-accurate player. tests/BreakTests.cpp drives it.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace brk {

constexpr double kPi = 3.14159265358979323846;
enum Kind : uint8_t { Kick = 0, Snare = 1, Hat = 2 };
constexpr int kKinds = 3;

struct Slice
{
    int start = 0, len = 0; // samples in the source
    float peak = 0, centroid = 0, low = 0;
    Kind kind = Snare;
    uint8_t src = 0;   // which break of the kit it came from
    double sr = 44100; // that break's sample rate
};

struct Break
{
    double sr = 44100;
    std::vector<float> l, r;
    std::vector<Slice> slices;
    std::array<std::vector<int>, kKinds> byKind;
    std::vector<int> stepMap; // source 16th -> slice starting on it, or -1
    int bars = 1;
    double bpm = 0;
    std::vector<int> partStart { 0 }; // first sample of each break in the kit

    // Rebuild the per-kind lists after a class changes. Capacity is reserved at analysis, so no allocation.
    void index()
    {
        for (auto& v : byKind) v.clear();
        for (int i = 0; i < int(slices.size()); ++i) byKind[slices[i].kind].push_back(i);
    }
};

// ---------------------------------------------------------------- analysis (message thread)

inline void fft(std::vector<std::complex<float>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * kPi / double(len);
        for (size_t k = 0; k < len / 2; ++k)
        {
            const std::complex<float> w(float(std::cos(ang * double(k))), float(std::sin(ang * double(k))));
            for (size_t i = 0; i < n; i += len)
            {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

// Onsets in samples. Log spectral flux (512-point frames, 64 hop) against a moving mean, then each
// detection is pulled back to where the hit really starts: the first-difference signal (which ignores
// the low tail of the hit before it) rises out of the floor there. sens 0..1, higher finds quieter hits.
inline std::vector<int> detectOnsets(const std::vector<float>& x, double sr, float sens, double minGapMs = 35)
{
    constexpr int N = 512, hop = 64;
    const int n = int(x.size());
    const int frames = n / hop + 1;
    std::vector<float> win(N), mag(N / 2 + 1), prev(N / 2 + 1, 0), odf(size_t(frames), 0), hf(size_t(frames), 0);
    for (int i = 0; i < N; ++i) win[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * kPi * i / N));
    std::vector<std::complex<float>> buf(N);
    for (int f = 0; f < frames; ++f)
    {
        const int c = f * hop;
        for (int i = 0; i < N; ++i)
        {
            const int s = c - N / 2 + i;
            buf[size_t(i)] = (s >= 0 && s < n) ? x[size_t(s)] * win[size_t(i)] : 0.0f;
        }
        fft(buf);
        float flux = 0, fh = 0;
        for (int k = 1; k <= N / 2; ++k)
        {
            mag[size_t(k)] = std::log1p(100.0f * std::abs(buf[size_t(k)]) / N);
            flux += std::max(0.0f, mag[size_t(k)] - prev[size_t(k)]);
            if (k * sr / N > 1000) fh += std::max(0.0f, mag[size_t(k)] - prev[size_t(k)]);
        }
        std::swap(mag, prev);
        odf[size_t(f)] = flux;
        hf[size_t(f)] = fh;
    }
    const float mx = *std::max_element(odf.begin(), odf.end());
    if (mx <= 0) return {};
    for (auto& v : odf) v /= mx;

    std::vector<float> d(size_t(n), 0); // first difference = the transient
    for (int i = 1; i < n; ++i) d[size_t(i)] = std::abs(x[size_t(i)] - x[size_t(i - 1)]);

    // A kick is a few low bins and barely moves the flux, and it often lands under a ringing hat.
    // So the low band (< 150 Hz, filtered forwards and back for zero phase) gets its own detector:
    // the rise in energy across each candidate time. Each detection is refined on whichever band found it.
    auto lowBand = [&](double hz) {
        std::vector<float> y(static_cast<size_t>(n));
        const float a = float(1 - std::exp(-2 * kPi * hz / sr));
        float s1 = 0, s2 = 0;
        for (int i = 0; i < n; ++i) { s1 += a * (x[size_t(i)] - s1); s2 += a * (s1 - s2); y[size_t(i)] = s2; }
        s1 = s2 = 0;
        for (int i = n - 1; i >= 0; --i) { s1 += a * (y[size_t(i)] - s1); s2 += a * (s1 - s2); y[size_t(i)] = std::abs(s2); }
        return y;
    };
    const std::vector<float> lp = lowBand(150), lp4 = lowBand(400);
    const float lp4max = *std::max_element(lp4.begin(), lp4.end());
    // Peak follower on the low band (100 ms release): it holds through the wave's own cycles, so only
    // a new hit raises it. A rise of 2x or more in 6 ms counts fully, so a kick under a ringing kick
    // scores like one out of silence.
    std::vector<float> fol(static_cast<size_t>(n));
    {
        const float rel = float(std::exp(-1 / (0.1 * sr)));
        float e = 0, mxl = 0;
        for (int i = 0; i < n; ++i) fol[size_t(i)] = e = std::max(lp[size_t(i)], e * rel), mxl = std::max(mxl, e);
        const float eps = mxl * 0.02f;
        for (auto& v : fol) v += eps;
    }
    const int L = std::max(1, int(0.006 * sr));
    // A struck kick always has some broadband attack; two booming kicks beating against each other
    // swell the low band with none (and stir the low FFT bins). So a low rise only counts where the
    // flux above 1 kHz moves too.
    const float hmx = std::max(1e-9f, *std::max_element(hf.begin(), hf.end()));
    for (auto& v : hf) v /= hmx;
    std::vector<float> low(static_cast<size_t>(frames), 0);
    for (int f = 0; f < frames; ++f)
    {
        const int t0 = f * hop - N / 4, a0 = std::clamp(t0 - 1, 0, n - 1), a1 = std::clamp(t0 + L, 0, n - 1);
        float fl = 0;
        for (int j = std::max(0, f - 4); j <= std::min(frames - 1, f + 4); ++j) fl = std::max(fl, hf[size_t(j)]);
        low[size_t(f)] = std::clamp(float(std::log(fol[size_t(a1)] / fol[size_t(a0)]) / std::log(2.0)), 0.0f, 1.0f) * std::min(1.0f, fl / 0.08f);
        odf[size_t(f)] = std::max(odf[size_t(f)], low[size_t(f)]);
    }

    const float delta = 0.03f + (1.0f - std::clamp(sens, 0.0f, 1.0f)) * 0.22f;
    const int gap = int(minGapMs * sr / 1000), meanW = 24, peakW = 3;
    const int back = int(0.012 * sr), fwd = int(0.012 * sr), hold = std::max(1, int(0.0005 * sr));
    std::vector<int> out;
    for (int f = 0; f < frames; ++f)
    {
        float mean = 0;
        int cnt = 0;
        bool isMax = true;
        for (int j = std::max(0, f - meanW); j <= std::min(frames - 1, f + meanW); ++j)
        {
            mean += odf[size_t(j)], ++cnt;
            if (std::abs(j - f) <= peakW && odf[size_t(j)] > odf[size_t(f)]) isMax = false;
        }
        if (!isMax || odf[size_t(f)] < mean / float(cnt) + delta) continue;

        // The flux peaks ~N/4 after the hit (the Hann slope); search around there for the transient.
        const int t0 = f * hop - N / 4;
        const int lo = std::max(out.empty() ? 0 : out.back() + gap / 2, t0 - back), hi = std::min(n - 1, t0 + fwd);
        if (lo >= hi) continue;
        const auto& sig = low[size_t(f)] >= odf[size_t(f)] ? lp : d;
        int p = lo;
        for (int i = lo; i <= hi; ++i) if (sig[size_t(i)] > sig[size_t(p)]) p = i;
        const float floorLevel = sig[size_t(p)] * 0.12f;
        int s = p, quiet = 0;
        while (s > lo && quiet < hold) { quiet = sig[size_t(s - 1)] < floorLevel ? quiet + 1 : 0; --s; }
        s += quiet; // first sample of the rise
        {
            // The low band, sharpened to 400 Hz (still deaf to snare and hat noise), has the last word on
            // where a hit with a body starts: a kick's body can lead its click by a few ms, and the 150 Hz
            // band is smeared both ways. Start = the first point that climbs 15% of the way from the level
            // already ringing before the window (a kick often lands on the last one's tail) to the new
            // peak, less half a millisecond. Only for hits that add real low energy, and only ever earlier
            // than the transient found above, unless the low band found the hit itself.
            const int w0 = std::max(lo, s - int(0.006 * sr)), w1 = std::min(n - 1, s + int(0.006 * sr));
            float m = 0, base = 0;
            for (int i = w0; i <= w1; ++i) m = std::max(m, lp4[size_t(i)]);
            for (int i = std::max(0, w0 - int(0.01 * sr)); i < w0; ++i) base = std::max(base, lp4[size_t(i)]);
            if (m > 2 * base && m > 0.05f * lp4max)
            {
                const float th = base + 0.15f * (m - base);
                int s2 = w0;
                while (s2 < w1 && lp4[size_t(s2)] < th) ++s2;
                s2 = std::max(w0, s2 - int(0.0005 * sr));
                s = &sig == &lp ? s2 : std::min(s, s2);
            }
        }
        {
            // And the raw level: some kicks open quietly and swell, with no click and little 400 Hz at
            // first. Walk back in 0.25 ms steps while the level stays 10% of the way above what was
            // ringing before (6-16 ms earlier, before any swell) towards this hit's peak.
            const int W = std::max(1, int(0.00025 * sr));
            auto env = [&](int i) { float m = 0; for (int j = std::max(0, i); j < std::min(n, i + W); ++j) m = std::max(m, std::abs(x[size_t(j)])); return m; };
            float P = 0, base = 0;
            for (int i = s; i < std::min(n, s + int(0.01 * sr)); i += W) P = std::max(P, env(i));
            for (int i = std::max(0, s - int(0.016 * sr)); i < s - int(0.006 * sr); i += W) base = std::max(base, env(i));
            if (P > 1.5f * base)
            {
                const float th = base + 0.1f * (P - base);
                int r = s;
                while (r - W >= lo && env(r - W) > th) r -= W;
                s = std::min(s, r);
            }
        }
        for (int k = 1; k < int(0.002 * sr) && s - k > 0; ++k) // back to a zero crossing, if one is near
            if (x[size_t(s - k)] == 0 || (x[size_t(s - k)] >= 0) != (x[size_t(s - k + 1)] >= 0)) { s -= k; break; }
        if (!out.empty() && s - out.back() < gap) continue;
        out.push_back(s);
    }
    return out;
}

// Centroid (Hz) and share of energy below 150 Hz of what the hit ADDS: the spectrum of its first
// 80 ms minus the spectrum of the 80 ms before it, so the previous hit's tail is not counted as this one.
inline void measure(const std::vector<float>& x, double sr, Slice& s)
{
    constexpr int N = 4096;
    const int len = std::min({ s.len, int(0.08 * sr), N });
    std::vector<std::complex<float>> buf(N, 0.0f), before(N, 0.0f);
    float pk = 0;
    for (int i = 0; i < len; ++i)
    {
        const float v = x[size_t(s.start + i)], w = float(0.5 - 0.5 * std::cos(2 * kPi * (i + 0.5) / len));
        pk = std::max(pk, std::abs(v));
        buf[size_t(i)] = v * w;
        if (s.start - len + i >= 0) before[size_t(i)] = x[size_t(s.start - len + i)] * w;
    }
    fft(buf);
    fft(before);
    double num = 0, den = 0, low = 0, all = 0;
    for (int k = 1; k <= N / 2; ++k)
    {
        const double f = k * sr / N, e = std::max(0.0, double(std::norm(buf[size_t(k)])) - double(std::norm(before[size_t(k)]))), m = std::sqrt(e);
        num += f * m, den += m, all += e;
        if (f < 150) low += e;
    }
    s.peak = pk;
    s.centroid = den > 0 ? float(num / den) : 0;
    s.low = all > 0 ? float(low / all) : 0;
}

// Three groups by brightness and low energy, ranked: darkest = kick, brightest = hat.
// Relative to the break itself (k-means), because every break is recorded differently.
inline void classify(std::vector<Slice>& sl)
{
    const int n = int(sl.size());
    if (n == 0) return;
    if (n < 3)
    {
        for (auto& s : sl) s.kind = s.centroid < 400 ? Kick : s.centroid > 3000 ? Hat : Snare;
        return;
    }
    std::vector<double> a(static_cast<size_t>(n)), b(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) a[size_t(i)] = std::log2(std::max(20.0f, sl[size_t(i)].centroid)), b[size_t(i)] = sl[size_t(i)].low;
    auto z = [n](std::vector<double>& v) {
        double m = 0, q = 0;
        for (double x : v) m += x;
        m /= n;
        for (double x : v) q += (x - m) * (x - m);
        const double sd = std::sqrt(q / n) + 1e-9;
        for (double& x : v) x = (x - m) / sd;
    };
    z(a), z(b);
    std::vector<int> order(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) order[size_t(i)] = i;
    auto score = [&](int i) { return a[size_t(i)] - b[size_t(i)]; };
    std::sort(order.begin(), order.end(), [&](int x, int y) { return score(x) < score(y); });
    double ca[3], cb[3];
    for (int k = 0; k < 3; ++k)
    {
        const int i = order[size_t(std::min(n - 1, int((0.15 + 0.35 * k) * n)))];
        ca[k] = a[size_t(i)], cb[k] = b[size_t(i)];
    }
    std::vector<int> lab(static_cast<size_t>(n), 0);
    for (int it = 0; it < 40; ++it)
    {
        for (int i = 0; i < n; ++i)
        {
            double best = 1e30;
            for (int k = 0; k < 3; ++k)
            {
                const double dd = (a[size_t(i)] - ca[k]) * (a[size_t(i)] - ca[k]) + (b[size_t(i)] - cb[k]) * (b[size_t(i)] - cb[k]);
                if (dd < best) best = dd, lab[size_t(i)] = k;
            }
        }
        for (int k = 0; k < 3; ++k)
        {
            double sa = 0, sb = 0;
            int c = 0;
            for (int i = 0; i < n; ++i) if (lab[size_t(i)] == k) sa += a[size_t(i)], sb += b[size_t(i)], ++c;
            if (c) ca[k] = sa / c, cb[k] = sb / c;
        }
    }
    int rank[3] = { 0, 1, 2 };
    std::sort(rank, rank + 3, [&](int x, int y) { return ca[x] - cb[x] < ca[y] - cb[y]; });
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < 3; ++k) if (rank[k] == lab[size_t(i)]) sl[size_t(i)].kind = Kind(k);
}

// Slice, classify, and guess the length in bars: the whole-bar count whose tempo lands nearest 165 BPM.
inline void analyse(Break& b, float sens)
{
    std::vector<float> mono(b.l.size());
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (b.l[i] + b.r[i]);
    const auto on = detectOnsets(mono, b.sr, sens);
    b.slices.clear();
    for (size_t i = 0; i < on.size(); ++i)
    {
        Slice s;
        s.start = on[i];
        s.len = (i + 1 < on.size() ? on[i + 1] : int(mono.size())) - s.start;
        s.sr = b.sr;
        measure(mono, b.sr, s);
        b.slices.push_back(s);
    }
    classify(b.slices);
    for (auto& v : b.byKind) v.reserve(b.slices.size());
    b.index();

    b.stepMap.clear();
    if (b.slices.empty()) return;
    const double dur = double(int(mono.size()) - b.slices[0].start) / b.sr;
    double best = 1e9;
    for (int bars : { 1, 2, 4, 8, 16 })
    {
        const double bpm = bars * 240.0 / dur, dist = std::abs(std::log2(bpm / 165.0));
        if (dist < best) best = dist, b.bars = bars, b.bpm = bpm;
    }
    const int steps = b.bars * 16;
    const double stepLen = dur * b.sr / steps;
    b.stepMap.assign(size_t(steps), -1);
    for (int i = 0; i < int(b.slices.size()); ++i)
    {
        const int st = int(std::lround((b.slices[size_t(i)].start - b.slices[0].start) / stepLen));
        if (st < steps && (b.stepMap[size_t(st)] < 0 || b.slices[size_t(b.stepMap[size_t(st)])].peak < b.slices[size_t(i)].peak))
            b.stepMap[size_t(st)] = i;
    }
}

// Several analysed breaks as one kit: audio end to end, slices and step maps re-indexed, each slice
// tagged with its break. Each break keeps its own classes and its own sample rate.
inline Break combine(const std::vector<const Break*>& parts)
{
    Break k;
    k.bars = 0;
    k.partStart.clear();
    for (const Break* b : parts)
    {
        if (b->slices.empty()) continue;
        const int a = int(k.l.size()), s0 = int(k.slices.size());
        if (k.partStart.empty()) k.sr = b->sr, k.bpm = b->bpm;
        k.partStart.push_back(a);
        k.l.insert(k.l.end(), b->l.begin(), b->l.end());
        k.r.insert(k.r.end(), b->r.begin(), b->r.end());
        for (Slice s : b->slices) s.start += a, s.src = uint8_t(k.partStart.size() - 1), k.slices.push_back(s);
        for (int m : b->stepMap) k.stepMap.push_back(m < 0 ? -1 : m + s0);
        k.bars += b->bars;
    }
    if (k.partStart.empty()) k.partStart.push_back(0), k.bars = 1;
    for (auto& v : k.byKind) v.reserve(k.slices.size());
    k.index();
    return k;
}

// ---------------------------------------------------------------- the drum-hit envelope

struct Env { float attackMs = 1, decayMs = 200, sustain = 0.6f, releaseMs = 30; };
struct EnvS { double a, dTau, s, r; }; // in samples

inline EnvS toSamples(const Env& e, double sr)
{
    return { e.attackMs * sr / 1000, std::max(1.0, e.decayMs * sr / 1000 / 4.6), double(e.sustain), e.releaseMs * sr / 1000 };
}

// Gain at position t of a slice len samples long: linear attack, exponential decay to the sustain
// level, and an exponential release that lands on exactly 0 at the slice's end, the next onset.
inline float envAt(double t, double len, const EnvS& e)
{
    if (t <= 0 || t >= len) return 0;
    const double a = std::min(e.a, len * 0.25);
    double v = t < a ? t / a : e.s + (1 - e.s) * std::exp(-(t - a) / e.dTau);
    const double rs = std::max(a, len - e.r);
    if (t > rs)
    {
        constexpr double k = 5, ek = 0.006737946999085467; // exp(-5)
        v *= (std::exp(-k * (t - rs) / (len - rs)) - ek) / (1 - ek);
    }
    return float(v);
}

// ---------------------------------------------------------------- the generator (audio thread, no allocation)

constexpr int kSteps = 32;           // a pattern is two bars of 16ths
constexpr int16_t kRest = -1, kStop = -2; // rest lets the last hit ring; stop silences everything
struct Step { int16_t slice = kRest; uint8_t rolls = 1; bool rev = false; int8_t semis = 0; float gain = 1; };
using Pattern = std::array<Step, kSteps>;
struct GenParams { float chaos = 0.4f, density = 0.6f, rolls = 0.4f, reverse = 0.15f, pitch = 0.2f; };

struct Rng
{
    uint64_t s = 0x9E3779B97F4A7C15ull;
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    float uni() { return float(next() >> 40) / float(1 << 24); }
    int below(int n) { return n > 0 ? int(next() % uint64_t(n)) : 0; }
    bool chance(float p) { return uni() < p; }
};

// A seed for one place in a song: the same (seed, place) always gives the same numbers.
inline Rng rngAt(uint64_t seed, uint64_t place)
{
    uint64_t x = seed + 0x9E3779B97F4A7C15ull * (place + 1);
    x ^= x >> 30, x *= 0xBF58476D1CE4E5B9ull, x ^= x >> 27, x *= 0x94D049BB133111EBull, x ^= x >> 31;
    Rng r;
    r.s = x ? x : 1;
    return r;
}

inline int pick(const Break& b, Kind k, Rng& r)
{
    const auto& v = b.byKind[k];
    return v.empty() ? kRest : v[size_t(r.below(int(v.size())))];
}
inline bool isKind(const Break& b, int16_t s, Kind k) { return s >= 0 && b.slices[size_t(s)].kind == k; }
inline bool spine(int i) { return i % 16 == 0 || i % 8 == 4; } // kick on 1, snare on 2 and 4

inline void flavour(const Break& b, Step& st, const GenParams& g, Rng& r)
{
    static constexpr int8_t semis[] = { -12, -7, -5, -3, 5, 7, 12 };
    static constexpr uint8_t rolls[] = { 2, 2, 3, 4, 4, 6 };
    if (st.slice < 0) return;
    if (r.chance(g.rolls * (0.05f + 0.3f * g.chaos))) st.rolls = rolls[r.below(6)];
    if (!isKind(b, st.slice, Kick) && r.chance(g.reverse * 0.25f)) st.rev = true;
    if (r.chance(g.pitch * 0.25f)) st.semis = semis[r.below(7)];
}

// Two bars from a random bar of the source, kept in its own groove, then re-hit, re-chopped,
// thickened or thinned, with the spine (kick on 1, snares on 2 and 4) always put back.
inline Pattern makeBase(const Break& b, const GenParams& g, Rng& r)
{
    Pattern p {};
    const int n = int(b.stepMap.size());
    if (n == 0) return p;
    const int off = r.below(b.bars) * 16;
    for (int i = 0; i < kSteps; ++i)
    {
        auto& st = p[size_t(i)];
        st.slice = int16_t(b.stepMap[size_t((off + i) % n)]);
        if (st.slice >= 0 && r.chance(g.chaos * 0.45f)) st.slice = int16_t(pick(b, b.slices[size_t(st.slice)].kind, r));
        if (!spine(i) && r.chance(g.chaos * 0.3f)) st.slice = int16_t(b.stepMap[size_t(r.below(n))]);
    }
    for (int i = 0; i < kSteps; ++i)
    {
        auto& st = p[size_t(i)];
        if (i % 16 == 0 && !isKind(b, st.slice, Kick)) st.slice = int16_t(pick(b, Kick, r));
        else if (i % 8 == 4 && !isKind(b, st.slice, Snare)) st.slice = int16_t(pick(b, Snare, r));
        else if (i % 16 == 10 && st.slice < 0 && r.chance(0.4f + 0.4f * g.density)) st.slice = int16_t(pick(b, Kick, r));
        else if (!spine(i) && st.slice < 0)
        {
            if (i % 2 == 0 && r.chance(g.density * 0.8f)) st.slice = int16_t(pick(b, Hat, r));
            else if (i % 2 == 1 && r.chance(g.density * 0.35f))
            {
                const bool ghost = r.chance(0.35f);
                st.slice = int16_t(pick(b, ghost ? Snare : Hat, r));
                st.gain = ghost ? 0.45f : 0.65f;
            }
        }
        else if (!spine(i) && isKind(b, st.slice, Hat) && r.chance((1 - g.density) * 0.6f)) st.slice = kRest;
        if (!spine(i)) flavour(b, st, g, r);
    }
    return p;
}

// Which break each class plays from: kit[kind] = break index. A hit from another break becomes a hit of
// the same class from the chosen one, always the same one, so the pattern still repeats like itself.
using Kit = std::array<uint8_t, kKinds>;
inline int16_t toKit(const Break& b, int16_t s, const Kit& kit)
{
    if (s < 0) return s;
    const Slice& x = b.slices[size_t(s)];
    const int to = kit[x.kind];
    if (x.src == to) return s;
    int rank = 0, count = 0;
    for (int i = 0; i < int(b.slices.size()); ++i)
    {
        const Slice& y = b.slices[size_t(i)];
        if (y.kind != x.kind) continue;
        if (y.src == x.src && i < s) ++rank;
        if (y.src == to) ++count;
    }
    if (!count) return s;
    int want = rank % count;
    for (int i = 0; i < int(b.slices.size()); ++i)
        if (b.slices[size_t(i)].kind == x.kind && b.slices[size_t(i)].src == to && want-- == 0) return int16_t(i);
    return s;
}

// The phrase is A A' A FILL (two bars each). A' re-chops its second bar; the fill rolls the snare up
// into a silence before the next downbeat, because the silence is what makes the downbeat land.
inline Pattern variation(const Break& b, const Pattern& base, int pos, const GenParams& g, Rng& r, int fillFrom = -1)
{
    Pattern p = base;
    if (pos == 0 || pos == 2) return p;
    const int ops = 1 + int(g.chaos * 6);
    for (int k = 0; k < ops; ++k)
    {
        const int i = 17 + r.below(15), j = 17 + r.below(15);
        if (spine(i)) continue;
        if (r.chance(0.5f)) { if (!spine(j)) std::swap(p[size_t(i)], p[size_t(j)]); }
        else { p[size_t(i)] = Step {}; p[size_t(i)].slice = int16_t(pick(b, r.chance(0.5f) ? Snare : Hat, r)); flavour(b, p[size_t(i)], g, r); }
    }
    for (int i = 16; i < kSteps; ++i)
        if (spine(i) && p[size_t(i)].slice >= 0) p[size_t(i)].rolls = 1, p[size_t(i)].rev = false; // the spine stays plain
    if (pos == 3)
    {
        const int16_t sn = int16_t(pick(b, Snare, r));
        const int from = fillFrom >= 0 ? fillFrom : g.chaos > 0.6f ? 20 : 24;
        for (int i = from; i < kSteps; ++i)
        {
            if (!r.chance(0.35f + 0.55f * g.chaos) && i % 4 != 0) continue;
            auto& st = p[size_t(i)];
            st = Step {};
            st.slice = sn;
            st.rolls = g.rolls < 0.15f ? 1 : uint8_t(i < 26 ? 2 : i < 29 ? 3 : (g.rolls > 0.6f ? 6 : 4));
            st.gain = 0.55f + 0.45f * float(i - from) / float(kSteps - from);
        }
        if (r.chance(0.4f + 0.4f * g.chaos))
        {
            p[31].slice = kStop;
            if (r.chance(g.chaos * 0.6f)) p[30].slice = kStop;
        }
    }
    return p;
}

// ---------------------------------------------------------------- the player (audio thread)

class Player
{
public:
    static constexpr int kVoices = 8, kTicks = 12; // 12 ticks per 16th: rolls of 2, 3, 4 and 6 land on ticks
    GenParams gen;
    Env env;
    float gainTarget = 1;
    int evolve = 1;            // phrases (8 bars) between new bases; 0 = hold the current one
    // Form: Loop is the 8-bar phrase above. Song is 4 parts of 32 bars, looping every 128; Endless is the same
    // parts, a new one every 32 bars forever. In both, each part replays its own pattern and its last bar is
    // the attack, a whole-bar roll into the next part. Both are tied to the bar number and the song seed, so
    // starting at bar 65 always plays part 3, and the same seed is the same song.
    enum Form { Loop, Song, Endless };
    int form = Loop;
    uint64_t songSeed = 1;
    static constexpr int kPartSegs = 16, kSongParts = 4; // 16 two-bar patterns = 32 bars
    std::vector<int64_t>* log = nullptr; // tests only: absolute sample of every trigger

    // What the UI shows. Written by render(); read through the processor.
    Pattern pattern {};
    int64_t step = -1, seg = -1;
    int lastSlice = -1;
    bool patternChanged = false;
    Kit kit {}; // the break each class plays from, when the kit has more than one

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        limRel = float(1 - std::exp(-1 / (0.08 * sr)));
        for (auto& v : voices) v.slice = -1;
        lastTick = INT64_MIN;
    }
    void setBreak(const Break* b) { brk = b; for (auto& v : voices) v.slice = -1; haveBase = false; seg = -1; }
    void requestNew() { newBase = true; }
    void seed(uint64_t s) { rng.s = s ? s : 1; }

    // Adds nothing, writes n samples. ppq0 = quarter-note position of the first sample.
    void render(float* L, float* R, int n, double ppq0, double ppqPerSample, bool playing)
    {
        if (!playing && wasPlaying) chokeAll();
        if (!playing) lastTick = INT64_MIN;
        wasPlaying = playing;
        const float g0 = gain, dg = (gainTarget - gain) / float(std::max(1, n));
        for (int i = 0; i < n; ++i)
        {
            if (playing && brk)
            {
                const int64_t t = int64_t(std::floor((ppq0 + i * ppqPerSample) * 4 * kTicks + 1e-9));
                if (t != lastTick) { lastTick = t; at = clock + i; onTick(t); }
            }
            float l = 0, r = 0;
            for (auto& v : voices) if (v.slice >= 0) voice(v, l, r);
            const float gg = g0 + dg * float(i);
            l *= gg, r *= gg;
            // Peak limiter at -0.3 dBFS: instant down, 80 ms back up.
            const float pk = std::max(std::abs(l), std::abs(r)) * lim;
            if (pk > 0.966f) lim *= 0.966f / pk;
            else lim += (1 - lim) * limRel;
            L[i] = l * lim, R[i] = r * lim;
        }
        gain = gainTarget;
        clock += n;
    }

private:
    struct Voice { int slice = -1; double pos = 0, rate = 1; bool rev = false; float gain = 0, fade = 1; bool dying = false; EnvS e {}; };
    std::array<Voice, kVoices> voices {};
    const Break* brk = nullptr;
    double sr = 44100;
    int64_t lastTick = INT64_MIN, clock = 0, at = 0;
    bool wasPlaying = false, haveBase = false, newBase = false;
    float gain = 1, lim = 1, limRel = 0.0003f;
    Pattern base {};
    Rng rng;

    void chokeAll() { for (auto& v : voices) if (v.slice >= 0) v.dying = true; }
    int64_t part = -1;

    void newKit(Rng& r)
    {
        base = makeBase(*brk, gen, r), haveBase = true, newBase = false;
        if (brk->partStart.size() > 1) // a new pattern, a new kit: each class from any of the breaks
            for (auto& k : kit) k = uint8_t(r.below(int(brk->partStart.size())));
    }

    void onTick(int64_t t)
    {
        if (t < 0) return;
        const int64_t st = t / kTicks;
        const int sub = int(t % kTicks);
        if (st / kSteps != seg)
        {
            seg = st / kSteps;
            if (form == Loop)
            {
                const int pos = int(seg % 4);
                if (!haveBase || newBase || (evolve > 0 && pos == 0 && (seg / 4) % evolve == 0))
                    newKit(rng);
                pattern = variation(*brk, base, pos, gen, rng);
            }
            else
            {
                const int64_t p = seg / kPartSegs, inPart = seg % kPartSegs;
                const int64_t partNo = form == Song ? p % kSongParts : p;
                if (!haveBase || newBase || partNo != part)
                {
                    Rng pr = rngAt(songSeed, uint64_t(partNo));
                    newKit(pr);
                    part = partNo;
                }
                Rng vr = rngAt(songSeed ^ 0x5bd1e995ull, uint64_t(form == Song ? seg % (kPartSegs * kSongParts) : seg));
                const bool attack = inPart == kPartSegs - 1;
                pattern = variation(*brk, base, attack ? 3 : int(inPart % 2), gen, vr, attack ? 17 : -1); // the roll fills the bar after its downbeat kick
            }
            if (brk->partStart.size() > 1) for (auto& h : pattern) h.slice = toKit(*brk, h.slice, kit);
            patternChanged = true;
        }
        step = st;
        const Step& s = pattern[size_t(st % kSteps)];
        if (s.slice == kStop && sub == 0) chokeAll();
        if (s.slice < 0 || s.slice >= int(brk->slices.size())) return;
        const int every = kTicks / std::max<int>(1, s.rolls);
        if (sub % every) return;
        const int k = sub / every;
        trigger(s.slice, s.rev, s.semis, s.rolls > 1 ? s.gain * (0.55f + 0.45f * float(k + 1) / float(s.rolls)) : s.gain);
    }

    void trigger(int slice, bool rev, int semis, float g)
    {
        if (log) log->push_back(at);
        chokeAll(); // a chopped break is one drummer: each hit cuts the last, through a 3 ms fade
        Voice* v = &voices[0];
        for (auto& c : voices)
        {
            if (c.slice < 0) { v = &c; break; }
            if (c.fade < v->fade) v = &c;
        }
        *v = Voice {};
        v->slice = slice, v->rev = rev, v->gain = g;
        const double ssr = brk->slices[size_t(slice)].sr;
        v->rate = std::pow(2.0, semis / 12.0) * ssr / sr;
        v->e = toSamples(env, ssr);
        lastSlice = slice;
    }

    void voice(Voice& v, float& l, float& r)
    {
        const Slice& s = brk->slices[size_t(v.slice)];
        const double src = v.rev ? s.len - 1 - v.pos : v.pos;
        if (src < 0 || src >= s.len - 1 || v.fade <= 0) { v.slice = -1; return; }
        const int i = int(src);
        const float fr = float(src - i);
        // ponytail: linear interpolation; fine at pitch 0 (exact), a little dull when pitched up. Use a polyphase resampler if heard.
        const size_t a = size_t(s.start + i), b2 = std::min(a + 1, brk->l.size() - 1);
        const float e = envAt(src, s.len - 1, v.e) * v.gain * v.fade;
        l += (brk->l[a] + (brk->l[b2] - brk->l[a]) * fr) * e;
        r += (brk->r[a] + (brk->r[b2] - brk->r[a]) * fr) * e;
        v.pos += v.rate;
        if (v.dying) v.fade -= float(1000.0 / (3.0 * sr));
    }
};

} // namespace brk
