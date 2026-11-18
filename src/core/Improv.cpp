#include "Improv.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <deque>
#include <map>
#include <sstream>

namespace liszt {

namespace {

using G = GenreComposer;

// dark modes only (Kerem: "dark, sublime ... not childish or too happy")
const Mode* const kModes[] = { &kAeolian, &kDorian, &kPhrygian, &kHarmonicMinor, &kMelodicMinor, &kPhrygianDominant, &kHungarianMinor };
const char* const kNotes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* const kSummits[] = { "theme", "unison", "pedal", "halftime", "new theme" };
const char* const kRiffs[] = { "none", "doom", "chug", "tremolo", "sludge", "drone", "stoner" };

// titles: two words, dark (the page shows them; Kerem renames what he keeps)
const char* const kWordA[] = { "Ash", "Iron", "Salt", "Black", "Hollow", "Lead", "Night", "Cinder", "Glass", "Bone", "Sodium", "Tar",
                               "Ember", "Frost", "Grave", "Smoke", "Rust", "Silent", "Burnt", "Low" };
const char* const kWordB[] = { "Cathedral", "Engine", "River", "Liturgy", "Furnace", "Harbour", "Choir", "Orbit", "Tide", "Vigil",
                               "Machine", "Garden", "Signal", "Procession", "Lantern", "Psalm", "Weather", "Ritual", "Meridian", "Hymn" };

std::deque<std::string> gNames; // the titles the specs point at (a deque: its strings never move)

int fifthOf(const Mode& m) { return m.steps[4]; }

// the old song's last chord: the tonic triad, its seventh and its ninth, as the pad voices it, and the drone's notes
uint32_t finalChord(const ImprovSong& s)
{
    uint32_t set = 0;
    for (int d : { 0, 2, 4, 6, 8 }) set |= 1u << ((s.tonic + degreeSemis(*s.mode, d)) % 12);
    return set;
}
// the new song's drone in its Seed: root, fifth, octave, and the ninth where it is a whole step (emitDrone)
uint32_t seedDrone(int tonic, const Mode& m)
{
    uint32_t set = 1u << tonic | 1u << ((tonic + fifthOf(m)) % 12);
    if (m.steps[1] == 2) set |= 1u << ((tonic + 2) % 12);
    return set;
}
const char* moveName(int d)
{
    switch (d)
    {
    case 0: return "same tonic, new mode";
    case 7: return "up a fifth";
    case 5: return "down a fifth";
    case 3: return "to the relative";
    case 10: return "down a whole step";
    case 2: return "up a whole step";
    case 8: return "down a major third";
    case 9: return "down a minor third";
    case 4: return "up a major third";
    default: return "pivot";
    }
}

// Each part's level in each genre song's mix at its summit (dB against the whole mix; N: silent there), measured by
// tools/track_levels.py (2026-11-14), in GenreComposer::all() order, tracks in GTrack order. A borrowed part is moved
// from its donor's balance to the target: the songs' median (Kerem, 2026-11-14: "this high pitched sound rhythm is too
// loud compared to the other synths" - the ticks came from Deconstructed Club, a click-heavy song); the ticks lower still.
constexpr double N = 99;
constexpr double kPartLevel[8][20] = {
    { -4.1, N, -19.7, N, N, -2.7, -6.6, -8.8, N, -17.4, -14.0, -15.4, -17.4, N, -25.2, -14.7, 0.3, -2.8, N, -8.7 }, // stoner
    { -14.4, N, -20.7, -40.3, -19.5, -14.5, -7.3, -10.4, -12.8, -28.9, -9.8, -19.1, -19.1, -20.4, -28.6, -20.3, -20.0, -13.4, N, -9.9 }, // jungle
    { -17.8, N, -19.2, -26.9, -20.2, -17.9, -16.0, -15.0, N, -29.7, -16.7, -19.8, -20.6, -21.9, -29.0, -17.6, -20.9, N, -12.5, -9.2 }, // ecstatic
    { -14.6, N, -17.7, -24.7, -18.4, -15.0, -14.5, -8.9, -10.8, -28.6, -18.3, -18.8, -19.0, -19.3, -27.6, -16.2, N, -15.9, N, -10.2 }, // noir
    { -20.2, N, -22.3, -25.1, -20.9, -18.7, -19.0, -13.2, -20.0, -27.8, -18.2, -21.8, -22.8, -21.5, -31.4, -15.8, -23.9, N, -11.6, -10.4 }, // deconstructed
    { -8.2, N, -18.9, -38.0, -43.9, -15.4, -16.3, N, N, N, -16.0, -18.9, -12.3, -23.6, -28.9, -11.5, -17.4, -16.0, N, -10.3 }, // halftime
    { -2.7, N, -16.0, N, -18.8, -0.6, -5.1, -21.5, -24.4, -19.3, -12.9, -17.7, -18.1, -26.8, -25.5, -14.6, 2.3, -7.4, -1.7, -8.5 }, // doom
    { -13.4, N, -18.3, N, N, -8.4, -6.0, -38.8, -7.1, -21.4, -13.8, -41.8, -45.3, N, -36.8, -13.4, -7.9, N, -10.8, -7.0 }, // blackgaze
};
constexpr double kPartTarget[20] = { -13.9, N, -19.0, -30.0, -19.9, -14.8, -10.9, -13.2, -12.8, -27.8, -15.0, -19.0, -19.1, -21.7, -28.8, -15.2, -17.4, -13.4, -11.2, -9.6 };
// how far a part borrowed from song d is moved, at most 12 dB either way
double partMove(int d, int t)
{
    const double have = kPartLevel[size_t(d)][size_t(t)], want = kPartTarget[size_t(t)];
    if (have == N || want == N) return 0;
    return std::max(-12.0, std::min(12.0, want - have));
}

// the sound donors each track may borrow from: a song's instrument for that track only where it plays that role, and
// only for a part its own form plays (a song keeps a silent placeholder, internal/none, on the parts it never uses:
// Blackgaze's drone, Ecstatic's chords; borrowed, the Seed's drone was silence, 2026-11-14)
bool uses(const G::Spec& d, int track)
{
    static const char* letters = "PKSHCBUADF_GYMTIRW"; // GTrack order, as Genres.cpp layersOf
    const int lt = track == G::Harm ? G::Pad : track;   // the choir sings where the pad plays
    if (lt >= int(std::strlen(letters)) || letters[lt] == '_') return true;
    for (auto& s : d.form) if (s.name && std::strchr(s.layers, letters[lt])) return true;
    return false;
}
bool canLend(const G::Spec& d, int track)
{
    if (!uses(d, track)) return false;
    switch (track)
    {
    case G::Drone: return d.lead != G::Drone;                     // Liquid's and the Curse's drone track is a singer/saxophone
    case G::Riff: return d.riff != G::RiffStyle::None;
    case G::Bass: return !d.walk;                                 // the contrabass only for a walk (below)
    case G::Lead: return d.lead == G::Lead;
    case G::Arp: return d.lead != G::Arp;                         // Noir's arp track is its saxophone (the soloist below)
    default: return true;
    }
}

struct Plan
{
    ImprovSong s;
    double score = -1;
};

double dimsOf(const ImprovSong& a, const ImprovSong& b, std::vector<std::pair<std::string, double>>* out)
{
    // each dimension 0 (the same) .. 1 (as different as it gets)
    int instr = 0, n = 0;
    for (int t = 0; t < G::kGenreTracks; ++t) { ++n; instr += a.sound[size_t(t)] != b.sound[size_t(t)]; }
    const std::pair<std::string, double> d[] = {
        { "tempo", std::min(1.0, std::abs(a.bpm - b.bpm) / (0.3 * std::max(a.bpm, b.bpm))) },
        { "key", a.tonic != b.tonic ? 1.0 : 0.0 },
        { "mode", a.mode != b.mode ? 1.0 : 0.0 },
        { "genre form", a.form != b.form ? 1.0 : 0.0 },
        { "rhythm", a.rhythm != b.rhythm ? 1.0 : 0.0 },
        { "bass", a.bass != b.bass || a.walk != b.walk ? 1.0 : 0.0 },
        { "harmony", a.harmony != b.harmony ? 1.0 : 0.0 },
        { "melody", a.melody != b.melody ? 1.0 : 0.0 },
        { "riff", a.spec.riff != b.spec.riff ? 1.0 : 0.0 },
        { "summit", a.spec.summit != b.spec.summit ? 1.0 : 0.0 },
        { "instruments", double(instr) / n },
        { "space", a.space != b.space ? 1.0 : 0.0 },
        { "length", std::min(1.0, std::abs(a.seconds - b.seconds) / 60.0) },
    };
    double sum = 0;
    for (auto& x : d) sum += x.second;
    if (out) out->assign(std::begin(d), std::end(d));
    return sum / double(std::size(d));
}

ImprovSong candidate(Rng& rng, int k)
{
    const auto& all = G::all();
    const int N = int(all.size());
    ImprovSong s;
    s.k = k;
    s.form = rng.range(0, N - 1);
    s.rhythm = rng.range(0, N - 1);
    s.bass = rng.range(0, N - 1);
    s.harmony = rng.range(0, N - 1);
    s.melody = rng.range(0, N - 1);
    do s.riff = rng.range(0, N - 1); while (all[size_t(s.riff)]->riff == G::RiffStyle::None && rng.chance(.8));
    s.mode = kModes[rng.range(0, int(std::size(kModes)) - 1)];
    s.tonic = rng.range(0, 11);
    s.bpm = 2 * rng.range(35, 86);                 // 70..172
    s.walk = all[size_t(s.bass)]->walk || rng.chance(.25);
    s.space = rng.range(0, N - 1);

    const G::Spec& F = *all[size_t(s.form)];
    const G::Spec& R = *all[size_t(s.rhythm)];
    const G::Spec& B = *all[size_t(s.bass)];
    const G::Spec& H = *all[size_t(s.harmony)];
    const G::Spec& M = *all[size_t(s.melody)];
    const G::Spec& Q = *all[size_t(s.riff)];
    G::Spec& p = s.spec;
    p = F;                                          // form, builds, end figures, and the defaults
    p.scene = "improv";
    p.quietEdges = true;
    p.bpmLo = p.bpmHi = s.bpm;
    p.mode = s.mode;
    p.tonics = { s.tonic, s.tonic, s.tonic, s.tonic, s.tonic };
    // percussion
    p.pulse = R.pulse; p.metal = R.metal; p.tick = R.tick; p.perc = R.perc;
    p.ground = R.ground; p.body = R.body; p.mallet = R.mallet; p.texture = R.texture;
    p.chop = R.chop; p.gap = R.gap; p.stutter = R.stutter; p.stutterTrack = R.stutterTrack; p.stutterDiv = R.stutterDiv;
    p.swing = R.swing; p.percLo = R.percLo; p.percHi = R.percHi;
    // the low end: a groove from the bass donor, or a walk
    p.bass = B.bass; p.sub = B.sub; p.walk = s.walk;
    // harmony
    p.progs = H.progs; p.preRoot = H.preRoot; p.ext = H.ext; p.chord = H.chord; p.chordSweep = H.chordSweep;
    p.gaze = H.gaze; p.harmLo = H.harmLo; p.harmHi = H.harmHi; p.pedal = H.pedal;
    // melody: the hook or the arpeggio
    p.arpRate = M.arpRate; p.arpLenLo = M.arpLenLo; p.arpLenHi = M.arpLenHi; p.arpDensLo = M.arpDensLo; p.arpDensHi = M.arpDensHi;
    p.arpGate = M.arpGate; p.arpLo = M.arpLo; p.arpHi = M.arpHi; p.hook = M.hook; p.arpMorph = M.arpMorph;
    // the riff
    p.riff = Q.riff; p.riffLo = Q.riffLo; p.legato = Q.legato;
    p.summit = G::Summit(rng.range(0, 4));

    // each track's instrument: any song that plays that role on it
    for (int t = 0; t < G::kGenreTracks; ++t)
    {
        int d;
        int guard = 0;
        do d = rng.range(0, N - 1); while (!canLend(*all[size_t(d)], t) && ++guard < 64);
        s.sound[size_t(t)] = d;
    }
    if (s.walk)
    {   // the contrabass is Noir's
        for (int d = 0; d < N; ++d) if (all[size_t(d)]->walk) s.sound[G::Bass] = d;
    }
    // the soloist: its own lead track (an instrument written for it), or, now and then, Noir's saxophone on the arp
    const G::Spec* sax = nullptr;
    for (auto* d : all) if (d->lead == G::Arp) sax = d;
    if (sax && rng.chance(.2))
    {
        s.sound[G::Arp] = int(std::find(all.begin(), all.end(), sax) - all.begin());
        p.lead = G::Arp; p.sing = sax->sing; p.arpLo = sax->arpLo; p.arpHi = sax->arpHi;
    }
    else
    {
        const G::Spec& L = *all[size_t(s.sound[G::Lead])];
        p.lead = G::Lead; p.sing = false; p.leadLo = L.leadLo; p.leadHi = L.leadHi;
    }

    // the form: the donor's six parts fitted to a length of 3:20 to 4:45, a Seed before and a Release after
    const double barSec = 240.0 / s.bpm;
    const int edge = std::max(4, int(std::lround(30.0 / barSec / 4)) * 4); // about 30 s each: the changeover (Kerem 2026-11-18: "30 seconds or so")
    const int want = int(rng.range(200, 285) / barSec);
    int donorBars = 0;
    for (auto& t : F.form) if (t.name) donorBars += t.bars;
    const double f = double(want - 2 * edge) / donorBars;
    std::array<G::Sec, 10> form {};
    int n = 0, total = 2 * edge;
    form[size_t(n++)] = { "Seed", edge, .08f, "W" };
    for (auto& t : F.form)
    {
        if (!t.name) break;
        const int b = std::max(8, int(std::lround(t.bars * f / 4)) * 4);
        form[size_t(n++)] = { t.name, b, t.energy, t.layers };
        total += b;
    }
    // never past five minutes (the engine's tail of a few seconds included)
    while (total * barSec > 290)
    {
        auto big = std::max_element(form.begin() + 1, form.begin() + n, [](auto& a, auto& b) { return a.bars < b.bars; });
        big->bars -= 4;
        total -= 4;
    }
    form[size_t(n++)] = { "Release", edge, .08f, "PW" };
    p.form = form;
    s.bars = total;
    s.seconds = int(total * barSec);
    return s;
}

// the hard rules: what must change from the song before, and the pivot into it
bool fits(const ImprovSong& c, const ImprovSong* prev, int relax)
{
    if (!prev) return true;
    const auto& all = G::all();
    if (std::abs(c.bpm - prev->bpm) < 0.15 * std::max(c.bpm, prev->bpm)) return false;
    if (c.mode == prev->mode) return false;
    if (c.form == prev->form || c.rhythm == prev->rhythm || c.harmony == prev->harmony || c.melody == prev->melody) return false;
    if (c.bass == prev->bass && c.walk == prev->walk) return false;
    if (c.walk && prev->walk) return false;
    if (c.spec.riff == prev->spec.riff || c.spec.summit == prev->spec.summit) return false;
    if (all[size_t(c.form)] == all[size_t(prev->form)]) return false;
    int same = 0;
    for (int t = 0; t < G::kGenreTracks; ++t) same += c.sound[size_t(t)] == prev->sound[size_t(t)];
    if (same > G::kGenreTracks / 4) return false;
    if (c.tonic == prev->tonic) return false;
    // harmonic mixing: each note of the new drone is a tone of the old final chord, or a note of the old scale that
    // no chord tone sits a semitone from (a ninth, an eleventh: colour, never a rub)
    const uint32_t chord = finalChord(*prev), drone = seedDrone(c.tonic, *c.mode);
    for (int pc = 0; pc < 12; ++pc)
    {
        if (!(drone >> pc & 1) || (chord >> pc & 1)) continue;
        if (relax < 2 && !inMode(*prev->mode, prev->tonic, pc)) return false;
        if (relax < 1 && (chord >> ((pc + 1) % 12) & 1) || (chord >> ((pc + 11) % 12) & 1)) return false;
    }
    return true;
}

} // namespace

bool improvScene(const std::string& scene, int& k)
{
    if (scene.rfind("improv/", 0) != 0) return false;
    k = std::max(0, std::atoi(scene.c_str() + 7));
    return true;
}

const ImprovSong& improvSong(uint64_t seed, int k)
{
    static std::map<uint64_t, std::deque<ImprovSong>> chains;
    auto& chain = chains[seed];
    while (int(chain.size()) <= k)
    {
        const int i = int(chain.size());
        const ImprovSong* prev = i > 0 ? &chain[size_t(i - 1)] : nullptr;
        const ImprovSong* prev2 = i > 1 ? &chain[size_t(i - 2)] : nullptr;
        Rng rng(seed * 0x9E3779B97F4A7C15ull + uint64_t(i) * 0xD1B54A32D192ED03ull + 11);
        Plan best;
        // the strictest pivot first; only if no candidate keeps it, a looser one (the rub allowed, then any note)
        for (int relax = 0, tries = 0; relax < 3 && best.score < 0; ++relax)
          for (tries = 0; tries < 20000 && (best.score < 0 || tries < 1500); ++tries)
        {
            ImprovSong c = candidate(rng, i);
            if (!fits(c, prev, relax) || (prev2 && c.mode == prev2->mode && relax == 0)) continue;
            // furthest from the song before, and not back to the one before that
            const double score = (prev ? dimsOf(c, *prev, nullptr) : 1.0) + (prev2 ? 0.5 * dimsOf(c, *prev2, nullptr) : 0.0);
            if (score > best.score) { best.s = c; best.score = score; }
        }
        ImprovSong s = best.score >= 0 ? best.s : candidate(rng, i); // (never met: thousands of candidates always hold one)
        if (prev)
        {
            s.distance = dimsOf(s, *prev, &s.dims);
            s.move = moveName((s.tonic - prev->tonic + 12) % 12);
        }
        else s.move = "first";
        std::string title;
        do title = std::string(kWordA[rng.range(0, int(std::size(kWordA)) - 1)]) + " " + kWordB[rng.range(0, int(std::size(kWordB)) - 1)];
        while (std::any_of(chain.begin(), chain.end(), [&](const ImprovSong& o) { return o.name == title; }));
        gNames.push_back(title);
        s.name = gNames.back();
        s.spec.title = gNames.back().c_str();
        chain.push_back(s);
    }
    return chain[size_t(k)];
}

std::string improvPalette(const ImprovSong& s, const std::function<std::string(const std::string&)>& read,
                          const std::function<bool(const std::string&)>& installed)
{
    // A track's lines are the ones that name it: its patches ("pad 0 400 Cat/Patch") and its settings ("gain pad 3").
    // Each donor's levels were set under its own master: a track keeps its loudness here by carrying the
    // difference between its donor's master and this song's (the space donor's).
    const auto& all = G::all();
    const auto& info = *genreSceneInfo(all[0]->scene);
    auto lines = [&](int d) {
        std::vector<std::string> v;
        std::istringstream in(read(all[size_t(d)]->scene));
        for (std::string l; std::getline(in, l);)
        {
            if (auto h = l.find('#'); h != std::string::npos) l.erase(h);
            while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.pop_back();
            if (!l.empty()) v.push_back(l);
        }
        return v;
    };
    auto tok = [](const std::string& l, int i) {
        std::istringstream in(l);
        std::string w;
        for (int k = 0; k <= i && (in >> w); ++k) if (k == i) return w;
        return std::string();
    };
    auto masterOf = [&](int d) {
        for (auto& l : lines(d)) if (tok(l, 0) == "master") return std::atof(tok(l, 1).c_str());
        return 8.0;
    };
    const double master = masterOf(s.space);
    std::ostringstream out;
    out << "# Improvisation " << s.k << ": " << s.name << " (generated, tools: src/core/Improv.cpp)\n";
    for (auto& l : lines(s.space))
    {
        const auto w = tok(l, 0);
        if (w == "shimmer" || w == "return" || w == "feedback" || w == "master" || (w == "send" && tok(l, 1) == "all")) out << l << "\n";
    }
    static const char* perTrack[] = { "gain", "delay", "drive", "crush", "comp", "duck", "lowcut", "amp", "release", "feed", "send" };
    // a donor that has, for this track, a patch that sounds here: its patch lines are "<track> gain bright <patch...>"
    auto sounds = [&](int d, const std::string& name) {
        for (auto& l : lines(d))
        {
            if (tok(l, 0) != name) continue;
            std::istringstream in(l);
            std::string a, b, c, patch;
            in >> a >> b >> c;
            std::getline(in, patch);
            patch.erase(0, patch.find_first_not_of(" \t"));
            if (!installed || installed(patch)) return true;
        }
        return false;
    };
    for (int t = 0; t < G::kGenreTracks; ++t)
    {
        const std::string name = info.trackNames[t];
        int d = s.sound[size_t(t)];
        std::string note;
        if (!sounds(d, name))
        {   // the planned instrument is not installed here: the same part from another song (one that plays it, then any)
            int alt = -1;
            for (int pass = 0; pass < 2 && alt < 0; ++pass)
                for (int k = 1; k < int(all.size()) && alt < 0; ++k)
                {
                    const int c = (d + k) % int(all.size());
                    if ((pass == 1 || canLend(*all[size_t(c)], t)) && sounds(c, name)) alt = c;
                }
            if (alt < 0)
            {   // nothing of it installed anywhere: one of the engine's own sounds, so the song still plays
                const char* own = t == G::Bass || t == G::Sub ? "internal/808"
                                : t == G::Metal || t == G::Perc || t == G::Tick || t == G::Ground || t == G::Body || t == G::Impact || t == G::Mallet ? "internal/metal"
                                : t == G::Pulse ? "internal/none" : "internal/blip";
                out << "# " << name << ": " << all[size_t(d)]->scene << "'s is not installed here, nor any song's: the engine's own\n"
                    << name << " 0.0 1000 " << own << "\n";
                continue;
            }
            note = std::string(" (") + all[size_t(d)]->scene + "'s is not installed here)";
            d = alt;
        }
        const double shift = masterOf(d) - master + partMove(d, t);
        bool gain = false;
        out << "# " << name << " from " << all[size_t(d)]->scene << note << "\n";
        for (auto& l : lines(d))
        {
            const auto w = tok(l, 0);
            if (w == name) { out << l << "\n"; continue; }
            if (std::find(std::begin(perTrack), std::end(perTrack), w) == std::end(perTrack) || tok(l, 1) != name) continue;
            if (w == "gain")
            {
                char buf[96];
                std::snprintf(buf, sizeof buf, "gain %s %.1f", name.c_str(), std::atof(tok(l, 2).c_str()) + shift);
                out << buf << "\n";
                gain = true;
            }
            else out << l << "\n";
        }
        if (!gain && std::abs(shift) > 0.05)
        {
            char buf[96];
            std::snprintf(buf, sizeof buf, "gain %s %.1f", name.c_str(), shift);
            out << buf << "\n";
        }
    }
    return out.str();
}

std::string improvJson(const ImprovSong& s)
{
    const auto& all = G::all();
    const auto& info = *genreSceneInfo(all[0]->scene);
    std::ostringstream o;
    auto q = [](const std::string& x) { return "\"" + x + "\""; };
    o << "{\"k\":" << s.k << ",\"name\":" << q(s.name) << ",\"bpm\":" << s.bpm << ",\"key\":" << q(std::string(kNotes[s.tonic]) + " " + s.mode->name)
      << ",\"tonic\":" << s.tonic << ",\"seconds\":" << s.seconds << ",\"bars\":" << s.bars << ",\"move\":" << q(s.move)
      << ",\"form\":" << q(all[size_t(s.form)]->scene) << ",\"rhythm\":" << q(all[size_t(s.rhythm)]->scene)
      << ",\"bass\":" << q(s.walk ? "walk" : all[size_t(s.bass)]->scene) << ",\"harmony\":" << q(all[size_t(s.harmony)]->scene)
      << ",\"melody\":" << q(all[size_t(s.melody)]->scene) << ",\"riff\":" << q(kRiffs[int(s.spec.riff)])
      << ",\"summit\":" << q(kSummits[int(s.spec.summit)]) << ",\"space\":" << q(all[size_t(s.space)]->scene)
      << ",\"soloist\":" << q(s.spec.lead == G::Arp ? "saxophone (arp)" : std::string("lead from ") + all[size_t(s.sound[G::Lead])]->scene)
      << ",\"distance\":" << s.distance << ",\"dims\":{";
    for (size_t i = 0; i < s.dims.size(); ++i) o << (i ? "," : "") << q(s.dims[i].first) << ":" << s.dims[i].second;
    o << "},\"sounds\":{";
    for (int t = 0; t < G::kGenreTracks; ++t) o << (t ? "," : "") << q(info.trackNames[t]) << ":" << q(all[size_t(s.sound[size_t(t)])]->scene);
    o << "},\"sections\":[";
    int i = 0;
    for (auto& t : s.spec.form) if (t.name) o << (i++ ? "," : "") << "[" << q(t.name) << "," << t.bars << "," << t.energy << "]";
    o << "]}";
    return o.str();
}

} // namespace liszt
