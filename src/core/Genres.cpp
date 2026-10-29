#include "Genres.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace liszt {

namespace {

using G = GenreComposer;
constexpr uint16_t bit(int t) { return uint16_t(1u << t); }
constexpr double kTwoPi = 6.283185307179586;
// The chord tones the arpeggio and the tuned percussion walk on, in degrees above the root:
// root, third, fifth, octave, tenth, twelfth, fifteenth.
constexpr int kLadder[] = { 0, 2, 4, 7, 9, 11, 14 };

G::Part part(const char* low, const char* mid, const char* high, int perBar = 16) { return { { { low, perBar }, { mid, perBar }, { high, perBar } } }; }
G::Part none() { return part("", "", ""); }

uint16_t layersOf(const char* s)
{
    static const char* letters = "PKSHCBUADF_GYMTI"; // in GTrack order (the harm has no letter: it follows the pad)
    uint16_t m = 0;
    for (; *s; ++s)
        if (const char* p = std::strchr(letters, *s)) m |= bit(int(p - letters));
    return m;
}

// ---------------------------------------------------------------- the genres
// Patterns are one bar of sixteenths unless a part says otherwise; a longer string runs over two bars.

const G::Spec kJungle {
    .scene = "jungle", .title = "Jungle",
    // slower than the dance floor's 170 so the live kit can play over it; dorian: the major IV in the minor
    .bpmLo = 158, .bpmHi = 162, .mode = &kDorian, .tonics = { 2, 9, 4, 11, 7 },
    .progs = { { { 0, 3, 0, 6 }, { 0, 2, 3, 6 }, { 0, 4, 3, 6 } } }, .preRoot = 3,
    .form = { { { "Mist", 16, .15f, "PF" }, { "Signal", 16, .35f, "PAHF" }, { "First Break", 64, .65f, "PKSHCBUA" },
                { "Undertow", 32, .45f, "PBUAD" }, { "Second Break", 64, .85f, "PKSHCBUAD" }, { "Void", 16, .30f, "PAF" },
                { "Ascension", 80, 1.0f, "PKSHCBUADF" }, { "Rain", 32, .50f, "PKSHCAL" }, { "Tide", 16, .10f, "PF" } } },
    // the break's skeleton drives the pulse; the metal answers off the backbeat, the ticks run between
    .pulse = part("X.........X.....", "X.X.......XX....X.X.......X.....", "X.X.......XX....X.X.......X..X.."),
    .metal = part("..........1.....", "...1......2..3..", "...1..2...3..1.2..1...3.2...4..2"),
    .tick = part("x.o.x.o.x.o.x.o.", "xgogxgo2xgogxg2g", "xoxgxoxgx2xgxoxg"),
    // deep struck metal, tuned to the chord, away from the backbeats the live breaks play
    .perc = part("", "......1...3.....", "......1...3...2.......1.....4.2."),
    // the reese: whole bars, the fifth at the end of the phrase
    .bass = part("x---------------", "x-----------5---", "x-------x---5-8-"),
    .chord = part("", "x---------------................", "x---------------................"),
    // the 808 in halftime under the breaks, sliding up the octave into the turn
    .sub = part("x---------------", "x-----------x---x-------^-------", "x-------x---x---x-----5-^---x---"),
    .percLo = 36, .percHi = 52,
    .chop = .35f,
};

const G::Spec kTripHop {
    .scene = "triphop", .title = "Trip-Hop",
    // after Amon Tobin: chopped jazz under a cinematic dark, melodic minor (the minor-major seventh,
    // the dominant IV and V), a walking bass, the comping chords answering the hook
    .bpmLo = 88, .bpmHi = 96, .mode = &kMelodicMinor, .tonics = { 0, 3, 10, 5, 8 },
    .progs = { { { 0, 3, 0, 4 }, { 0, 5, 1, 4 }, { 2, 3, 0, 4 } } }, .preRoot = 4,
    .form = { { { "Smoke Room", 8, .15f, "PFT" }, { "Foley", 16, .35f, "PHCDTY" }, { "Supermodified", 32, .60f, "PKSHCBUADLGYMT" },
                { "Ghost Note", 16, .40f, "PDAFMT" }, { "Bloodstone", 32, .85f, "PKSHCBUADGYMTI" }, { "Vacuum", 8, .25f, "PAT" },
                { "Four Ton Mantis", 40, 1.0f, "PKSHCBUADFGYMTI" }, { "Ash", 16, .30f, "PDALMT" } } },
    // the live kit plays the break; the machine's percussion cuts it up around him
    .pulse = part("X.......X.X.....", "X.....X...X.....", "X.....X.X.X...x."),
    .metal = part("......1.......2.", "...1..2....3..1.", "..1..2.1...3.2.1"), // vibraphone-like struck metal
    .tick = part("x.......x.......", "x...x.g.x...x.g.", "x.g.x.g.x.g.x.g2"), // fewer: the colour, not the groove
    .perc = part("", "1.....3...2.....", "1..3..2...4.1.3."),
    .bass = part("x-------5-------", "x---x---5---3---", "x--.x-5-3---x-8-"), // walking: root, fifth, third, octave
    .chord = part("x-------........", "x-----..x-......", "x-..x-....x-..x."),
    .sub = part("x---------------", "x-------x-----^-", "x-----x-x-------"),
    // the section: log drums under, hand drums answering the snare (never on 2 or 4), a kalimba in
    // threes against the bar, vinyl crackle held through
    .ground = part("", "..........1.....", "......1...3...........2...1....."),
    .body = part("", "..o.....x..o..x.", "..o..g.o..x.o.xg"),
    .mallet = part("", "1..3..4..3..1..2", "1.23.14.32.14.3."),
    .texture = part("x---------------", "x---------------", "x-------x-------"),
    .percLo = 38, .percHi = 57,
    .chop = .30f, .stutter = .15f, .stutterTrack = G::Tick, .stutterDiv = 32, .swing = .18f, .chordSweep = .35f,
};

const G::Spec kEcstatic {
    .scene = "ecstatic", .title = "Ecstatic Arpeggios",
    .bpmLo = 136, .bpmHi = 142, .mode = &kLydian, .tonics = { 5, 10, 0, 3, 8 }, // lydian: the raised fourth, the major II: the sublime
    .progs = { { { 0, 1, 0, 1 }, { 5, 1, 0, 4 }, { 0, 4, 5, 1 } } }, .preRoot = 1,
    .form = { { { "Dawn", 16, .15f, "PA" }, { "Spiral", 32, .35f, "PA" }, { "Lattice", 48, .55f, "PADH" },
                { "Suspension", 32, .40f, "PAFS" }, { "Ecstasy", 64, .80f, "PADHSCBUL" }, { "Afterimage", 32, .45f, "PAS" },
                { "Computation", 64, 1.0f, "PKSHCBUADF" }, { "Halo", 32, .20f, "PAD" } } },
    // no break here: the arpeggio is the rhythm; the pulse only lands at the summit
    .pulse = part("", "", "X...X...X...X..."),
    .metal = part("", "", "......1.......2."),
    .tick = part("", "..g...g...g...g.", "g.g.g.g.g.g.g.g."),
    .perc = part("", "..........1.....", "..........1...........3.....2..."),
    .bass = part("", "x-------x-------", "x-------5---8---"),
    // the chords gated pointillistically: the trance without its kick
    .chord = part("x-------........", "x.x.x..x.x.x..x.", "x.xx.xx.x.xx.xx."),
    .sub = part("", "x---------------", "x-------x---^---"),
    .arpRate = { 16, 16, 32 }, .arpLenLo = 7, .arpLenHi = 11, .arpDensLo = .60f, .arpDensHi = 1.0f, .arpGate = .7f,
    .arpLo = 60, .arpHi = 96, .percLo = 36, .percHi = 52, .hook = false,
};

const G::Spec kDub {
    .scene = "dubtechno", .title = "Dub Techno",
    .bpmLo = 120, .bpmHi = 124, .mode = &kAeolian, .tonics = { 9, 2, 7, 0, 5 },
    .progs = { { { 0, 0, 3, 3 }, { 0, 5, 3, 0 }, { 0, 3, 0, 6 } } }, .preRoot = 3,
    .form = { { { "Fog", 16, .15f, "PD" }, { "Current", 32, .35f, "PKHD" }, { "Depth", 48, .55f, "PKSHCBUDL" },
                { "Haze", 32, .35f, "PDF" }, { "Pressure", 64, .80f, "PKSHCBUDF" }, { "Echo Chamber", 32, .45f, "PDHA" },
                { "Submersion", 64, 1.0f, "PKSHCBUADF" }, { "Silt", 16, .15f, "PD" } } },
    .pulse = part("X...x...x...x...", "X...x...x...x...", "X...x...x...x..."),
    .metal = part("", "......1.......1.", "......1....2..1."), // rimshots
    .tick = part("..o...o...o...o.", "g.2.g.2.g.2.g.2.", "g.2gg.2gg.2gg.2g"),
    .perc = part("", "..........1.....", "..........1...........3...2....."), // a deep drum into the echo
    .bass = part("x.......x.......", "..x...x...x...x.", "..x..xx...x..x5."),
    // the stab: a minor seventh, short, into a long echo; its filter breathes over minutes
    .chord = part("..x.............", "..x.....x.....x.", "..x..x....x..x.."),
    .sub = part("x---------------", "x-----------x---", "x-------x---^---"),
    .percLo = 36, .percHi = 52, .ext = 1, .swing = .12f, .chordSweep = .55f,
};

const G::Spec kNoir {
    .scene = "noir", .title = "Noir",
    // after Portishead, Mezzanine and Bohren: slow, the hungarian minor (the raised fourth and the
    // leading tone around a minor tonic), long chords, the hook alone in a big room
    .bpmLo = 72, .bpmHi = 80, .mode = &kHungarianMinor, .tonics = { 11, 6, 8, 1, 3 },
    .progs = { { { 0, 5, 4, 0 }, { 0, 5, 1, 4 }, { 0, 6, 5, 4 } } }, .preRoot = 4,
    .form = { { { "Rain", 8, .15f, "PF" }, { "Cigarette", 16, .30f, "PHD" }, { "Roads", 24, .55f, "PKSHCBUDL" },
                { "Hollow Hall", 16, .35f, "PDAF" }, { "Mezzanine", 32, .80f, "PKSHCBUAD" }, { "Teardrop", 8, .20f, "PA" },
                { "Cathedral of Smoke", 32, 1.0f, "PKSHCBUADF" }, { "Last Light", 16, .25f, "PDAL" } } },
    .pulse = part("X.........X.....", "X.......X.X.....", "X.....x.X.X....."),
    .metal = part("", "..........1.....", "......1...2...3."), // far bells
    .tick = part("..g...g...g...g.", "x.g.x.g.x.g.x.g2", "xgxgx2xgxgxgx2xg"),
    .perc = part("", "......1.......3.", "...1..2...3...2."), // deep metal, never on the one: his kick is there
    .bass = part("x---------------", "x-------5-------", "x-----5-x---3---"),
    .chord = part("x---------------", "x-------x-------", "x---..x-x---..x-"),
    .sub = part("x---------------", "x-------^-------", "x-----x-^---x---"),
    .percLo = 36, .percHi = 52,
    .gap = .10f, .swing = .10f, .chordSweep = .45f,
};

const G::Spec kGqom {
    .scene = "gqom", .title = "Gqom",
    .bpmLo = 118, .bpmHi = 124, .mode = &kPhrygian, .tonics = { 4, 11, 6, 1, 9 },
    .progs = { { { 0, 1, 0, 1 }, { 0, 5, 6, 1 }, { 0, 1, 5, 6 } } }, .preRoot = 1,
    .form = { { { "Taxi Rank", 16, .15f, "PC" }, { "Gathering", 32, .35f, "PCH" }, { "Gqom", 48, .60f, "PKCHSU" },
                { "Ghost", 32, .40f, "PCDF" }, { "Sgubhu", 64, .85f, "PKSHCBUD" }, { "Shadow", 16, .30f, "PF" },
                { "Possessed", 64, 1.0f, "PKSHCBUADF" }, { "Embers", 32, .30f, "PKCL" } } },
    // no four on the floor: the pulse in threes against the bar, the percussion in triplets
    .pulse = part("X..x..x.........", "X..x..x...x.....", "X..x..x...x.x...X..x..x...x..x.x"),
    .metal = part("", "..........1.....", "..........1...2."),
    .tick = part("g.g.g.g.g.g.g.g.", "g.gxg.gxg.gxg.gx", "ggxgggxgggxgggx2"),
    .perc = part("1.....2.....1.....3.....", "1..2..3..1..2.....3..2..", "1..2..3..1..2..3..2..1.3", 24),
    .bass = part("", "x---............", "x---......x-----"),
    .chord = part("x...............................", "x.........x.....", "x.....x...x....."),
    .sub = part("x---------------", "x-----x-----x---", "x--x--x---x-^---"),
    .percLo = 38, .percHi = 57, .swing = .08f,
};

const G::Spec kDeconstructed {
    .scene = "deconstructed", .title = "Deconstructed Club",
    .bpmLo = 128, .bpmHi = 136, .mode = &kPhrygianDominant, .tonics = { 4, 9, 2, 11, 7 }, // the major third over the flat second
    .progs = { { { 0, 1, 5, 6 }, { 0, 3, 1, 0 }, { 0, 6, 5, 1 } } }, .preRoot = 1,
    .form = { { { "Shatter", 16, .20f, "PDF" }, { "Glass", 32, .40f, "PCKD" }, { "Fracture", 48, .65f, "KSCBDUL" },
                { "Cathedral", 32, .40f, "PDAF" }, { "Violence", 48, .85f, "PKSHCBUAD" }, { "Silence", 16, .15f, "P" },
                { "Rebirth", 64, 1.0f, "PKSHCBUADF" }, { "Wound", 32, .30f, "PDA" } } },
    .pulse = part("X.......X..x....", "X..x.....X.x..x.", "X..x.X...X.xx..xX....x..X..x.X.."),
    .metal = part("", "......1...2.1...", "...1..2...3.1.21"),
    .tick = part("", "x...x.x...x...x.", "x.xxx.x.x.xxx.xx"),
    .perc = part("1.......3.......", "1..3..2...4..1..", "1.3.2.41.3.2.4.1"), // metal: clicks tuned to the chord
    .bass = part("x---------------", "x-------x---5---", "x--x--x-x---8-5-"),
    .chord = part("X---------------", "X.......X..X....", "X..X..X.X.X..X.."),
    .sub = part("x---------------", "x-------x--x----", "x--x--x-x---^-5-"),
    .percLo = 60, .percHi = 90,
    .chop = .30f, .gap = .25f, .stutter = .20f, .stutterTrack = G::Metal, .stutterDiv = 32,
};

const G::Spec kHalftime {
    .scene = "halftime", .title = "Halftime",
    .bpmLo = 168, .bpmHi = 174, .mode = &kHarmonicMinor, .tonics = { 7, 0, 5, 10, 2 }, // harmonic minor: the major V pulling home
    .progs = { { { 0, 5, 3, 4 }, { 0, 3, 4, 0 }, { 5, 3, 4, 4 } } }, .preRoot = 4,
    .form = { { { "Smoke", 16, .15f, "PF" }, { "Weight", 32, .35f, "PHBU" }, { "Halftime", 64, .60f, "PKSHCBUD" },
                { "Hollow", 32, .40f, "PAF" }, { "Mass", 64, .85f, "PKSHCBUAD" }, { "Breath", 16, .25f, "PA" },
                { "Gravity", 64, 1.0f, "PKSHCBUADF" }, { "Fall", 32, .30f, "PKSCAL" } } },
    // halftime: the tempo is 170, the body feels 85 (the live snare on three); the metal answers around it
    .pulse = part("X...............", "X.........x.....", "X.........x...x.X......x..x....."),
    .metal = part("..........1.....", "......1...2.....", "......1...2...3.......1...2..1.3"),
    .tick = part("x...x...x...x...", "x.x.x.x.x.x.x.x.", "x.xgx.x.x.xgx.x."),
    .perc = part("", "..........1.....", "......1...3...........2...4..1.."), // deep metal around the snare on three
    .bass = part("x---------------", "x-------..x-----", "x-----x-..x---5-"),
    .chord = part("", "x---------------................", "x---------------................"),
    .sub = part("x---------------", "x---------x-----x-------^-------", "x-------x-x-----x-----5-^---x-x-"),
    .percLo = 36, .percHi = 52,
    .chop = .10f, .stutter = .25f, .stutterTrack = G::Tick, .stutterDiv = 32,
};

bool is(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

} // namespace

const std::vector<const G::Spec*>& GenreComposer::all()
{
    static const std::vector<const Spec*> v = { &kJungle, &kTripHop, &kEcstatic, &kDub, &kNoir, &kGqom, &kDeconstructed, &kHalftime };
    return v;
}

const G::Spec* GenreComposer::find(const std::string& scene)
{
    for (auto* s : all()) if (scene == s->scene) return s;
    return nullptr;
}

const SceneInfo* genreSceneInfo(const std::string& scene)
{
    // The same eleven tracks for every genre: the pad, the choir and the chords into the reverb, the
    // arpeggio and the chords into the echo, the low end dry.
    static const std::vector<SceneInfo> infos = [] {
        std::vector<SceneInfo> v;
        for (auto* s : G::all())
            v.push_back({ s->scene, 16, { "pad", "pulse", "metal", "tick", "perc", "bass", "sub", "arp", "chord", "fx", "harm",
                                          "ground", "body", "mallet", "texture", "impact" },
                          { 0, 1, 2, 2, 2, 1, 1, 3, 0, 0, 0, 1, 2, 2, 2, 1 },
                          { { 0, .30f }, { 0, .02f }, { .05f, .12f }, { .03f, .08f }, { .10f, .15f }, { 0, .03f }, { 0, 0 }, { .20f, .20f }, { .20f, .25f }, { .10f, .35f }, { .08f, .40f },
                            { 0, .10f }, { .05f, .12f }, { .20f, .25f }, { 0, .20f }, { .10f, .45f } },
                          { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
                          { 1, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0 }, true });
        return v;
    }();
    for (auto& i : infos) if (scene == i.name) return &i;
    return nullptr;
}

GenreComposer::GenreComposer(const Spec& spec, uint64_t seed) : Composer(seed), spec_(spec)
{
    tonicPc_ = spec_.tonics[size_t(rng_.range(0, 4))];
    bpm_ = 2.0 * rng_.range(spec_.bpmLo / 2, spec_.bpmHi / 2);
    planForm();
    planHarmony();
    makeArp();
    arpHome_ = arp_;
    makeHook();
    // the groove's accents, fixed for the song so the feel is one feel: the downbeat strongest, the
    // backbeats next, the rest leaning a little differently in every song
    for (int i = 0; i < 16; ++i)
        accent_[size_t(i)] = i == 0 ? 1.0f : i % 4 == 0 ? float(rng_.range(88, 98)) / 100 : float(rng_.range(70, 92)) / 100;
}

void GenreComposer::planForm()
{
    float peak = 0;
    for (auto& t : spec_.form) if (t.name) peak = std::max(peak, t.energy);
    bool themed = false;
    int bar = 0;
    float prev = 0;
    for (auto& t : spec_.form)
    {
        if (!t.name) break;
        const bool theme = !themed && t.energy >= peak; // the summit: the home arpeggio comes back
        themed = themed || theme;
        sections_.push_back({ t.name, bar, t.bars, t.energy, layersOf(t.layers), theme });
        drummerLeads_.push_back(std::strchr(t.layers, 'L') != nullptr);
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

void GenreComposer::planHarmony()
{
    // One progression is home and keeps coming back; the breakdowns (a section quieter than the one
    // before it) play another. A chord lasts four bars in the calm and at the summit, where the
    // slowness is the weight, two while the music drives. The section before the summit lands on the
    // chord that leads into it; the last section rests on the tonic.
    const int h = rng_.range(0, 2);
    int a = rng_.range(0, 1);
    if (a >= h) ++a;
    home_ = spec_.progs[size_t(h)];
    away_ = spec_.progs[size_t(a)];
    rootOfBar_.assign(sectionOfBar_.size(), 0);
    awayBar_.assign(sectionOfBar_.size(), false);
    for (size_t si = 0; si + 1 < sections_.size(); ++si)
    {
        auto& sec = sections_[si];
        const bool away = si > 0 && sec.energy < sections_[si - 1].energy;
        const bool beforePeak = sections_[si + 1].restoreTheme;
        const int per = sec.energy < .4f || sec.restoreTheme ? 4 : 2;
        const auto& prog = away ? away_ : home_;
        if (sec.restoreTheme) liftBar_ = sec.startBar + sec.bars / 2;
        for (int b = 0; b < sec.bars; ++b)
        {
            const size_t at = size_t(sec.startBar + b);
            // into the summit the chords quicken to one a bar, then land on the chord that leads in
            const bool quicken = beforePeak && b >= sec.bars - 6;
            rootOfBar_[at] = beforePeak && b >= sec.bars - 2 ? spec_.preRoot : quicken ? prog[size_t(b % 4)] : prog[size_t((b / per) % 4)];
            awayBar_[at] = away;
        }
    }
}

std::vector<int> GenreComposer::chordTones(int bar, bool rootless) const
{
    // thirds stacked on the root inside the mode: the triad, its seventh, its ninth. Rootless
    // (the bass has the root) it is third, fifth, seventh, ninth: the colour without the weight.
    const auto& m = *spec_.mode;
    const int r = rootAt(bar), n = 3 + spec_.ext;
    std::vector<int> pcs;
    for (int k = rootless && n > 3 ? 1 : 0; k < n; ++k) pcs.push_back((tonicAt(bar) + degreeSemis(m, r + 2 * k)) % 12);
    return pcs;
}

std::vector<int> GenreComposer::voice(int bar, int n, int lo, int hi, bool rootless, std::vector<int>& prev) const
{
    // Every close position of the chord (each tone in turn at the bottom, at every octave that fits)
    // and the one whose voices move least from the last chord. The first chord sits low in the range.
    const auto pcs = chordTones(bar, rootless);
    const int k = int(pcs.size());
    std::vector<int> best;
    int bestCost = 1 << 30;
    for (int rot = 0; rot < k; ++rot)
        for (int base = lo; base <= hi; ++base)
        {
            if ((base % 12 + 12) % 12 != pcs[size_t(rot)]) continue;
            std::vector<int> v { base };
            for (int i = 1; i < n; ++i)
            {
                int p = v.back() + 1;
                while ((p % 12 + 12) % 12 != pcs[size_t((rot + i) % k)]) ++p;
                v.push_back(p);
            }
            if (v.back() > hi) continue;
            // the least motion, held near the middle of the range so the voicing does not creep up or down
            int cost = std::abs(v[0] + v.back() - (lo + hi)) / 2;
            if (prev.size() == v.size()) for (int i = 0; i < n; ++i) cost += 2 * std::abs(v[size_t(i)] - prev[size_t(i)]);
            if (cost < bestCost) { bestCost = cost; best = v; }
        }
    prev = best;
    return best;
}

void GenreComposer::makeHook()
{
    // The hook: two bars of sixteenths. The first bar asks (three to five notes, the downbeat always,
    // the rest leaning on the off-beats a 3-3-2 groove gives), the second answers, half of it in the
    // first bar's rhythm, and ends on a long chord tone. The line moves mostly by step with one leap;
    // a note on the beat takes a chord tone, between beats a passing tone may sound.
    static const int askAt[] = { 3, 6, 8, 10, 11, 12, 14 };
    std::vector<int> onsets { 0 };
    const int n1 = rng_.range(3, 5);
    while (int(onsets.size()) < n1)
    {
        const int s = askAt[size_t(rng_.range(0, 6))];
        if (std::find(onsets.begin(), onsets.end(), s) == onsets.end()) onsets.push_back(s);
    }
    std::sort(onsets.begin(), onsets.end());
    const size_t first = onsets.size();
    for (size_t i = 0; i < first; ++i)
        if (onsets[i] < 12 && rng_.chance(.6)) onsets.push_back(16 + onsets[i]);
    if (onsets.size() == first) onsets.push_back(16);
    std::sort(onsets.begin(), onsets.end());

    auto chordTone = [](int d) { const int m = ((d % 7) + 7) % 7; return m == 0 || m == 2 || m == 4 || m == 6; };
    int deg = std::array<int, 3> { 0, 2, 4 }[size_t(rng_.range(0, 2))];
    bool leapt = false;
    hook_.clear();
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const int s = onsets[i];
        if (i > 0)
        {
            int step = rng_.chance(.5) ? 1 : 2;
            if (!leapt && rng_.chance(.25)) { step = rng_.range(3, 4); leapt = true; }
            deg += rng_.chance(deg > 4 ? .7 : deg < 0 ? .3 : .5) ? -step : step;
            deg = std::clamp(deg, -2, 9);
        }
        const bool last = i + 1 == onsets.size();
        if ((s % 4 == 0 || last) && !chordTone(deg)) deg += deg > 4 ? -1 : 1;
        const int next = last ? 31 : onsets[i + 1];
        const int len = last ? 32 - s : std::max(1, next - s - (rng_.chance(.25) ? 1 : 0));
        hook_.push_back({ s, len, deg, s % 4 == 0 ? .95f : .78f });
    }
    hookHome_ = hook_;
}

void GenreComposer::emitHook(int bar, std::vector<Event>& out)
{
    if (!active(Arp, bar)) return;
    auto& sec = sectionAt(bar);
    const int in = bar - sec.startBar;
    const float e = energyAt(bar);
    if (sec.restoreTheme && in == 0) hook_ = hookHome_;
    else if (in > 0 && in % 8 == 0 && rng_.chance(.25 + .3 * e)) // it wears a little, as a phrase does when it is played on
    {
        auto& n = hook_[size_t(rng_.range(1, int(hook_.size()) - 1))];
        n.deg = std::clamp(n.deg + (rng_.chance(.5) ? 1 : -1) * 2, -2, 9);
    }

    // How this section develops it.
    const bool calm = e < .35f, away = awayBar_[size_t(bar)], summit = sec.restoreTheme;
    const int span = calm ? 4 : 2;            // slowed to half speed in the calm
    const int cycle = in / span, within = in % span;
    if (calm && cycle % 2 == 1) return;       // alone, with room around it
    const int t0Cycle = (bar - within) * kBarTicks;
    const int64_t six = kSixteenth * (calm ? 2 : 1);
    const auto& m = *spec_.mode;
    const int r = rootAt(bar), lo = spec_.arpLo, hi = spec_.arpHi;
    const int rootP = pitchOf(bar, 0, lo + 5, lo + 16, 12 * (lo / 12)); // base on a C: it is a register, not a transposition
    // In eight bars: the question, its answer, the question, and a close (the answer cut to one
    // long note on the chord's root): a period, as a song has.
    const bool close = !calm && !away && cycle % 4 == 3;
    bool closed = false;
    for (auto n : hook_)
    {
        const int64_t t = int64_t(t0Cycle) + n.step * six;
        if (t < int64_t(bar) * kBarTicks || t >= int64_t(bar + 1) * kBarTicks) continue; // only this bar's notes
        if (close && n.step >= 16)
        {
            if (closed) continue;
            closed = true;
            n.deg = 0;
            n.len = 32 - n.step;
        }
        int deg = n.deg;
        if (away) deg = 4 - deg;                             // the breakdowns turn it upside down
        else if (!summit && n.step >= 16 && cycle % 2 == 1) deg += 2; // every other time, its answer a third up
        const int64_t len = std::max<int64_t>(kSixteenth / 2, n.len * six - 12);
        auto put = [&](int p, float vel) {
            out.push_back({ t, Event::NoteOn, Arp, uint8_t(p), vel });
            out.push_back({ t + len, Event::NoteOff, Arp, uint8_t(p), 0 });
        };
        int p = rootP + degreeSemis(m, r + deg) - degreeSemis(m, r);
        while (p > hi) p -= 12;
        while (p < lo) p += 12;
        put(p, n.vel);
        if (summit && cycle > 0 && p + 12 <= hi) put(p + 12, n.vel * .8f); // the summit doubles it an octave up
    }
}

void GenreComposer::makeArp()
{
    // A cycle of chord tones that walks up and down the ladder, its length rarely a divisor of the
    // bar, so it phases against the beat; the ranks decide which notes sound as the energy rises.
    const int len = rng_.range(spec_.arpLenLo, spec_.arpLenHi);
    arp_.assign(size_t(len), 0);
    int at = 0;
    for (int i = 1; i < len; ++i)
    {
        at = std::clamp(at + (rng_.chance(.5) ? 1 : -1) * rng_.range(1, 2), 0, 6);
        arp_[size_t(i)] = at;
    }
    std::vector<std::pair<double, int>> order;
    for (int i = 0; i < len; ++i) order.push_back({ i == 0 ? -1.0 : rng_.uniform(), i });
    std::sort(order.begin(), order.end());
    arpRank_.assign(size_t(len), 0);
    for (int r = 0; r < len; ++r) arpRank_[size_t(order[size_t(r)].second)] = r;
}

bool GenreComposer::active(int track, int bar) const
{
    if (bar < 0 || bar >= totalBars()) return false;
    if (track == Harm) return (sectionAt(bar).layers & bit(Pad)) != 0 && sectionAt(bar).energy >= .4f;
    return (sectionAt(bar).layers & bit(track)) != 0;
}

std::array<int, 2> GenreComposer::range(int track) const
{
    switch (track)
    {
        case Pad: return { 36, 76 };
        case Pulse: return { 24, 35 };
        case Metal: return { 48, 76 };
        case Tick: return { 72, 100 };
        case Perc: return { spec_.percLo, spec_.percHi };
        case Bass: return { 40, 64 };
        case Sub: return { 24, 52 };
        case Arp: return { spec_.arpLo, spec_.arpHi };
        case Chord: return { 46, 76 };
        case Harm: return { 60, 86 };
        case Ground: return { 28, 48 };
        case Body: return { 45, 69 };
        case Mallet: return { 60, 88 };
        case Texture: return { 48, 84 };
        case Impact: return { 30, 50 };
        default: return { 48, 72 };
    }
}

int GenreComposer::tier(int bar) const
{
    const float e = energyAt(bar);
    return e < .4f ? 0 : e < .75f ? 1 : 2;
}

float GenreComposer::levelAt(int track, int bar) const
{
    if (!active(track, bar)) return 0;
    auto& sec = sectionAt(bar);
    const int in = bar - sec.startBar, left = sec.startBar + sec.bars - bar;
    const float fade = track == Pad || track == Fx || track == Sub || track == Chord || track == Harm ? 4.0f : 2.0f;
    const float fadeIn = active(track, bar - in - 1) || (bar == 0 && track != Pad) ? 1.0f : std::min(1.0f, (in + 1) / fade);
    const float fadeOut = active(track, bar + left) ? 1.0f : std::min(1.0f, left / fade);
    return std::min(fadeIn, fadeOut);
}

int GenreComposer::pitchOf(int bar, int deg, int lo, int hi, int base) const
{
    return foldInto(base + tonicAt(bar) + degreeSemis(*spec_.mode, rootAt(bar) + deg), lo, hi);
}

int GenreComposer::ladderPitch(int bar, int idx, int lo, int hi) const
{
    int p = 12 * (lo / 12) + tonicAt(bar) + degreeSemis(*spec_.mode, rootAt(bar) + kLadder[idx % 7]);
    while (p < lo) p += 12;
    while (p > hi) p -= 12;
    return p;
}

void GenreComposer::emitAutomation(int bar, std::vector<Event>& out)
{
    const int64_t t0 = int64_t(bar) * kBarTicks;
    for (int tr = 0; tr < kGenreTracks; ++tr)
        out.push_back({ t0, Event::Level, uint8_t(tr), 0, levelAt(tr, bar) });
    auto& sec = sectionAt(bar);
    const float e0 = energyAt(bar), e1 = bar + 1 < totalBars() ? energyAt(bar + 1) : e0;
    const bool next = bar + 1 < totalBars();
    const bool rise = next && sectionAt(bar + 1).energy > sec.energy;
    for (int beat = 0; beat < 4; ++beat)
    {
        const double e = e0 + (e1 - e0) * beat / 4.0;
        const double ramp = (bar - sec.startBar + beat / 4.0) / sec.bars;
        const double pos = bar * 4 + beat;
        auto put = [&](int tr, double v) { out.push_back({ t0 + beat * kPPQ, Event::Filter, uint8_t(tr), 0, float(std::clamp(v, -1.0, 1.0)) }); };
        put(Pad, -0.6 + 0.8 * e + .15 * std::sin(kTwoPi * pos / 128 + 1.1));
        put(Chord, -0.5 + 0.9 * e + spec_.chordSweep * std::sin(kTwoPi * pos / 96 + 2.0));
        put(Arp, -0.7 + 1.3 * e + .25 * std::sin(kTwoPi * pos / 64));
        put(Bass, -0.3 + 0.5 * e);
        put(Fx, rise ? -1.0 + 2.0 * ramp : -0.4 + .25 * std::sin(kTwoPi * pos / 64));
    }
}

void GenreComposer::emitHeld(int track, int bar, std::vector<Event>& out, int& held, int pitch)
{
    const int64_t t0 = int64_t(bar) * kBarTicks;
    const bool retrigger = track == Fx && bar == sectionAt(bar).startBar; // the riser starts again with each section
    if (held >= 0 && (pitch != held || retrigger)) { out.push_back({ t0, Event::NoteOff, uint8_t(track), uint8_t(held), 0 }); held = -1; }
    if (pitch >= 0 && held < 0) { out.push_back({ t0, Event::NoteOn, uint8_t(track), uint8_t(pitch), .8f }); held = pitch; }
    if (bar + 1 == totalBars() && held >= 0) { out.push_back({ t0 + kBarTicks, Event::NoteOff, uint8_t(track), uint8_t(held), 0 }); held = -1; }
}

void GenreComposer::emitPad(int bar, std::vector<Event>& out)
{
    const int64_t t0 = int64_t(bar) * kBarTicks;
    std::vector<int> want;
    if (active(Pad, bar))
    {
        // the root and its fifth low, the colour of the chord (third, seventh, ninth) voiced above
        // them, moving as little as it can from the last chord
        const auto& m = *spec_.mode;
        const int r = rootAt(bar), lo = pitchOf(bar, 0, 36, 47, 36);
        want = { lo, lo + degreeSemis(m, r + 4) - degreeSemis(m, r) };
        for (int p : voice(bar, 3, 55, 76, true, padVoices_)) want.push_back(p);
    }
    // On a new chord its third arrives late: the voice holds the fourth over the bar line and falls
    // to the third on beat three (a suspension), once the music has some motion.
    int suspended = -1, sus = -1;
    if (!want.empty() && bar > 0 && rootAt(bar) != rootAt(bar - 1) && energyAt(bar) > .3f)
    {
        const int third = (tonicAt(bar) + degreeSemis(*spec_.mode, rootAt(bar) + 2)) % 12;
        for (size_t i = 2; i < want.size(); ++i)
            if (want[i] % 12 == third)
            {
                const int up = want[i] + degreeSemis(*spec_.mode, rootAt(bar) + 3) - degreeSemis(*spec_.mode, rootAt(bar) + 2);
                const auto has = [](const std::vector<int>& v, int p) { return std::find(v.begin(), v.end(), p) != v.end(); };
                if (!has(padHeld_, want[i]) && !has(padHeld_, up) && !has(want, up) && up <= 76) { suspended = want[i]; sus = up; }
                break;
            }
    }
    for (int p : padHeld_)
        if (std::find(want.begin(), want.end(), p) == want.end()) out.push_back({ t0, Event::NoteOff, Pad, uint8_t(p), 0 });
    for (int p : want)
    {
        if (std::find(padHeld_.begin(), padHeld_.end(), p) != padHeld_.end()) continue;
        if (p == suspended)
        {
            out.push_back({ t0, Event::NoteOn, Pad, uint8_t(sus), .75f });
            out.push_back({ t0 + 2 * kPPQ, Event::NoteOff, Pad, uint8_t(sus), 0 });
            out.push_back({ t0 + 2 * kPPQ, Event::NoteOn, Pad, uint8_t(p), .7f });
        }
        else out.push_back({ t0, Event::NoteOn, Pad, uint8_t(p), .75f });
    }
    padHeld_ = want;
    if (bar + 1 == totalBars())
    {
        for (int p : padHeld_) out.push_back({ t0 + kBarTicks, Event::NoteOff, Pad, uint8_t(p), 0 });
        padHeld_.clear();
    }
}

void GenreComposer::emitHarm(int bar, std::vector<Event>& out)
{
    // The choir: three voices above the pad, voice-led on their own. On a new chord they breathe:
    // the old chord ends on the bar line, the new one enters half a bar late; while a chord holds, every other bar the top voice sighs
    // up to its neighbour in the mode on beat three and comes back with the next bar.
    const int64_t t0 = int64_t(bar) * kBarTicks;
    std::vector<int> want;
    if (active(Harm, bar)) want = voice(bar, 3, 62, 84, true, harmVoices_);
    const bool late = !want.empty() && bar > 0 && rootAt(bar) != rootAt(bar - 1);
    const int64_t at = t0 + (late ? 2 * kPPQ : 0);
    for (int p : harmHeld_)
        if (std::find(want.begin(), want.end(), p) == want.end()) out.push_back({ t0, Event::NoteOff, Harm, uint8_t(p), 0 });
    for (int p : want)
        if (std::find(harmHeld_.begin(), harmHeld_.end(), p) == harmHeld_.end()) out.push_back({ at, Event::NoteOn, Harm, uint8_t(p), .7f });
    harmHeld_ = want;
    const bool holds = !want.empty() && !late && bar + 1 < totalBars() && rootAt(bar + 1) == rootAt(bar);
    if (holds && bar % 2 == 1 && energyAt(bar) > .55f)
    {
        const int top = want.back();
        int up = top + 1;
        while (up <= top + 2 && !inMode(bar, up)) ++up;
        if (up <= 86 && up <= top + 2 && inMode(bar, up))
        {
            out.push_back({ t0 + 2 * kPPQ, Event::NoteOff, Harm, uint8_t(top), 0 });
            out.push_back({ t0 + 2 * kPPQ, Event::NoteOn, Harm, uint8_t(up), .62f });
            out.push_back({ t0 + kBarTicks - 30, Event::NoteOff, Harm, uint8_t(up), 0 });
            out.push_back({ t0 + kBarTicks - 30, Event::NoteOn, Harm, uint8_t(top), .66f });
        }
    }
    if (bar + 1 == totalBars())
    {
        for (int p : harmHeld_) out.push_back({ t0 + kBarTicks, Event::NoteOff, Harm, uint8_t(p), 0 });
        harmHeld_.clear();
    }
}

void GenreComposer::emitImpact(int bar, std::vector<Event>& out)
{
    // One big hit where a section rises: on its first downbeat, thrown into the echo.
    auto& sec = sectionAt(bar);
    if (!active(Impact, bar) || bar != sec.startBar || bar == 0 || sectionAt(bar - 1).energy >= sec.energy) return;
    const int64_t t0 = int64_t(bar) * kBarTicks;
    const int p = ladderPitch(bar, 0, 30, 50);
    out.push_back({ t0, Event::NoteOn, Impact, uint8_t(p), 1.0f });
    out.push_back({ t0 + kPPQ, Event::NoteOff, Impact, uint8_t(p), 0 });
    out.push_back({ t0, Event::Throw, Impact, 0, 1.0f });
}

bool GenreComposer::inMode(int bar, int pitch) const
{
    for (int d = 0; d < 7; ++d)
        if ((tonicAt(bar) + degreeSemis(*spec_.mode, d)) % 12 == pitch % 12) return true;
    return false;
}

int GenreComposer::subPitch(int bar, char c) const
{
    // the 808: the root low (41 to 78 Hz), a digit the scale degree above it, ^ the root an octave up
    const auto& m = *spec_.mode;
    const int r = rootAt(bar), root = pitchOf(bar, 0, 28, 39, 24);
    if (c == '^') return root + 12;
    const int deg = c >= '1' && c <= '9' ? c - '1' : 0;
    return root + degreeSemis(m, r + deg) - degreeSemis(m, r);
}

void GenreComposer::emitPart(int track, const Part& part, int bar, std::vector<Event>& out)
{
    if (!active(track, bar)) return;
    const int64_t t0 = int64_t(bar) * kBarTicks;
    auto& sec = sectionAt(bar);
    const int si = sectionOfBar_[size_t(bar)], in = bar - sec.startBar;
    const bool lastBar = bar + 1 == sec.startBar + sec.bars;
    const bool drum = track == Pulse || track == Metal || track == Tick || track == Perc || track == Ground || track == Body || track == Mallet;
    const bool tuned = track == Metal || track == Perc || track == Body; // the fills walk down their chord tones
    // before the summit the drums, the bass and the chords hold their breath for half a bar
    const bool drop = lastBar && bar + 1 < totalBars() && sectionAt(bar + 1).restoreTheme;
    const bool rising = lastBar && bar + 1 < totalBars() && sectionAt(bar + 1).energy > sec.energy;

    // The drums in phrases of sixteen bars: a turn at the end of every fourth bar, a fill at the
    // eighth, a bigger one at the sixteenth (and into a section that rises), the last hit of each
    // fill thrown into the echo.
    const int ph = in % 16;
    const bool fill16 = drum && (ph == 15 || rising) && !drop;
    const bool fill8 = drum && ph == 7 && !fill16 && !drop;
    const bool turn = drum && ph % 4 == 3 && !fill8 && !fill16 && !drop;
    // A section the percussion enters: the ticks first, the metal a quarter in, the perc at half.
    const bool entering = si > 0 && !(sections_[size_t(si - 1)].layers & (bit(Metal) | bit(Tick) | bit(Perc)));
    if (entering && (((track == Metal || track == Body) && in < sec.bars / 4) || ((track == Perc || track == Mallet) && in < sec.bars / 2))) return;
    // Where Kerem's drums lead, the machine's percussion steps back to half and leaves him the room.
    const bool live = drummerLeads_[size_t(si)];

    auto hit = [&](int64_t t, int pitch, float vel, int64_t len) {
        out.push_back({ t, Event::NoteOn, uint8_t(track), uint8_t(pitch), std::clamp(vel, .05f, 1.0f) });
        out.push_back({ t + len, Event::NoteOff, uint8_t(track), uint8_t(pitch), 0 });
    };
    auto throwIt = [&](int64_t t, float beats) { out.push_back({ t, Event::Throw, uint8_t(track), 0, beats }); };
    auto drumPitch = [&](char c, int step) {
        const int alt = c >= '1' && c <= '9' ? c - '1' : 0;
        switch (track)
        {
            case Pulse: return foldInto(24 + tonicAt(bar), 24, 35);
            case Metal: return ladderPitch(bar, alt, 48, 76);
            case Tick: return alt == 1 ? 100 : ladderPitch(bar, 3 + step % 3, 72, 95); // 2: a noise tick, else tuned
            case Ground: return ladderPitch(bar, alt, 28, 48);
            case Body: return ladderPitch(bar, alt + step % 2, 45, 69); // the two hands: two pitches of the drum
            case Mallet: return ladderPitch(bar, alt, 60, 88);
            default: return ladderPitch(bar, c == 'e' ? step % 5 : alt, spec_.percLo, spec_.percHi);
        }
    };
    const int64_t six = kSixteenth;

    // the fills, played over the pattern's last beats
    int fillFrom = 16; // the pattern stops at this sixteenth for the fill
    if (fill16 && tuned)
    {
        // two beats: sixteenths swelling, the last beat in thirty-seconds, toms falling under them
        fillFrom = 8;
        int64_t last = 0;
        for (int i = 8; i < 16; ++i)
        {
            const bool split = i >= 12;
            for (int k = 0; k < (split ? 2 : 1); ++k)
            {
                const int64_t t = t0 + i * six + k * six / 2;
                const float vel = .35f + .6f * float(i - 8) / 8;
                const auto r = range(track);
                hit(t, ladderPitch(bar, std::max(0, 5 - (i - 8) / 2 - k), r[0], r[1]), vel, six / 2 - 10);
                last = t;
            }
        }
        throwIt(last, 1.0f);
    }
    else if (fill8 && tuned)
    {
        // one beat: four sixteenths, the last one thrown
        fillFrom = 12;
        for (int i = 12; i < 16; ++i)
        {
            const int64_t t = t0 + i * six;
            const auto r = range(track);
            hit(t, ladderPitch(bar, 3 - (i - 12), r[0], r[1]), .45f + .12f * float(i - 12), six - 10);
        }
        throwIt(t0 + 15 * six, .5f);
    }

    const Pat& pat = part.tier[size_t(tier(bar))];
    const int len = int(std::strlen(pat.s));
    if (len == 0) return;
    const int P = pat.perBar;
    const int64_t stepT = kBarTicks / P;
    const bool euclid = is(pat.s, "euclid");
    if (euclid && bar == sec.startBar) percRot_ = rng_.range(0, P - 1);
    const int k = euclid ? 2 + int(energyAt(bar) * P * .25f) : 0;
    const int grow = in / 16; // the phrase count in this section: the ticks grow with it

    std::vector<bool> struck(size_t(P), false);
    for (int i = 0; i < P; ++i)
    {
        const int beat = i * 4 / P;
        if (i * 16 / P >= fillFrom) break;
        if (drop && beat >= 2 && track != Pad) break;
        if (gapBeat_ == beat && track != Pad) continue;
        if (stutterBeat_ == beat && track == spec_.stutterTrack) continue;
        const int src = drum ? i + (chopMap_[size_t(beat)] - beat) * P / 4 : i;
        // The section's cells develop, phrase by phrase of eight bars: the hand drums displaced by a
        // dotted eighth every other phrase, the mallet phasing a sixteenth further each phrase
        // (Reich), the ground thinned to its first stroke at the fourth phrase.
        const int phrase = in / 8;
        const int rot = track == Body ? (phrase % 2) * 3 : track == Mallet ? phrase % P : 0;
        if (track == Ground && phrase % 4 == 3 && i > P / 2) break;
        const int at = int(((int64_t(bar) * P + src + rot) % len + len) % len);
        const char c = euclid ? (((i + percRot_) * k) % P < k ? 'e' : '.') : pat.s[at];
        if (c == '.' || c == '-') continue;
        if (live && track != Pulse && (i & 1)) continue; // half the percussion: the live kit has the room
        int holds = 0;
        if (!drum) while (holds + 1 < len && (track != Sub || i + holds + 1 < P) && pat.s[(at + holds + 1) % len] == '-') ++holds; // the 808 within its bar: the next bar may play another pattern
        float vel = c == 'X' ? 1.0f : c == 'x' || c == 'e' ? .85f : c == 'o' ? .65f : c == 'g' ? .38f : .8f;
        int64_t t = t0 + i * stepT;
        if (P == 16 && (i & 1)) t += int64_t(spec_.swing * six);
        if (drum)
        {
            // the feel: every part leans on the song's accents; the ticks sit a hair behind the
            // off-beats, the metal a hair ahead of them (it pushes, the ticks drag)
            if (P == 16) vel *= accent_[size_t(i)];
            if (track == Tick && P == 16 && (i & 1)) t += six / 12;
            if (track == Metal && P == 16 && (i & 1)) t -= six / 24;
            struck[size_t(i)] = true;
            hit(t, drumPitch(c, i), vel, std::min<int64_t>(stepT / 2, kPPQ - 20)); // short: the swing must not reach the next hit
            continue;
        }
        const int64_t noteLen = int64_t(double(stepT * (1 + holds)) * (holds ? .95 : .7));
        if (track == Sub)
        {
            // Legato into a different note: the next one starts before this one ends, and the 808 slides.
            const int p = subPitch(bar, c), at2 = i + 1 + holds;
            const int nextBar = bar + at2 / P;
            const Pat& np = at2 < P ? pat : part.tier[size_t(tier(std::min(nextBar, totalBars() - 1)))];
            const int nl = int(std::strlen(np.s));
            const char nc = nl == 0 ? '.' : np.s[(int64_t(nextBar) * np.perBar + at2 % P) % nl];
            const bool slide = nc != '.' && nc != '-' && nextBar < totalBars() && active(Sub, nextBar) && subPitch(nextBar, nc) != p;
            const int64_t late = t - (t0 + i * stepT); // the swing: the note ends on the grid all the same
            hit(t, p, vel, stepT * (1 + holds) - late + (slide ? 40 : -10));
            continue;
        }
        if (track == Texture) // held: the scrape, the rattle, the crackle, on a chord tone
        {
            const int deg = c >= '1' && c <= '9' ? c - '1' : 0;
            hit(t, ladderPitch(bar, deg, 48, 84), vel, std::max<int64_t>(stepT, stepT * (1 + holds) - 10));
            continue;
        }
        if (track == Bass)
        {
            const int deg = c >= '1' && c <= '9' ? c - '1' : 0;
            const auto& m = *spec_.mode;
            const int r = rootAt(bar), root = pitchOf(bar, 0, 40, 51, 36); // written an octave up: the bass patches sound an octave down
            hit(t, root + degreeSemis(m, r + deg) - degreeSemis(m, r), vel, noteLen);
        }
        else // the chord, voice-led: four voices of it (rootless when it has a ninth: the bass has the root)
            for (int p : voice(bar, 4, 48, 74, spec_.ext == 2, chordVoices_)) hit(t, p, vel, noteLen);
    }

    // The bass walks into a new chord: on the last eighth of the bar, when the pattern rests there,
    // the scale step next to the coming root, from the side it is coming from.
    if (track == Bass && P == 16 && bar + 1 < totalBars() && active(Bass, bar + 1) && rootAt(bar + 1) != rootAt(bar) && !drop
        && pat.s[(int64_t(bar) * P + 14) % len] == '.' && pat.s[(int64_t(bar) * P + 15) % len] == '.' && energyAt(bar) > .3f)
    {
        const auto& m = *spec_.mode;
        const int next = pitchOf(bar + 1, 0, 40, 51, 36), now = pitchOf(bar, 0, 40, 51, 36);
        const int r1 = rootAt(bar + 1), step = next > now ? -1 : 1;
        const int p = next + degreeSemis(m, r1 + step) - degreeSemis(m, r1);
        if (p >= 40 && p <= 64 && tonicAt(bar + 1) == tonicAt(bar)) hit(t0 + 14 * six, p, .7f, 2 * six - 20);
    }

    // What grows and what turns, on the sixteenth grid only (the triplet parts keep their own).
    if (drum && P == 16 && !live)
    {
        auto add = [&](int i, int pitch, float vel) {
            const int beat = i / 4;
            if (struck[size_t(i)] || i >= fillFrom || beat == gapBeat_ || (beat == stutterBeat_ && track == spec_.stutterTrack)) return;
            if (drop && beat >= 2) return;
            struck[size_t(i)] = true;
            int64_t t = t0 + i * six + ((i & 1) ? int64_t(spec_.swing * six) + (track == Tick ? six / 12 : 0) : 0);
            hit(t, pitch, vel, six / 2);
        };
        if (track == Tick && grow >= 1 && tier(bar) > 0) add(14, 100, .55f);                  // the second phrase adds a noise tick
        if (track == Tick && grow >= 2 && tier(bar) > 0) for (int i = 1; i < 16; i += 2) add(i, drumPitch('g', i), .25f); // the third fills in ghosts
        if (turn && track == Pulse) add(15, drumPitch('x', 15), .6f);                         // a pickup into the next bar (it pumps the mix)
        if (turn && track == Metal && tier(bar) > 0) { add(13, drumPitch('3', 13), .35f); add(15, drumPitch('2', 15), .4f); } // the metal turns
        if (turn && track == Tick) add(10, 100, .6f);
    }

    if (stutterBeat_ >= 0 && track == spec_.stutterTrack && !(drop && stutterBeat_ >= 2) && stutterBeat_ * 4 < fillFrom)
    {
        // a stutter: one beat broken into thirty-seconds (or sixty-fourths), swelling
        const int n = spec_.stutterDiv / 4;
        const int64_t d = kBarTicks / spec_.stutterDiv;
        for (int j = 0; j < n; ++j)
            hit(t0 + stutterBeat_ * kPPQ + j * d, drumPitch(track == Perc ? 'e' : 'x', j), .3f + .6f * float(j) / float(n), d - 8);
        if (track != Pulse) throwIt(t0 + stutterBeat_ * kPPQ + (n - 1) * d, .5f);
    }
}

void GenreComposer::emitArp(int bar, std::vector<Event>& out)
{
    const int R = spec_.arpRate[size_t(tier(bar))];
    if (!active(Arp, bar) || R == 0) return;
    auto& sec = sectionAt(bar);
    const int in = bar - sec.startBar;
    if (sec.restoreTheme && in == 0) arp_ = arpHome_;
    else if (in % 4 == 0 && rng_.chance(.3 + .4 * energyAt(bar)))
        arp_[size_t(rng_.range(1, int(arp_.size()) - 1))] = rng_.range(0, 6);

    const int64_t t0 = int64_t(bar) * kBarTicks, stepT = kBarTicks / R;
    const float dens = spec_.arpDensLo + (spec_.arpDensHi - spec_.arpDensLo) * energyAt(bar);
    const int L = int(arp_.size()), sounding = std::max(1, int(std::ceil(dens * L)));
    std::vector<std::pair<int64_t, int>> notes;
    for (int i = 0; i < R; ++i, ++arpStep_)
    {
        const int idx = int(arpStep_ % L);
        if (arpRank_[size_t(idx)] >= sounding || gapBeat_ == i * 4 / R) continue;
        notes.push_back({ t0 + i * stepT, ladderPitch(bar, arp_[size_t(idx)], spec_.arpLo, spec_.arpHi) });
    }
    for (size_t j = 0; j < notes.size(); ++j)
    {
        const int64_t next = j + 1 < notes.size() ? notes[j + 1].first : t0 + kBarTicks;
        const int64_t len = std::min<int64_t>(int64_t(spec_.arpGate * stepT), next - notes[j].first - 10);
        const float vel = (notes[j].first - t0) % kPPQ == 0 ? .9f : .7f;
        out.push_back({ notes[j].first, Event::NoteOn, Arp, uint8_t(notes[j].second), vel });
        out.push_back({ notes[j].first + len, Event::NoteOff, Arp, uint8_t(notes[j].second), 0 });
    }
}

void GenreComposer::generateBar(int bar, std::vector<Event>& out)
{
    if (bar < 0 || bar >= totalBars()) return;
    // this bar's breaks
    const float e = energyAt(bar);
    for (int b = 0; b < 4; ++b) chopMap_[size_t(b)] = b > 0 && rng_.chance(spec_.chop * e) ? rng_.range(0, 3) : b;
    gapBeat_ = rng_.chance(spec_.gap * e) ? rng_.range(1, 3) : -1;
    stutterBeat_ = rng_.chance(spec_.stutter * e) ? rng_.range(1, 3) : -1;
    if (stutterBeat_ == gapBeat_) stutterBeat_ = -1;

    emitAutomation(bar, out);
    emitPad(bar, out);
    emitPart(Pulse, spec_.pulse, bar, out);
    emitPart(Metal, spec_.metal, bar, out);
    emitPart(Tick, spec_.tick, bar, out);
    emitPart(Perc, spec_.perc, bar, out);
    emitPart(Bass, spec_.bass, bar, out);
    emitPart(Chord, spec_.chord, bar, out);
    if (spec_.hook) emitHook(bar, out);
    else emitArp(bar, out);
    emitPart(Sub, spec_.sub, bar, out);
    emitHarm(bar, out);
    emitPart(Ground, spec_.ground, bar, out);
    emitPart(Body, spec_.body, bar, out);
    emitPart(Mallet, spec_.mallet, bar, out);
    emitPart(Texture, spec_.texture, bar, out);
    emitImpact(bar, out);
    emitHeld(Fx, bar, out, fxHeld_, active(Fx, bar) ? foldInto(48 + tonicAt(bar), 48, 59) : -1);
}

std::string GenreComposer::describe() const
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double secs = totalBars() * 4 * 60.0 / bpm_;
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s · %s %s · %.0f bpm · %d bars · %d:%02d", spec_.title, names[tonicPc_], spec_.mode->name, bpm_,
                  totalBars(), int(secs) / 60, int(secs) % 60);
    return buf;
}

} // namespace liszt
