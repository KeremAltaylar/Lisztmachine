// Checks on the composer alone, over many seeds. Run: liszt_tests
#include "Composer.h"
#include "Genres.h"
#include "Improv.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>

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
        CHECK(minutes > 4.5 && minutes < 7.5, "length %.1f min", minutes);

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
                    CHECK(!(c.silenceFrom(b) >= 0 && x.tick >= c.silenceFrom(b) && !c.keeps(b, x.track)),
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
    std::vector<const GenreComposer::Spec*> genreSongs = GenreComposer::all();
    genreSongs.insert(genreSongs.end(), GenreComposer::extra().begin(), GenreComposer::extra().end());
    for (auto* spec : genreSongs)
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
            CHECK(minutes > 3.5 && minutes < 6, "%s length %.1f min (the concert is about 43)", spec->scene, minutes);
            std::map<std::pair<int, int>, int> held;
            int notes[G::kGenreTracks] = {}, slides = 0, subHeld = 0;
            // a walk's approach may leave the key: by a half step into the track's next note
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
                    const bool chromatic = (x.track == G::Bass && spec->walk) && nextOn[i] >= 0 && std::abs(nextOn[i] - x.pitch) == 1;
                    const bool kitDrum = spec->kit && (x.track == G::Pulse || x.track == G::Body || x.track == G::Tick); // a drum kit's notes are keys, not pitches
                    if (!(x.track == G::Tick && x.pitch >= 96) && !chromatic && !kitDrum) // noise ticks have no pitch
                        CHECK(inMode(c.modeAt(int(x.tick / kBarTicks)), c.tonicAt(int(x.tick / kBarTicks)), x.pitch), "%s track %d pitch %d outside the key at bar %d tick %d next %d", spec->scene, x.track, x.pitch, int(x.tick / kBarTicks), int(x.tick % kBarTicks), nextOn[i]);
                    ++held[key];
                    ++notes[x.track];
                }
                else if (x.type == Event::NoteOff) { CHECK(held[key] == 1, "%s note-off without note-on track %d pitch %d", spec->scene, x.track, x.pitch); --held[key]; }
            }
            for (auto& [k, n] : held) CHECK(n == 0, "%s stuck note track %d pitch %d", spec->scene, k.first, k.second);
            // the soloist plays every bar from the fourth but a hole's, and walks through some part changes (the rises, the level ones)
            {
                std::vector<int> lead(size_t(c.totalBars()), 0), run(size_t(c.totalBars()), 0);
                for (auto& x : ev)
                    if (x.type == Event::NoteOn && x.track == spec->lead)
                    {
                        ++lead[size_t(x.tick / kBarTicks)];
                        if (x.tick % kBarTicks >= 3 * kPPQ) ++run[size_t(x.tick / kBarTicks)];
                    }
                int silent = 0, walks = 0;
                for (int b = 4; b < c.totalBars(); ++b) silent += lead[size_t(b)] == 0;
                for (auto& s : c.sections()) if (s.startBar > 4) walks += run[size_t(s.startBar - 1)] > 0;
                CHECK(silent <= 1, "%s: the soloist is silent in %d bars", spec->scene, silent);
                CHECK(walks >= 1, "%s: the soloist never walks into a part", spec->scene);
                // the low hits: about one a bar where they play, never crowded
                int booms = 0;
                for (auto& x : ev) booms += x.type == Event::NoteOn && x.track == G::Boom;
                CHECK(booms > c.totalBars() / 4 && booms < c.totalBars() * 2, "%s: %d low hits in %d bars", spec->scene, booms, c.totalBars());
            }
            CHECK(slides > 10, "%s: the 808 never slides (%d)", spec->scene, slides);
            CHECK(notes[G::Harm] > 20, "%s: the choir never sings", spec->scene);
            // the fills throw their last hit into the echo
            const auto throws = std::count_if(ev.begin(), ev.end(), [](auto& x) { return x.type == Event::Throw && x.value > 0; });
            CHECK(throws > c.totalBars() / 16, "%s: %d echo throws in %d bars", spec->scene, int(throws), c.totalBars()); // one a 16-bar phrase
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
    // Improvisation: a chain of songs, each at most five minutes, each different from the one before in every
    // dimension that is measured, and the next drone inside the last song's final chord
    std::array<int, 4> bands {};
    std::array<int, 6> shapes {};
    std::array<int, 5> moves {};
    int solos = 0, drifts = 0, halves = 0, crosses = 0, memories = 0, songs = 0;
    for (seed = 1; seed <= 6; ++seed)
        for (int k = 0; k < 12; ++k)
        {
            const auto& s = improvSong(seed, k);
            ++songs; ++bands[size_t(s.band)]; ++shapes[size_t(s.shape)]; ++moves[size_t(s.harmonyMove)];
            solos += s.solo > 0; drifts += s.drift; halves += s.half; crosses += s.cross; memories += s.memory;
            auto c = makeComposer("improv/" + std::to_string(k), seed);
            auto ev = generateAll(*c);
            const double secs = c->totalBars() * 240.0 / c->bpm();
            CHECK(secs <= 295 && secs >= 150, "improv %d: %.0f s", k, secs);
            CHECK(!ev.empty(), "improv %d: silent", k);
            CHECK(c->sections().front().name == std::string("Seed") && c->sections().back().name == std::string("Release"), "improv %d: no Seed/Release", k);
            if (k > 0)
            {
                for (auto& d : s.dims)
                    if (d.first != "length" && d.first != "space" && d.first != "character") CHECK(d.second > 0, "improv %d: %s same as the song before", k, d.first.c_str());
                CHECK(s.distance >= 0.75, "improv %d: distance %.2f", k, s.distance);
            }
            {   // every borrowed instrument is a real one: no silent placeholder (internal/none) but the ghost pulse
                std::string root = __FILE__;
                root = root.substr(0, root.find_last_of("/\\"));
                root = root.substr(0, root.find_last_of("/\\"));
                const auto pal = improvPalette(s, [&](const std::string& sc) {
                    std::string t;
                    if (FILE* f = std::fopen((root + "/sounds/" + sc + ".txt").c_str(), "rb")) { char b[4096]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) t.append(b, n); std::fclose(f); }
                    return t;
                });
                size_t at = 0;
                while ((at = pal.find("internal/none", at)) != std::string::npos)
                {
                    const size_t line = pal.rfind('\n', at) + 1;
                    const std::string track = pal.substr(line, pal.find(' ', line) - line);
                    CHECK(track == "pulse", "improv %d: %s borrows a silent placeholder", k, track.c_str());
                    ++at;
                }
                CHECK(pal.find("# drone from") != std::string::npos, "improv %d: palette not read", k);
                // the character: an electronic song takes nothing from the sampled pool
                if (s.character == 2) CHECK(pal.find("from the pool") == std::string::npos, "improv %d: electronic song with a pool sample", k);
                // on a machine without the sampled instruments (Kerem's Mac before installing them): every track still has a sound
                const auto bare = improvPalette(s, [&](const std::string& sc) {
                    std::string t;
                    if (FILE* f = std::fopen((root + "/sounds/" + sc + ".txt").c_str(), "rb")) { char b[4096]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) t.append(b, n); std::fclose(f); }
                    return t;
                }, [](const std::string& p) { return p.rfind("sfz/", 0) != 0; });
                for (auto* name : { "pad", "bass", "sub", "arp", "chord", "harm", "riff", "drone", "lead" })
                {
                    bool found = false;
                    for (size_t at = 0; (at = bare.find(std::string("\n") + name + " ", at)) != std::string::npos; ++at)
                    {
                        const size_t end = bare.find('\n', at + 1);
                        const std::string line = bare.substr(at + 1, end - at - 1);
                        if (line.find("sfz/") == std::string::npos && line.find(" gain") == std::string::npos) found = true;
                    }
                    CHECK(found, "improv %d: no installed sound for %s", k, name);
                }
            }
            if (seed == 1) std::printf("      improv %2d  %-22s %s  %.0f%% different  %s\n", k, s.name.c_str(), c->describe().c_str(), 100 * s.distance, s.move.c_str());
        }
    // the variety (Kerem, 2026-11-19): every band size, shape and harmony move turns up, and the rest now and then
    for (int b : bands) CHECK(b > 0, "a band size never chosen");
    for (int b : shapes) CHECK(b > 0, "a shape never chosen");
    for (int b : moves) CHECK(b > 0, "a harmony move never chosen");
    CHECK(solos > songs / 5 && drifts > songs / 8 && halves > songs / 8 && crosses > songs / 8 && memories > songs / 5,
          "solos %d drifts %d halves %d crosses %d memories %d of %d", solos, drifts, halves, crosses, memories, songs);
    std::printf("      improv variety over %d songs: bands %d/%d/%d/%d, shapes %d/%d/%d/%d/%d/%d, solos %d, drifts %d, half %d, cross %d, memory %d\n",
                songs, bands[0], bands[1], bands[2], bands[3], shapes[0], shapes[1], shapes[2], shapes[3], shapes[4], shapes[5], solos, drifts, halves, crosses, memories);
    seed = 0;

    std::printf("%s (%d failures)\n", failures ? "FAILED" : "all checks passed", failures);
    std::printf("e.g. seed 1: %s\n", BerlinComposer(1).describe().c_str());
    return failures ? 1 : 0;
}
