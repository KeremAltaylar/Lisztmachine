// Checks on the composer alone, over many seeds. Run: liszt_tests
#include "Composer.h"
#include "Chorale.h"
#include "Yours.h"
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
                    CHECK(x.pitch >= BerlinComposer::kRange[x.track][0] && x.pitch <= BerlinComposer::kRange[x.track][1],
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

    // ---- the chorale scene
    double stepwise = 0, steps = 0;
    int parallels = 0, chordsChecked = 0;
    for (seed = 1; seed <= 24; ++seed)
    {
        ChoraleComposer c(seed), again(seed);
        auto ev = generateAll(c);
        auto ev2 = generateAll(again);
        CHECK(ev.size() == ev2.size() && std::equal(ev.begin(), ev.end(), ev2.begin(), [](auto& a, auto& b) {
                  return a.tick == b.tick && a.type == b.type && a.track == b.track && a.pitch == b.pitch && a.value == b.value;
              }), "chorale: same seed must give the same piece");
        const double minutes = c.totalBars() * 4 / c.bpm();
        CHECK(minutes > 9.0 && minutes < 11.0, "chorale length %.1f min", minutes);

        // no key struck twice while held, none left hanging, velocities sane
        std::map<std::pair<int, int>, int> held;
        for (auto& x : ev)
        {
            const auto key = std::make_pair(int(x.track), int(x.pitch));
            if (x.type == Event::NoteOn)
            {
                CHECK(held[key] == 0, "chorale double trigger %s %d at tick %lld", kTrackNames[x.track], x.pitch, (long long) x.tick);
                CHECK(x.value > 0 && x.value <= 1, "chorale velocity %f", x.value);
                ++held[key];
            }
            else if (x.type == Event::NoteOff) { CHECK(held[key] == 1, "chorale note-off without note-on"); --held[key]; }
        }
        for (auto& [k, n] : held) CHECK(n == 0, "chorale stuck note %s %d", kTrackNames[k.first], k.second);

        // the four voices: ranges, complete chords, no parallel fifths or octaves, cadences home
        const auto& ch = c.chords();
        const auto& vo = c.voicings();
        for (size_t k = 0; k < ch.size(); ++k)
        {
            const auto pcs = c.chordPcs(ch[k]);
            for (int v = 0; v < 4; ++v)
                CHECK(vo[k][size_t(v)] >= ChoraleComposer::kVoiceRange[v][0] && vo[k][size_t(v)] <= ChoraleComposer::kVoiceRange[v][1],
                      "chorale voice %d pitch %d out of range at chord %zu", v, vo[k][size_t(v)], k);
            CHECK(vo[k][0] < vo[k][1] && vo[k][1] < vo[k][2] && vo[k][2] < vo[k][3], "chorale voices cross at chord %zu", k);
            bool third = false;
            for (int v = 0; v < 4; ++v) third = third || (vo[k][size_t(v)] % 12 == pcs[1]);
            CHECK(third, "chorale chord %zu has no third", k);
            if (ch[k].cadence == ChoraleComposer::Authentic)
                CHECK(ch[k].root == 0 && vo[k][3] % 12 == c.tonicPc(), "authentic cadence at chord %zu not home (soprano %d)", k, vo[k][3]);
            if (k == 0) continue;
            ++chordsChecked;
            for (int i = 0; i < 4; ++i)
                for (int j = i + 1; j < 4; ++j)
                {
                    const int before = ((vo[k - 1][size_t(j)] - vo[k - 1][size_t(i)]) % 12 + 12) % 12;
                    const int after = ((vo[k][size_t(j)] - vo[k][size_t(i)]) % 12 + 12) % 12;
                    const int mi = vo[k][size_t(i)] - vo[k - 1][size_t(i)], mj = vo[k][size_t(j)] - vo[k - 1][size_t(j)];
                    if (mi != 0 && mj != 0 && (mi > 0) == (mj > 0) && before == after && (after == 7 || after == 0))
                    {
                        if (++parallels <= 12 && std::getenv("LISZT_DEBUG"))
                            std::printf("  parallel %s voices %d-%d  chord %zu: root %d q %d inv %d pedal %d -> root %d q %d inv %d pedal %d  [%d %d %d %d] -> [%d %d %d %d]\n",
                                        after == 7 ? "5th" : "8ve", i, j, k, ch[k - 1].root, ch[k - 1].quality, ch[k - 1].inversion, ch[k - 1].pedal,
                                        ch[k].root, ch[k].quality, ch[k].inversion, ch[k].pedal, vo[k - 1][0], vo[k - 1][1], vo[k - 1][2], vo[k - 1][3], vo[k][0], vo[k][1], vo[k][2], vo[k][3]);
                    }
                }
            const int d = std::abs(vo[k][3] - vo[k - 1][3]);
            CHECK(d <= 12, "chorale melody leaps %d at chord %zu", d, k);
            steps += 1; stepwise += d <= 2 ? 1 : 0;
        }

        // the rhythm keeps time: an impulse on every beat while the micro percussion plays (the bar
        // before a new section may drop its last beat), a deep pulse every other downbeat with the drums
        std::set<std::pair<int, int64_t>> onsets;
        for (auto& x : ev) if (x.type == Event::NoteOn) onsets.insert({ int(x.track), x.tick });
        int beats = 0, missing = 0;
        for (int bar = 0; bar < c.totalBars(); ++bar)
        {
            const auto& sec = c.sectionAt(bar);
            const int64_t t0 = int64_t(bar) * kBarTicks;
            using C = ChoraleComposer;
            if (sec.layers & (1 << C::Micro))
                for (int b = 0; b < 4; ++b)
                {
                    ++beats;
                    if (!onsets.count({ int(C::Micro), t0 + b * kPPQ })) ++missing;
                }
            if ((sec.layers & (1 << C::Drums)) && (bar - sec.startBar) % 2 == 0)
                CHECK(onsets.count({ int(C::Drums), t0 }), "chorale: no pulse on the downbeat of bar %d", bar);
        }
        CHECK(missing <= beats / 40, "chorale: %d of %d beats without a click", missing, beats);
    }
    seed = 0;
    CHECK(parallels == 0, "chorale: %d parallel fifths or octaves in %d chord changes", parallels, chordsChecked);
    CHECK(stepwise / steps > 0.5, "chorale melody only %.0f%% stepwise", 100 * stepwise / steps);
    std::printf("chorale: %d chord changes, %d parallels, melody %.0f%% stepwise\n", chordsChecked, parallels, 100 * stepwise / steps);

    // ---- Let All Be Yours
    for (seed = 1; seed <= 24; ++seed)
    {
        using Y = YoursComposer;
        Y c(seed), again(seed);
        auto ev = generateAll(c);
        auto ev2 = generateAll(again);
        CHECK(ev.size() == ev2.size() && std::equal(ev.begin(), ev.end(), ev2.begin(), [](auto& a, auto& b) {
                  return a.tick == b.tick && a.type == b.type && a.track == b.track && a.pitch == b.pitch && a.value == b.value;
              }), "yours: same seed must give the same piece");
        const double minutes = c.totalBars() * 4 / c.bpm();
        CHECK(minutes > 8.5 && minutes < 10.5, "yours length %.1f min", minutes);
        std::map<std::pair<int, int>, int> held;
        std::set<std::pair<int, int64_t>> onsets;
        for (auto& x : ev)
        {
            const auto key = std::make_pair(int(x.track), int(x.pitch));
            if (x.type == Event::NoteOn)
            {
                CHECK(held[key] == 0, "yours double trigger track %d pitch %d at tick %lld", x.track, x.pitch, (long long) x.tick);
                CHECK(x.value > 0 && x.value <= 1, "yours velocity %f", x.value);
                CHECK(x.pitch >= Y::kRange[x.track][0] && x.pitch <= Y::kRange[x.track][1], "yours track %d pitch %d out of range", x.track, x.pitch);
                // everything pitched stays in the tonic's phrygian: no note outside the key
                if (x.track != Y::Hats || x.pitch < 96) // the ticks and the iron are tuned too, to the tonic and its fifth
                    CHECK(inMode(kPhrygian, c.tonicPc(), x.pitch), "yours track %d pitch %d outside the key", x.track, x.pitch);
                ++held[key];
                onsets.insert({ int(x.track), x.tick });
            }
            else if (x.type == Event::NoteOff) { CHECK(held[key] == 1, "yours note-off without note-on track %d pitch %d", x.track, x.pitch); --held[key]; }
        }
        for (auto& [k, n] : held) CHECK(n == 0, "yours stuck note track %d pitch %d", k.first, k.second);
        // four on the floor: a kick on every beat wherever the kick plays (drop bars excepted)
        int beats = 0, missing = 0;
        for (int bar = 0; bar < c.totalBars(); ++bar)
            if (c.active(Y::Kick, bar))
                for (int b = 0; b < 4; ++b) { ++beats; missing += !onsets.count({ int(Y::Kick), int64_t(bar) * kBarTicks + b * kPPQ }); }
        CHECK(beats > 600 && missing <= beats / 60, "yours: %d of %d beats without a kick", missing, beats);
    }
    seed = 0;

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
            CHECK(minutes > 5.5 && minutes < 11, "%s length %.1f min", spec->scene, minutes);
            std::map<std::pair<int, int>, int> held;
            int notes[G::kGenreTracks] = {}, slides = 0, subHeld = 0;
            for (auto& x : ev)
            {
                const auto key = std::make_pair(int(x.track), int(x.pitch));
                if (x.track == G::Sub && x.type == Event::NoteOn && subHeld++ > 0) ++slides; // the 808 starts a note while one holds
                if (x.track == G::Sub && x.type == Event::NoteOff) --subHeld;
                if (x.type == Event::NoteOn)
                {
                    CHECK(held[key] == 0, "%s double trigger track %d pitch %d at tick %lld", spec->scene, x.track, x.pitch, (long long) x.tick);
                    CHECK(x.value > 0 && x.value <= 1, "%s velocity %f", spec->scene, x.value);
                    const auto r = c.range(x.track);
                    CHECK(x.pitch >= r[0] && x.pitch <= r[1], "%s track %d pitch %d out of range", spec->scene, x.track, x.pitch);
                    if (!(x.track == G::Tick && x.pitch >= 96)) // noise ticks have no pitch
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
    std::printf("      chorale: %s\n", ChoraleComposer(1).describe().c_str());
    std::printf("      let all be yours: %s\n", YoursComposer(1).describe().c_str());
    return failures ? 1 : 0;
}
