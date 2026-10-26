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
    static const char* letters = "PKSHCBUADF"; // in GTrack order
    uint16_t m = 0;
    for (; *s; ++s)
        if (const char* p = std::strchr(letters, *s)) m |= bit(int(p - letters));
    return m;
}

// ---------------------------------------------------------------- the genres
// Patterns are one bar of sixteenths unless a part says otherwise; a longer string runs over two bars.

const G::Spec kJungle {
    .scene = "jungle", .title = "Jungle",
    .bpmLo = 168, .bpmHi = 174, .mode = &kAeolian, .tonics = { 4, 5, 2, 7, 9 },
    .roots = { 0, 5, 3, 6, 2 }, .rootW = { .40, .20, .15, .15, .10 }, .preRoot = 6,
    .form = { { { "Mist", 16, .15f, "PF" }, { "Signal", 16, .35f, "PAHF" }, { "First Break", 64, .65f, "PKSHBUA" },
                { "Undertow", 32, .45f, "PBUAD" }, { "Second Break", 64, .85f, "PKSHBUAD" }, { "Void", 16, .30f, "PAF" },
                { "Ascension", 80, 1.0f, "PKSHBUADF" }, { "Rain", 32, .50f, "PKSHA" }, { "Tide", 16, .10f, "PF" } } },
    // the break: a kick on one and the and-of-three, snares on two and four with ghosts between
    .kick = part("X.........X.....", "X.X.......XX....X.X.......X.....", "X.X.......XX....X.X.......X..X.."),
    .snare = part("........X.......", "....X..g.g..X..g....X..g.g....X.", "....X..x.x..X..x....X..g.x..X.gx"),
    .hats = part("x.o.x.o.x.o.x.o.", "x.o.x.o.x.o.x.2.", "xgogxgogxgogxg2g"),
    .perc = none(),
    // the reese: whole bars, the fifth at the end of the phrase
    .bass = part("x---------------", "x-----------5---", "x-------x---5-8-"),
    .chord = part("", "x---------------................", "x---------------................"),
    .arpRate = { 4, 8, 8 }, .arpLenLo = 5, .arpLenHi = 7, .arpDensLo = .30f, .arpDensHi = .60f, .arpGate = 1.8f,
    .arpLo = 72, .arpHi = 96,
    .chop = .35f,
};

const G::Spec kFootwork {
    .scene = "footwork", .title = "Footwork",
    .bpmLo = 158, .bpmHi = 162, .mode = &kPhrygian, .tonics = { 2, 4, 5, 7, 9 },
    .roots = { 0, 1, 5, 3, 6 }, .rootW = { .40, .22, .16, .12, .10 }, .preRoot = 1,
    .form = { { { "Ritual", 16, .15f, "PC" }, { "Call", 32, .35f, "PCHD" }, { "Footwork", 48, .60f, "PKSHCBUD" },
                { "Origami", 32, .45f, "PCADF" }, { "Battle", 64, .85f, "PKSHCBUAD" }, { "Breath", 16, .30f, "PAF" },
                { "Black", 80, 1.0f, "PKSHCBUADF" }, { "Dust", 16, .15f, "PCF" } } },
    // triplet eighths for the kick and the 808, sixteenths for the hats, sextuplets for the toms
    .kick = part("X.....X.....", "X..x..X..x.x", "X..x.xX..x..X.xx.xX..x.x", 12),
    .snare = part("", "....x.......x...", "....x.......x..g"),
    .hats = part("x.x.x.x.x.x.x.x.", "xgxgxgxgxgxgxgxg", "xgxgxgx2xgxgxgxg"),
    .perc = part("1.......2.......3.......", "1.......2.....3.1.......", "1.2.3.......1.2.3...4.4.", 24),
    .bass = part("x-----......", "x-----..x-..", "x-----..5-..x--.x-..5---", 12),
    .chord = part("", "x..x..x.....", "x.xx.xx..x.x", 12),
    .arpRate = { 0, 12, 24 }, .arpLenLo = 5, .arpLenHi = 7, .arpDensLo = .40f, .arpDensHi = .70f, .arpGate = .6f,
    .arpLo = 60, .arpHi = 84, .percLo = 38, .percHi = 57,
    .chop = .20f,
};

const G::Spec kEcstatic {
    .scene = "ecstatic", .title = "Ecstatic Arpeggios",
    .bpmLo = 136, .bpmHi = 142, .mode = &kAeolian, .tonics = { 9, 2, 4, 7, 0 },
    .roots = { 0, 5, 2, 6, 3 }, .rootW = { .30, .25, .20, .15, .10 }, .preRoot = 6, // i VI III VII iv
    .form = { { { "Dawn", 16, .15f, "PA" }, { "Spiral", 32, .35f, "PA" }, { "Lattice", 48, .55f, "PADH" },
                { "Suspension", 32, .40f, "PAFS" }, { "Ecstasy", 64, .80f, "PADHSBU" }, { "Afterimage", 32, .45f, "PAS" },
                { "Computation", 64, 1.0f, "PKSHBUADF" }, { "Halo", 32, .20f, "PAD" } } },
    // no break here: the arpeggio is the rhythm; the kick only lands at the summit, the snare only rolls into it
    .kick = part("", "", "X...X...X...X..."),
    .snare = none(),
    .hats = part("", "..g...g...g...g.", "g.g.g.g.g.g.g.g."),
    .perc = none(),
    .bass = part("", "x-------x-------", "x-------5---8---"),
    // the chords gated pointillistically: the trance without its kick
    .chord = part("x-------........", "x.x.x..x.x.x..x.", "x.xx.xx.x.xx.xx."),
    .arpRate = { 16, 16, 32 }, .arpLenLo = 7, .arpLenHi = 11, .arpDensLo = .60f, .arpDensHi = 1.0f, .arpGate = .7f,
    .arpLo = 60, .arpHi = 96,
};

const G::Spec kDub {
    .scene = "dubtechno", .title = "Dub Techno",
    .bpmLo = 120, .bpmHi = 124, .mode = &kAeolian, .tonics = { 9, 2, 7, 0, 5 },
    .roots = { 0, 3, 5, 6, 4 }, .rootW = { .50, .20, .15, .10, .05 }, .preRoot = 3, .regionCalm = 16, .regionDriven = 16,
    .form = { { { "Fog", 16, .15f, "PD" }, { "Current", 32, .35f, "PKHD" }, { "Depth", 48, .55f, "PKSHBUD" },
                { "Haze", 32, .35f, "PDF" }, { "Pressure", 64, .80f, "PKSHBUDF" }, { "Echo Chamber", 32, .45f, "PDHA" },
                { "Submersion", 64, 1.0f, "PKSHBUADF" }, { "Silt", 16, .15f, "PD" } } },
    .kick = part("X...x...x...x...", "X...x...x...x...", "X...x...x...x..."),
    .snare = part("", "......2.......2.", "......2....2..2."), // rimshots
    .hats = part("..o...o...o...o.", "g.2.g.2.g.2.g.2.", "g.2gg.2gg.2gg.2g"),
    .perc = none(),
    .bass = part("x.......x.......", "..x...x...x...x.", "..x..xx...x..x5."),
    // the stab: a minor seventh, short, into a long echo; its filter breathes over minutes
    .chord = part("..x.............", "..x.....x.....x.", "..x..x....x..x.."),
    .arpRate = { 0, 0, 8 }, .arpLenLo = 5, .arpLenHi = 5, .arpDensLo = .25f, .arpDensHi = .35f, .arpGate = .5f, .arpLo = 67, .arpHi = 88,
    .chordSize = 4, .swing = .12f, .chordSweep = .55f,
};

const G::Spec kGlitch {
    .scene = "glitch", .title = "Clicks & Glitch",
    .bpmLo = 120, .bpmHi = 128, .mode = &kAeolian, .tonics = { 0, 2, 5, 7, 9 },
    .roots = { 0, 5, 3, 0, 6 }, .rootW = { .45, .20, .15, .10, .10 }, .preRoot = 5, .regionCalm = 16, .regionDriven = 16,
    .form = { { { "Data", 16, .15f, "C" }, { "Signal", 32, .35f, "CHA" }, { "Matrix", 48, .55f, "KCHAU" },
                { "Void", 16, .30f, "PA" }, { "Formula", 64, .80f, "KSHCBUA" }, { "Test Pattern", 32, .45f, "CHAD" },
                { "Supercodex", 64, 1.0f, "PKSHCBUADF" }, { "Zero", 16, .10f, "C" } } },
    .kick = part("X...............", "X.......X.......", "X..x....X..x..x."),
    .snare = part("", "", "....X.......X..."),
    .hats = part("", "g.g.g.g.g.g.g.g.g.g.g.g.g.g.g.g.", "gxgggxgggxgggxgggxgggxgggxgggxgg", 32),
    .perc = part("euclid", "euclid", "euclid", 32), // sine clicks, tuned to the chord, spread evenly
    .bass = part("", "x-------........", "x---....x---...."),
    .chord = part("", "", "x---------------................"),
    .arpRate = { 16, 20, 32 }, .arpLenLo = 9, .arpLenHi = 13, .arpDensLo = .30f, .arpDensHi = .70f, .arpGate = .5f, // twenty against sixteen
    .arpLo = 84, .arpHi = 108, .percLo = 60, .percHi = 95,
    .stutter = .30f, .stutterTrack = G::Perc, .stutterDiv = 64,
};

const G::Spec kGqom {
    .scene = "gqom", .title = "Gqom",
    .bpmLo = 118, .bpmHi = 124, .mode = &kPhrygian, .tonics = { 4, 5, 2, 7, 9 },
    .roots = { 0, 1, 5, 3, 6 }, .rootW = { .45, .20, .15, .10, .10 }, .preRoot = 1, .regionCalm = 16, .regionDriven = 16,
    .form = { { { "Taxi Rank", 16, .15f, "PC" }, { "Gathering", 32, .35f, "PCH" }, { "Gqom", 48, .60f, "PKCHSU" },
                { "Ghost", 32, .40f, "PCDF" }, { "Sgubhu", 64, .85f, "PKSHCBUD" }, { "Shadow", 16, .30f, "PF" },
                { "Possessed", 64, 1.0f, "PKSHCBUADF" }, { "Embers", 32, .30f, "PKC" } } },
    // no four on the floor: the kick in threes against the bar, the toms in triplets
    .kick = part("X..x..x.........", "X..x..x...x.....", "X..x..x...x.x...X..x..x...x..x.x"),
    .snare = part("", "........x.......", "........x.....g."),
    .hats = part("g.g.g.g.g.g.g.g.", "g.gxg.gxg.gxg.gx", "ggxgggxgggxgggx2"),
    .perc = part("1.....2.....1.....3.....", "1..2..3..1..2.....3..2..", "1..2..3..1..2..3..2..1.3", 24),
    .bass = part("", "x---............", "x---......x-----"),
    .chord = part("x...............................", "x.........x.....", "x.....x...x....."),
    .arpRate = { 0, 8, 16 }, .arpLenLo = 5, .arpLenHi = 7, .arpDensLo = .30f, .arpDensHi = .50f, .arpGate = .5f, .arpLo = 48, .arpHi = 72,
    .percLo = 38, .percHi = 57, .swing = .08f,
};

const G::Spec kDeconstructed {
    .scene = "deconstructed", .title = "Deconstructed Club",
    .bpmLo = 128, .bpmHi = 136, .mode = &kPhrygian, .tonics = { 4, 5, 2, 7, 9 },
    .roots = { 0, 1, 5, 3, 6 }, .rootW = { .35, .25, .15, .15, .10 }, .preRoot = 1,
    .form = { { { "Shatter", 16, .20f, "PDF" }, { "Glass", 32, .40f, "PCKD" }, { "Fracture", 48, .65f, "KSCBDU" },
                { "Cathedral", 32, .40f, "PDAF" }, { "Violence", 48, .85f, "PKSHCBUAD" }, { "Silence", 16, .15f, "P" },
                { "Rebirth", 64, 1.0f, "PKSHCBUADF" }, { "Wound", 32, .30f, "PDA" } } },
    .kick = part("X.......X..x....", "X..x.....X.x..x.", "X..x.X...X.xx..xX....x..X..x.X.."),
    .snare = part("", "....X.....g.X...", "....X..x..g.X.xX"),
    .hats = part("", "x...x.x...x...x.", "x.xxx.x.x.xxx.xx"),
    .perc = part("1.......3.......", "1..3..2...4..1..", "1.3.2.41.3.2.4.1"), // metal: clicks tuned to the chord
    .bass = part("x---------------", "x-------x---5---", "x--x--x-x---8-5-"),
    .chord = part("X---------------", "X.......X..X....", "X..X..X.X.X..X.."),
    .arpRate = { 0, 8, 16 }, .arpLenLo = 5, .arpLenHi = 9, .arpDensLo = .30f, .arpDensHi = .60f, .arpGate = .8f, .arpLo = 60, .arpHi = 90,
    .percLo = 60, .percHi = 90,
    .chop = .30f, .gap = .25f, .stutter = .20f, .stutterTrack = G::Snare, .stutterDiv = 32,
};

const G::Spec kHalftime {
    .scene = "halftime", .title = "Halftime",
    .bpmLo = 168, .bpmHi = 174, .mode = &kAeolian, .tonics = { 4, 5, 2, 7, 9 },
    .roots = { 0, 5, 3, 6, 4 }, .rootW = { .40, .20, .15, .15, .10 }, .preRoot = 6,
    .form = { { { "Smoke", 16, .15f, "PF" }, { "Weight", 32, .35f, "PHBU" }, { "Halftime", 64, .60f, "PKSHBUD" },
                { "Hollow", 32, .40f, "PAF" }, { "Mass", 64, .85f, "PKSHBUAD" }, { "Breath", 16, .25f, "PA" },
                { "Gravity", 64, 1.0f, "PKSHBUADF" }, { "Fall", 32, .30f, "PKSA" } } },
    // the snare on three: the tempo is 170, the body feels 85
    .kick = part("X...............", "X.........x.....", "X.........x...x.X......x..x....."),
    .snare = part("........X.......", "........X.......", "........X......g........X....g.g"),
    .hats = part("x...x...x...x...", "x.x.x.x.x.x.x.x.", "x.xgx.x.x.xgx.x."),
    .perc = none(),
    .bass = part("x---------------", "x-------..x-----", "x-----x-..x---5-"),
    .chord = part("", "x---------------................", "x---------------................"),
    .arpRate = { 0, 4, 8 }, .arpLenLo = 5, .arpLenHi = 7, .arpDensLo = .30f, .arpDensHi = .50f, .arpGate = 1.8f, .arpLo = 72, .arpHi = 96,
    .chop = .10f, .stutter = .25f, .stutterTrack = G::Hats, .stutterDiv = 32,
};

bool is(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

} // namespace

const std::vector<const G::Spec*>& GenreComposer::all()
{
    static const std::vector<const Spec*> v = { &kJungle, &kFootwork, &kEcstatic, &kDub, &kGlitch, &kGqom, &kDeconstructed, &kHalftime };
    return v;
}

const G::Spec* GenreComposer::find(const std::string& scene)
{
    for (auto* s : all()) if (scene == s->scene) return s;
    return nullptr;
}

const SceneInfo* genreSceneInfo(const std::string& scene)
{
    // The same ten tracks for every genre: the pad and the chords into the reverb, the arpeggio and
    // the chords into the echo, the low end dry.
    static const std::vector<SceneInfo> infos = [] {
        std::vector<SceneInfo> v;
        for (auto* s : G::all())
            v.push_back({ s->scene, 10, { "pad", "kick", "snare", "hats", "perc", "bass", "sub", "arp", "chord", "fx" },
                          { 0, 1, 2, 2, 2, 1, 1, 3, 0, 0 },
                          { { 0, .30f }, { 0, .02f }, { .05f, .12f }, { .03f, .08f }, { .10f, .15f }, { 0, .03f }, { 0, 0 }, { .20f, .20f }, { .20f, .25f }, { .10f, .35f } },
                          { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
                          { 1, 0, 0, 0, 0, 1, 0, 1, 1, 0 }, true });
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
    // One root per region, longer when calm. Each section starts at home; the one before the summit
    // ends on the chord that leads into it. The first and last sections stay home.
    rootOfBar_.assign(sectionOfBar_.size(), 0);
    for (size_t si = 1; si + 1 < sections_.size(); ++si)
    {
        auto& sec = sections_[si];
        const bool beforePeak = sections_[si + 1].restoreTheme;
        const int region = sec.energy < .5f ? spec_.regionCalm : spec_.regionDriven;
        int prev = 0;
        for (int r = 0; r * region < sec.bars; ++r)
        {
            int root = 0;
            if (r > 0) do root = spec_.roots[size_t(rng_.weighted(spec_.rootW))]; while (root == prev && root != 0);
            if (beforePeak && (r + 1) * region >= sec.bars) root = spec_.preRoot;
            for (int b = 0; b < region && r * region + b < sec.bars; ++b) rootOfBar_[size_t(sec.startBar + r * region + b)] = root;
            prev = root;
        }
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
    return (sectionAt(bar).layers & bit(track)) != 0;
}

std::array<int, 2> GenreComposer::range(int track) const
{
    switch (track)
    {
        case Pad: return { 36, 76 };
        case Kick: return { 24, 35 };
        case Snare: return { 50, 62 };
        case Hats: return { 42, 46 };
        case Perc: return { spec_.percLo, spec_.percHi };
        case Bass: return { 40, 64 };
        case Sub: return { 22, 40 };
        case Arp: return { spec_.arpLo, spec_.arpHi };
        case Chord: return { 46, 76 };
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
    const float fade = track == Pad || track == Fx || track == Sub || track == Chord ? 4.0f : 2.0f;
    const float fadeIn = active(track, bar - in - 1) || (bar == 0 && track != Pad) ? 1.0f : std::min(1.0f, (in + 1) / fade);
    const float fadeOut = active(track, bar + left) ? 1.0f : std::min(1.0f, left / fade);
    return std::min(fadeIn, fadeOut);
}

int GenreComposer::pitchOf(int bar, int deg, int lo, int hi, int base) const
{
    return foldInto(base + tonicPc_ + degreeSemis(*spec_.mode, rootAt(bar) + deg), lo, hi);
}

int GenreComposer::ladderPitch(int bar, int idx, int lo, int hi) const
{
    int p = 12 * (lo / 12) + tonicPc_ + degreeSemis(*spec_.mode, rootAt(bar) + kLadder[idx % 7]);
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
        // root, fifth and octave; the third two octaves up once the music is driven
        const auto& m = *spec_.mode;
        const int r = rootAt(bar), lo = pitchOf(bar, 0, 36, 47, 36);
        want = { lo, lo + degreeSemis(m, r + 4) - degreeSemis(m, r), lo + 12 };
        if (energyAt(bar) > .5f) want.push_back(lo + 24 + degreeSemis(m, r + 2) - degreeSemis(m, r));
    }
    for (int p : padHeld_)
        if (std::find(want.begin(), want.end(), p) == want.end()) out.push_back({ t0, Event::NoteOff, Pad, uint8_t(p), 0 });
    for (int p : want)
        if (std::find(padHeld_.begin(), padHeld_.end(), p) == padHeld_.end()) out.push_back({ t0, Event::NoteOn, Pad, uint8_t(p), .75f });
    padHeld_ = want;
    if (bar + 1 == totalBars())
    {
        for (int p : padHeld_) out.push_back({ t0 + kBarTicks, Event::NoteOff, Pad, uint8_t(p), 0 });
        padHeld_.clear();
    }
}

void GenreComposer::emitPart(int track, const Part& part, int bar, std::vector<Event>& out)
{
    if (!active(track, bar)) return;
    const int64_t t0 = int64_t(bar) * kBarTicks;
    auto& sec = sectionAt(bar);
    const bool lastBar = bar + 1 == sec.startBar + sec.bars;
    const bool drum = track == Kick || track == Snare || track == Hats || track == Perc;
    // before the summit the drums, the bass and the chords hold their breath for half a bar
    const bool drop = lastBar && bar + 1 < totalBars() && sectionAt(bar + 1).restoreTheme;
    const bool fill = track == Snare && lastBar && bar + 1 < totalBars() && sectionAt(bar + 1).energy > sec.energy;
    auto hit = [&](int64_t t, int pitch, float vel, int64_t len) {
        out.push_back({ t, Event::NoteOn, uint8_t(track), uint8_t(pitch), std::clamp(vel, .05f, 1.0f) });
        out.push_back({ t + len, Event::NoteOff, uint8_t(track), uint8_t(pitch), 0 });
    };
    auto drumPitch = [&](char c, int step) {
        const int alt = c >= '1' && c <= '9' ? c - '1' : 0;
        switch (track)
        {
            case Kick: return foldInto(24 + tonicPc_, 24, 35);
            case Snare: return alt == 1 ? 62 : 50;  // 2: the rim
            case Hats: return alt == 1 ? 46 : 42;   // 2: open
            default: return ladderPitch(bar, c == 'e' ? step % 5 : alt, spec_.percLo, spec_.percHi);
        }
    };

    if (fill) // the roll into the next section: sixteenths over the last two beats, louder all the way
        for (int i = 8; i < 16; ++i) hit(t0 + i * kSixteenth, 50, .35f + .08f * (i - 8), kSixteenth - 10);

    const Pat& pat = part.tier[size_t(tier(bar))];
    const int len = int(std::strlen(pat.s));
    if (len == 0) return;
    const int P = pat.perBar;
    const int64_t stepT = kBarTicks / P;
    const bool euclid = is(pat.s, "euclid");
    if (euclid && bar == sec.startBar) percRot_ = rng_.range(0, P - 1);
    const int k = euclid ? 3 + int(energyAt(bar) * P * .45f) : 0;

    for (int i = 0; i < P; ++i)
    {
        const int beat = i * 4 / P;
        if (fill && beat >= 2) break;
        if (drop && beat >= 2 && track != Pad) break;
        if (gapBeat_ == beat && track != Pad) continue;
        if (stutterBeat_ == beat && track == spec_.stutterTrack) continue;
        const int src = drum ? i + (chopMap_[size_t(beat)] - beat) * P / 4 : i;
        const int at = int((int64_t(bar) * P + src) % len);
        const char c = euclid ? (((i + percRot_) * k) % P < k ? 'e' : '.') : pat.s[at];
        if (c == '.' || c == '-') continue;
        int holds = 0;
        if (!drum) while (holds + 1 < len && pat.s[(at + holds + 1) % len] == '-') ++holds;
        const float vel = c == 'X' ? 1.0f : c == 'x' || c == 'e' ? .85f : c == 'o' ? .65f : c == 'g' ? .38f : .8f;
        int64_t t = t0 + i * stepT;
        if (P == 16 && (i & 1)) t += int64_t(spec_.swing * kSixteenth);
        if (drum) { hit(t, drumPitch(c, i), vel, std::min<int64_t>(stepT / 2, kPPQ - 20)); continue; } // short: the swing must not reach the next hit
        const int64_t noteLen = int64_t(double(stepT * (1 + holds)) * (holds ? .95 : .7));
        if (track == Bass)
        {
            const int deg = c >= '1' && c <= '9' ? c - '1' : 0;
            const auto& m = *spec_.mode;
            const int r = rootAt(bar), root = pitchOf(bar, 0, 40, 51, 36); // written an octave up: the bass patches sound an octave down
            hit(t, root + degreeSemis(m, r + deg) - degreeSemis(m, r), vel, noteLen);
        }
        else // the chord: root, third, fifth (and seventh)
        {
            const auto& m = *spec_.mode;
            const int r = rootAt(bar), root = pitchOf(bar, 0, 48, 59, 48);
            for (int d = 0; d < spec_.chordSize; ++d) hit(t, root + degreeSemis(m, r + 2 * d) - degreeSemis(m, r), vel, noteLen);
        }
    }

    if (stutterBeat_ >= 0 && track == spec_.stutterTrack && !(drop && stutterBeat_ >= 2) && !(fill && stutterBeat_ >= 2))
    {
        // a stutter: one beat broken into thirty-seconds (or sixty-fourths), swelling
        const int n = spec_.stutterDiv / 4;
        const int64_t d = kBarTicks / spec_.stutterDiv;
        for (int j = 0; j < n; ++j)
            hit(t0 + stutterBeat_ * kPPQ + j * d, drumPitch(track == Perc ? 'e' : 'x', j), .3f + .6f * float(j) / float(n), d - 8);
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
    emitPart(Kick, spec_.kick, bar, out);
    emitPart(Snare, spec_.snare, bar, out);
    emitPart(Hats, spec_.hats, bar, out);
    emitPart(Perc, spec_.perc, bar, out);
    emitPart(Bass, spec_.bass, bar, out);
    emitPart(Chord, spec_.chord, bar, out);
    emitArp(bar, out);
    emitHeld(Sub, bar, out, subHeld_, active(Sub, bar) ? pitchOf(bar, 0, 24, 35, 24) : -1);
    emitHeld(Fx, bar, out, fxHeld_, active(Fx, bar) ? foldInto(48 + tonicPc_, 48, 59) : -1);
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
