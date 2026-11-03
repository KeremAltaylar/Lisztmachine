// Checks on the composer alone, over many seeds. Run: liszt_tests
#include "Composer.h"
#include "Genres.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <cstdio>
#include <cstring>
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

static bool isChordTone(const BerlinComposer& c, int bar, int pitch)
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
        BerlinComposer c(seed), again(seed);
        auto ev = generateAll(c);
        auto ev2 = generateAll(again);
        descriptions.insert(c.describe());

        CHECK(ev.size() == ev2.size() && std::equal(ev.begin(), ev.end(), ev2.begin(), [](auto& a, auto& b) {
                  return a.tick == b.tick && a.type == b.type && a.track == b.track && a.pitch == b.pitch && a.value == b.value;
              }), "same seed must give the same concert");

        double minutes = c.totalBars() * 4 / c.bpm();
        CHECK(minutes > 5.5 && minutes < 7.5, "length %.1f min", minutes);

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
                    CHECK(x.pitch >= BerlinComposer::kRange[x.track][0] && x.pitch <= BerlinComposer::kRange[x.track][1],
                          "%s pitch %d out of range", kTrackNames[x.track], x.pitch);
                    CHECK(b < c.totalBars() && inMode(c.modeAt(b), c.tonicPc(), x.pitch),
                          "%s pitch %d not in %s at bar %d", kTrackNames[x.track], x.pitch, c.modeAt(b).name, b);
                    CHECK(x.value > 0 && x.value <= 1, "velocity %f", x.value);
                    CHECK(!(c.silenceFrom(b) >= 0 && x.tick >= c.silenceFrom(b)),
                          "%s note %d in a silence at bar %d", kTrackNames[x.track], x.pitch, b);
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

    // ---- the genre songs
    for (auto* spec : GenreComposer::all())
    {
        using G = GenreComposer;
        for (auto* p : { &spec->pulse, &spec->metal, &spec->tick, &spec->perc, &spec->bass, &spec->chord, &spec->sub, &spec->ground, &spec->body, &spec->mallet, &spec->texture })
            for (auto& t : p->tier)
                CHECK(std::strcmp(t.s, "euclid") == 0 || std::strlen(t.s) % size_t(t.perBar) == 0, "%s: pattern \"%s\" is not whole bars of %d", spec->scene, t.s, t.perBar);
        for (seed = 1; seed <= 12; ++seed)
        {
            G c(*spec, seed), again(*spec, seed);
            auto ev = generateAll(c);
            auto ev2 = generateAll(again);
            CHECK(ev.size() == ev2.size() && std::equal(ev.begin(), ev.end(), ev2.begin(), [](auto& a, auto& b) {
                      return a.tick == b.tick && a.type == b.type && a.track == b.track && a.pitch == b.pitch && a.value == b.value;
                  }), "%s: same seed must give the same piece", spec->scene);
            const double minutes = c.totalBars() * 4 / c.bpm();
            CHECK(minutes > 4 && minutes < 8, "%s length %.1f min (the concert is 45)", spec->scene, minutes);
            std::map<std::pair<int, int>, int> held;
            int notes[G::kGenreTracks] = {}, slides = 0, subHeld = 0;
            // a walk's approach and a solo's passing note may leave the key: by a half step into the track's next note
            std::vector<int> nextOn(ev.size(), -1);
            { int last[G::kGenreTracks]; std::fill(std::begin(last), std::end(last), -1);
              for (size_t i = ev.size(); i-- > 0;) if (ev[i].type == Event::NoteOn) { nextOn[i] = last[ev[i].track]; last[ev[i].track] = ev[i].pitch; } }
            for (size_t i = 0; i < ev.size(); ++i)
            {
                auto& x = ev[i];
                const auto key = std::make_pair(int(x.track), int(x.pitch));
                if (x.track == G::Sub && x.type == Event::NoteOn && subHeld++ > 0) ++slides; // the 808 starts a note while one holds
                if (x.track == G::Sub && x.type == Event::NoteOff) --subHeld;
                if (x.type == Event::NoteOn)
                {
                    CHECK(held[key] == 0, "%s double trigger track %d pitch %d at tick %lld", spec->scene, x.track, x.pitch, (long long) x.tick);
                    CHECK(x.value > 0 && x.value <= 1, "%s velocity %f", spec->scene, x.value);
                    const auto r = c.range(x.track);
                    CHECK(x.pitch >= r[0] && x.pitch <= r[1], "%s track %d pitch %d out of range", spec->scene, x.track, x.pitch);
                    const bool chromatic = ((x.track == G::Bass && spec->walk) || (x.track == G::Arp && spec->solo)) && nextOn[i] >= 0 && std::abs(nextOn[i] - x.pitch) == 1;
                    if (!(x.track == G::Tick && x.pitch >= 96) && !chromatic) // noise ticks have no pitch
                        CHECK(inMode(*spec->mode, c.tonicAt(int(x.tick / kBarTicks)), x.pitch), "%s track %d pitch %d outside the key", spec->scene, x.track, x.pitch);
                    ++held[key];
                    ++notes[x.track];
                }
                else if (x.type == Event::NoteOff) { CHECK(held[key] == 1, "%s note-off without note-on track %d pitch %d", spec->scene, x.track, x.pitch); --held[key]; }
            }
            for (auto& [k, n] : held) CHECK(n == 0, "%s stuck note track %d pitch %d", spec->scene, k.first, k.second);
            CHECK(slides > 10, "%s: the 808 never slides (%d)", spec->scene, slides);
            CHECK(notes[G::Harm] > 20, "%s: the choir never sings", spec->scene);
            // the fills throw their last hit into the echo
            CHECK(std::count_if(ev.begin(), ev.end(), [](auto& x) { return x.type == Event::Throw && x.value > 0; }) > 10, "%s: no echo throws", spec->scene);
            // the summit plays the home progression, four bars a chord (a new theme brings its own)
            for (auto& s : c.sections())
                if (s.restoreTheme && spec->summit != G::Summit::NewTheme)
                    for (int b = 0; b < s.bars; ++b)
                        CHECK(c.rootAt(s.startBar + b) == c.homeProgression()[size_t((b / 4) % 4)], "%s: the summit leaves the home progression at bar %d", spec->scene, s.startBar + b);
            // every track the form asks for actually plays
            for (int t = 0; t < G::kGenreTracks; ++t)
            {
                bool used = false;
                for (auto& s : c.sections()) used = used || (s.layers & (1 << t));
                if (used && t != G::Metal) CHECK(notes[t] > 0, "%s: track %d never plays", spec->scene, t);
            }
        }
        std::printf("      %s\n", G(*spec, 1).describe().c_str());
    }
    seed = 0;

    std::printf("%s (%d failures)\n", failures ? "FAILED" : "all checks passed", failures);
    std::printf("e.g. seed 1: %s\n", BerlinComposer(1).describe().c_str());
    return failures ? 1 : 0;
}
