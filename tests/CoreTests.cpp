// Checks on the composer alone, over many seeds. Run: liszt_tests
#include "Composer.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

using namespace liszt;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; if (failures < 30) { std::printf("FAIL seed %llu: ", (unsigned long long) seed); std::printf(__VA_ARGS__); std::printf("\n"); } } } while (0)

static std::vector<Event> generateAll(Composer& c)
{
    std::vector<Event> ev;
    for (int b = 0; b < c.totalBars(); ++b) c.generateBar(b, ev);
    std::sort(ev.begin(), ev.end());
    return ev;
}

static bool isChordTone(const Composer& c, int bar, int pitch)
{
    auto& m = c.modeAt(bar);
    for (int d : { 0, 2, 4 })
        if ((c.tonicPc() + degreeSemis(m, c.chordAt(bar) + d)) % 12 == pitch % 12) return true;
    return false;
}

int main()
{
    std::set<std::string> descriptions;
    for (uint64_t seed = 1; seed <= 40; ++seed)
    {
        Composer c(seed), again(seed);
        auto ev = generateAll(c);
        auto ev2 = generateAll(again);
        descriptions.insert(c.describe());

        CHECK(ev.size() == ev2.size() && std::equal(ev.begin(), ev.end(), ev2.begin(), [](auto& a, auto& b) {
                  return a.tick == b.tick && a.type == b.type && a.track == b.track && a.pitch == b.pitch && a.value == b.value;
              }), "same seed must give the same concert");

        double minutes = c.totalBars() * 4 / c.bpm();
        CHECK(minutes > 8.5 && minutes < 11.5, "length %.1f min", minutes);

        std::map<std::pair<int, int>, int> sounding; // (track, pitch) -> count
        size_t e = 0;
        for (int bar = 0; bar <= c.totalBars(); ++bar)
        {
            const int64_t barTick = int64_t(bar) * kBarTicks;
            for (; e < ev.size() && ev[e].tick <= barTick; ++e)
            {
                auto& x = ev[e];
                const auto key = std::make_pair(int(x.track), int(x.pitch));
                if (x.type == Event::NoteOn)
                {
                    int b = int(x.tick / kBarTicks);
                    CHECK(sounding[key] == 0, "double trigger %s %d bar %d", kTrackNames[x.track], x.pitch, b);
                    ++sounding[key];
                    CHECK(x.pitch >= Composer::kRange[x.track][0] && x.pitch <= Composer::kRange[x.track][1],
                          "%s pitch %d out of range", kTrackNames[x.track], x.pitch);
                    CHECK(b < c.totalBars() && inMode(c.modeAt(b), c.tonicPc(), x.pitch),
                          "%s pitch %d not in %s at bar %d", kTrackNames[x.track], x.pitch, c.modeAt(b).name, b);
                    CHECK(x.value > 0 && x.value <= 1, "velocity %f", x.value);
                    if (x.track == Bass || x.track == Seq)
                        CHECK(x.tick % kSixteenth == 0, "sequencer note off the grid at tick %lld", (long long) x.tick);
                    if (x.track == Lead && x.tick % (2 * kPPQ) == 0)
                        CHECK(isChordTone(c, b, x.pitch), "lead strong-beat %d not a chord tone at bar %d", x.pitch, b);
                }
                else if (x.type == Event::NoteOff)
                {
                    CHECK(sounding[key] == 1, "note-off without note-on %s %d", kTrackNames[x.track], x.pitch);
                    --sounding[key];
                }
            }
            if (bar == c.totalBars()) break;

            // A-15: the pad is a held layer; it must never fall silent while it is meant to sound.
            int padNotes = 0;
            for (auto& [k, n] : sounding) if (k.first == Pad) padNotes += n;
            if (c.sectionAt(bar).layers & (1 << Pad))
                CHECK(padNotes >= 3, "pad has %d notes at bar %d", padNotes, bar);

            // A-12: the bass spine — a note on every downbeat while the bass plays.
            if (c.sectionAt(bar).layers & (1 << Bass))
                CHECK(std::any_of(ev.begin(), ev.end(), [&](auto& x) { return x.tick == barTick && x.track == Bass && x.type == Event::NoteOn; }),
                      "no bass on the downbeat of bar %d", bar);
        }
        for (; e < ev.size(); ++e)
            if (ev[e].type == Event::NoteOff) --sounding[std::make_pair(int(ev[e].track), int(ev[e].pitch))];
        for (auto& [k, n] : sounding)
            CHECK(n == 0, "stuck note %s %d", kTrackNames[k.first], k.second);
    }
    std::uint64_t seed = 0;
    CHECK(descriptions.size() > 10, "only %zu distinct concerts over 40 seeds", descriptions.size());

    std::printf("%s (%d failures)\n", failures ? "FAILED" : "all checks passed", failures);
    std::printf("e.g. seed 1: %s\n", Composer(1).describe().c_str());
    return failures ? 1 : 0;
}
