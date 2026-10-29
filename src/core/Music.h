#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace liszt {

// Deterministic on every compiler (splitmix64). The std distributions are not:
// the same seed would give a different concert on the Mac than on this PC.
struct Rng
{
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}

    uint64_t next()
    {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
    bool chance(double p) { return uniform() < p; }
    int range(int lo, int hi) { return lo + int(next() % uint64_t(hi - lo + 1)); } // inclusive

    template <class T, size_t N>
    int weighted(const std::array<T, N>& w)
    {
        double total = 0;
        for (auto x : w) total += x;
        double r = uniform() * total;
        for (size_t i = 0; i < N; ++i)
            if ((r -= w[i]) < 0) return int(i);
        return int(N - 1);
    }
};

struct Mode
{
    const char* name;
    std::array<int, 7> steps;
};

inline constexpr Mode kAeolian  { "aeolian",  { 0, 2, 3, 5, 7, 8, 10 } };
inline constexpr Mode kDorian   { "dorian",   { 0, 2, 3, 5, 7, 9, 10 } };
inline constexpr Mode kPhrygian { "phrygian", { 0, 1, 3, 5, 7, 8, 10 } };
// for the genre songs: each its own colour of the dark
inline constexpr Mode kLydian            { "lydian",            { 0, 2, 4, 6, 7, 9, 11 } };
inline constexpr Mode kHarmonicMinor     { "harmonic minor",    { 0, 2, 3, 5, 7, 8, 11 } };
inline constexpr Mode kMelodicMinor      { "melodic minor",     { 0, 2, 3, 5, 7, 9, 11 } };
inline constexpr Mode kPhrygianDominant  { "phrygian dominant", { 0, 1, 4, 5, 7, 8, 10 } };
inline constexpr Mode kHungarianMinor    { "hungarian minor",   { 0, 2, 3, 6, 7, 8, 11 } };

inline int floorDiv(int a, int b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

// Semitones above the tonic of scale degree d. Degrees run past 6 into higher octaves.
inline int degreeSemis(const Mode& m, int d)
{
    int o = floorDiv(d, 7);
    return 12 * o + m.steps[size_t(d - 7 * o)];
}

inline bool inMode(const Mode& m, int tonicPc, int pitch)
{
    int pc = ((pitch - tonicPc) % 12 + 12) % 12;
    for (int s : m.steps)
        if (s == pc) return true;
    return false;
}

inline int foldInto(int pitch, int lo, int hi)
{
    while (pitch < lo) pitch += 12;
    while (pitch > hi) pitch -= 12;
    return pitch;
}

} // namespace liszt
