#include "Genres.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace liszt {

namespace {

using G = GenreComposer;
int up(int p, int lo) { while (p < lo) p += 12; return p; }
constexpr uint32_t bit(int t) { return 1u << t; }
constexpr double kTwoPi = 6.283185307179586;
// The chord tones the arpeggio and the tuned percussion walk on, in degrees above the root:
// root, third, fifth, octave, tenth, twelfth, fifteenth.
constexpr int kLadder[] = { 0, 2, 4, 7, 9, 11, 14 };

G::Part part(const char* low, const char* mid, const char* high, int perBar = 16) { return { { { low, perBar }, { mid, perBar }, { high, perBar } } }; }
G::Part none() { return part("", "", ""); }

uint32_t layersOf(const char* s)
{
    static const char* letters = "PKSHCBUADF_GYMTIRW"; // in GTrack order (the harm has no letter: it follows the pad)
    uint32_t m = 0;
    for (; *s; ++s)
        if (const char* p = std::strchr(letters, *s)) m |= bit(int(p - letters));
    return m;
}

// ---------------------------------------------------------------- the genres
// Patterns are one bar of sixteenths unless a part says otherwise; a longer string runs over two bars.

const G::Spec kJungle {
    .scene = "jungle", .title = "Liquid",
    // liquid drum and bass (2026-10-30, Kerem: "like a liquid drum & bass song"): warm minor ninths
    // (dorian: the major fourth chord's soul), Rhodes on the two-step's off-beats, a rolling bass, the
    // choir; the guitar's long chords only where it opens out. A little under the floor's 174 so the
    // live kit can roll over it.
    // 2026-11-25 (Kerem: "make liquid a little bit more dark sounding and add some wobble bass"): natural minor (the
    // flat sixth: the fourth chord minor), the same progressions; the 303's filter wobbles in the driving parts
    .bpmLo = 164, .bpmHi = 168, .mode = &kAeolian, .tonics = { 2, 9, 4, 11, 7 },
    .progs = { { { 0, 3, 6, 2 }, { 2, 3, 0, 0 }, { 0, 4, 3, 6 } } }, .preRoot = 6,
    .form = { { { "Rainfall", 16, .15f, "PAT" }, { "Warm Front", 16, .35f, "PADHTMW" }, { "Roller", 32, .65f, "PKSHCBUADTGYMW" },   // 2026-11-26 (Kerem): a minute shorter: Roller, Undertow, Liquid Gold
                { "Undertow", 24, .40f, "PADFTMW" },
                { "Liquid Gold", 32, 1.0f, "PKSHCBUADFTGYMRIW" }, { "Tide", 16, .15f, "PARTW" } } },
    // the two-step's weight drives the pulse; rides and a shuffle of ticks around the live breaks
    .pulse = part("X.........X.....", "X.........X.....", "X.........X..x.."),
    .metal = part("", "..1...2...1...2.", "..1.2.1...1.3.2."),
    .tick = part("x.o.x.o.x.o.x.o.", "x.ogx.o.x.ogx.o.", "xgoxxgo2xgoxxgo2"),
    .perc = part("", "......1.........", "......1...3....."),
    // the roller: a bass that never sits still, the fifth and the octave in its turns
    .bass = part("x---------------", "x-----x---x-----", "x--.x-.x--x-5-8."),
    // the Rhodes: stabs on the off-beats of the two-step
    .chord = part("x---------------", "..x.....x.x.....", "..x..x..x.x..x.."),
    .sub = part("x---------------", "x-------x-------", "x-----x-x-------"),
    .ground = part("", "......1.........", "......1...........2.....3......."),
    .body = part("", "..o..x.o..o..x.g", "..oxg..o..oxg.xo"),
    .mallet = part("", "1.....3.....2...", "1..2..3..4..3.2."),
    .texture = part("x---------------", "x-------x-------", "x---x---x---x---"),
    .percLo = 36, .percHi = 52,
    .swing = .06f, .chordSweep = .35f, .riff = G::RiffStyle::Sludge, .riffLo = 35, .summit = G::Summit::NewTheme,
    .build = true, // 2026-11-01: the sharp part changes
    .lead = G::Drone, .sing = true, // 2026-11-02: the singer on the drone track
    .wobble = 4, // a cycle a beat at the summit, every half bar in the Roller
};

const G::Spec kEcstatic {
    .scene = "ecstatic", .title = "Ecstatic Arpeggios",
    // after Klaus Schulze and Caterina Barbieri (2026-10-30, Kerem: "more melancholic and deep"): one
    // long sequence turning against the bar, changing a note at a time over minutes; aeolian; a guitar
    // drone under it, and at the summit a pedal: the low end holds the tonic while the chords move above.
    .bpmLo = 118, .bpmHi = 124, .mode = &kAeolian, .tonics = { 9, 2, 4, 7, 0 },
    .progs = { { { 0, 5, 3, 4 }, { 0, 6, 5, 6 }, { 0, 2, 5, 4 } } }, .preRoot = 4,
    // 2026-11-01 (Kerem: "too many synths, rewrite it"): one sound per part, the chords gone (the sequence,
    // the pad and the choir carry the harmony), the sequence an octave deeper, the sharp part changes
    .form = { { { "Dawn", 16, .15f, "PAT" }, { "Continuum", 8, .35f, "PATM" }, { "Lattice", 16, .55f, "PAHTRM" },   // 2026-11-26 (Kerem): a minute shorter
                { "Suspension", 16, .40f, "PAFTGYMR" }, 
                { "Computation", 32, 1.0f, "PKSHCBUAFTGYMRI" }, { "Halo", 16, .20f, "PART" } } },
    // no break here: the sequence is the rhythm; the pulse only lands at the summit
    .pulse = part("", "", "X...X...X...X..."),
    .metal = part("", "", "......1.......2."),
    .tick = part("", "..g...g...g...g.", "g.g.g.g.g.g.g.g."),
    .perc = part("", "..........1.....", "..........1...........3.....2..."),
    .bass = part("", "x-------x-------", "x-------5---8---"),
    .chord = part("x-------........", "x.......x.......", "x...x...x...x..."),
    .sub = part("", "x---------------", "x-------x---^---"),
    .ground = part("", "", "..........1....."),
    .body = part("", "......o.......o.", "..o...o...o...o."),
    .mallet = part("1.......3.......", "1..3..2..4..3...", "1.32.14.3.21.43."),
    .texture = part("x---------------", "x---------------", "x-------x-------"),
    .arpRate = { 16, 16, 16 }, .arpLenLo = 9, .arpLenHi = 13, .arpDensLo = .50f, .arpDensHi = 1.0f, .arpGate = .6f,
    .arpLo = 45, .arpHi = 81, .percLo = 36, .percHi = 52, .ext = 1, .hook = false, // the JC303 sounds where it is written
    .arpMorph = 8, .riff = G::RiffStyle::Drone, .riffLo = 33, .summit = G::Summit::Pedal, .build = true,
};

const G::Spec kNoir {
    .scene = "noir", .title = "Noir",
    // after Portishead, Mezzanine and Bohren: slow, the hungarian minor (the raised fourth and the
    // leading tone around a minor tonic), long chords, the hook alone in a big room
    .bpmLo = 72, .bpmHi = 80, .mode = &kHungarianMinor, .tonics = { 11, 6, 8, 1, 3 },
    .progs = { { { 0, 5, 4, 0 }, { 0, 5, 1, 4 }, { 0, 6, 5, 4 } } }, .preRoot = 4,
    // 2026-11-01 (Kerem): the guitar stabs out, a second pad (the drone track) where they were; a
    // contrabass walking under a saxophone solo
    .form = { { { "Rain", 8, .15f, "PFT" }, { "Cigarette", 16, .30f, "PHDT" }, { "Roads", 16, .55f, "PKSHCBUDLTGYM" },
                { "Hollow Hall", 16, .35f, "PDAFT" }, 
                { "Cathedral of Smoke", 24, 1.0f, "PKSHCBUADFTGYMIW" }, { "Last Light", 8, .25f, "PDALTW" } } },
    .pulse = part("X.........X.....", "X.......X.X.....", "X.....x.X.X....."),
    .metal = part("", "..........1.....", "......1...2...3."), // far bells
    .tick = part("..g...g...g...g.", "x.g.x.g.x.g.x.g2", "xgxgx2xgxgxgx2xg"),
    .perc = part("", "......1.......3.", "...1..2...3...2."), // deep metal, never on the one: his kick is there
    .bass = part("x---------------", "x-------5-------", "x-----5-x---3---"),
    .chord = part("x---------------", "x-------x-------", "x---..x-x---..x-"),
    .sub = part("x---------------", "x-------^-------", "x-----x-^---x---"),
    .ground = part("", "..........1.....", "......1...3....."),
    .body = part("", "......o.........", "..o...o...o..o.."),
    .mallet = part("1...............", "1.......3.......", "1.......3...2..."),
    .texture = part("x---------------", "x---------------", "x-------x-------"),
    .arpLo = 52, .arpHi = 79, .percLo = 36, .percHi = 52, // the soloist: an alto saxophone, where it sounds
    .gap = .10f, .swing = .10f, .chordSweep = .45f, .summit = G::Summit::Theme,
    .build = true, // 2026-11-01: the sharp part changes, as in Deconstructed Club
    .walk = true, .lead = G::Arp, .pedal = true,
};

const G::Spec kDeconstructed {
    .scene = "deconstructed", .title = "Deconstructed Club",
    .bpmLo = 128, .bpmHi = 136, .mode = &kPhrygianDominant, .tonics = { 4, 9, 2, 11, 7 }, // the major third over the flat second
    .progs = { { { 0, 1, 5, 6 }, { 0, 3, 1, 0 }, { 0, 6, 5, 1 } } }, .preRoot = 1,
    // 2026-11-01 (Kerem): the big parts start sharp, the build before each one obvious (spec.build);
    // the low logs in more parts and busier
    .form = { { { "Shatter", 16, .20f, "PDFTRG" }, { "Glass", 16, .40f, "PCKDTGYMR" }, { "Fracture", 32, .80f, "KSCBDULTGYMR" },
                { "Cathedral", 32, .40f, "PDAFTG" }, 
                { "Rebirth", 40, 1.0f, "PKSHCBUADFTGYMIR" }, { "Wound", 16, .30f, "PDATRG" } } },
    .pulse = part("X.......X..x....", "X..x.....X.x..x.", "X..x.X...X.xx..xX....x..X..x.X.."),
    .metal = part("", "......1...2.1...", "...1..2...3.1.21"),
    .tick = part("", "x...x.x...x...x.", "x.xxx.x.x.xxx.xx"),
    .perc = part("1.......3.......", "1..3..2...4..1..", "1.3.2.41.3.2.4.1"), // metal: clicks tuned to the chord
    .bass = part("x---------------", "x-------x---5---", "x--x--x-x---8-5-"),
    .chord = part("X---------------", "X.......X..X....", "X..X..X.X.X..X.."),
    .sub = part("x---------------", "x-------x--x----", "x--x--x-x---^-5-"),
    .ground = part("1.........2.....", "1..2......2..1..1.....2...3..1..", "1.21..2.1.2..1.21..2..1.2..3.1.2"),
    .body = part("", "...o..o.....o...", ".o.o..o.o..o..o."),
    .mallet = part("1...............", "1.....2...3.....", "1..3..2..4.1..3."),
    .texture = part("x---------------", "x-------x-------", "x---x---x---x---"),
    .percLo = 60, .percHi = 90, .harmLo = 43, .harmHi = 67, // the choir low: a cello section (Kerem: "low oriented and celloish")
    .chop = .30f, .gap = .25f, .stutter = .20f, .stutterTrack = G::Metal, .stutterDiv = 32,
    .riff = G::RiffStyle::Chug, .riffLo = 35, .summit = G::Summit::Halftime, .build = true,
};

const G::Spec kHalftime {
    .scene = "halftime", .title = "Halftime",
    .bpmLo = 168, .bpmHi = 174, .mode = &kHarmonicMinor, .tonics = { 7, 0, 5, 10, 2 }, // harmonic minor: the major V pulling home
    .progs = { { { 0, 5, 3, 4 }, { 0, 3, 4, 0 }, { 5, 3, 4, 4 } } }, .preRoot = 4,
    // 2026-11-01 (Kerem): Breath a lot longer; the low and the mid percussion in more parts; Gravity and
    // Fall gone, the song ends in Breath; a bowed cello section (the drone track) holds the chords
    // 2026-11-25 (Kerem: "you can shorten the half time"): every part after the first 8 bars shorter, 176 -> 136 bars
    .form = { { { "Smoke", 16, .15f, "PFT" }, { "Weight", 16, .35f, "PHBUTGY" }, { "Halftime", 32, .60f, "PKSHCBUDTGYMW" },
                { "Hollow", 16, .40f, "PAFTYW" }, { "Mass", 40, .85f, "PKSHCBUADTGYMIRW" }, { "Breath", 16, .25f, "PATW" } } },
    // halftime: the tempo is 170, the body feels 85 (the live snare on three); the metal answers around it
    .pulse = part("X...............", "X.........x.....", "X.........x...x.X......x..x....."),
    .metal = part("..........1.....", "......1...2.....", "......1...2...3.......1...2..1.3"),
    .tick = part("x...x...x...x...", "x.x.x.x.x.x.x.x.", "x.xgx.x.x.xgx.x."),
    .perc = part("", "..........1.....", "......1...3...........2...4..1.."), // deep metal around the snare on three
    .bass = part("x---------------", "x-------..x-----", "x-----x-..x---5-"),
    .chord = part("", "x---------------................", "x---------------................"),
    .sub = part("x---------------", "x---------x-----x-------^-------", "x-------x-x-----x-----5-^---x-x-"),
    // the low drums (logs) and the hand drums groove around his kick and his snare on three, never on them
    .ground = part("......1...........2.............", "......1...1...........2...1.....", "...1..1...1..2.....1..2...1.3.2."),
    .body = part("..o.......o.....", "..o..x.o..o..x.g..o..x.o..og.x.o", "..oxg.xo.go.xgxo..ox.gxo.go.x.xo"),
    .mallet = part("1...............", "1.......2.......", "1.....3...2....."),
    .texture = part("x---------------", "x---------------", "x-------x-------"),
    .percLo = 36, .percHi = 52,
    .chop = .10f, .stutter = .25f, .stutterTrack = G::Tick, .stutterDiv = 32,
    .riff = G::RiffStyle::Doom, .riffLo = 31, .summit = G::Summit::Unison, .build = true,
};

const G::Spec kDoom {
    .scene = "doom", .title = "Doom",
    // after Sleep, Electric Wizard, Sunn O))) and Bell Witch: slow, phrygian (the flat second against
    // the root), power chords tuned down; the riff is the song. It starts alone, the band joins it,
    // and the summit is all of the low end playing it as one.
    .bpmLo = 74, .bpmHi = 78, .mode = &kPhrygian, .tonics = { 1, 2, 11, 9, 4 }, // 2026-10-31 (Kerem): a little faster
    .progs = { { { 0, 1, 0, 5 }, { 0, 5, 3, 1 }, { 0, 3, 1, 0 } } }, .preRoot = 1,
    // 2026-11-25 (Kerem: "extend the doom song, it is good, make additional parts, maybe a new synth introduced when
    // it's finished and new parts come"; Doom is now the concert's last song): after the Monument it does not end. Embers:
    // the band gone, a new synth (palette: enter Embers lead) alone over the drone and the pad; Pyre: the riff back under
    // it; Pillar: a second summit, longer than the first, the new synth over all of it; then the Dust as before
    .form = { { { "Feedback", 8, .15f, "RFTW" }, { "Procession", 16, .45f, "PRBUTGW" }, { "Monolith", 16, .70f, "PRBUDCKSTGYMW" },
                { "Shroud", 16, .40f, "PAFTW" },
                { "Monument", 24, 1.0f, "PRBUDCKSTGYMAFIW" },
                // 2026-11-25 later (Kerem: "make doom 11-12 mins"): the new parts longer, and Requiem, the new synth over
                // the hook in the calm, before the Dust
                { "Embers", 24, .30f, "PFTW+" }, { "Pyre", 24, .65f, "PRBUDCSTGYW+" },
                { "Pillar", 40, 1.0f, "PRBUDCKSTGYMAFIW+" }, { "Requiem", 16, .45f, "PAFTW+" }, { "Dust", 12, .15f, "RFTW" } } },
    .pulse = part("X...............", "X.......X.......", "X.......X...x..."),
    .metal = part("", "..........1.....", "......1.......2."), // far bells, chains
    .tick = part("", "", ""),
    .perc = part("", "........1.......", "....1.......2..."), // struck iron, deep
    .bass = part("x---------------", "x---------------", "x-------x-------"),
    // 2026-10-31 (Kerem: long sustains, no short plucky envelopes, heavy notes to play breakdowns on):
    // the chords held through, no clicks, the riff one heavy chord a bar, the choir in open intervals
    .chord = part("", "x-------------------------------", "x-------------------------------"),
    .sub = part("x---------------", "x---------------", "x-------^-------"),
    .ground = part("", "1...............", "1.......2.....1."),
    .body = part("", "........o.......", "....o.......o.o."),
    .mallet = part("", "1...............", "1.......3......."),
    .texture = part("x---------------", "x---------------", "x-------x-------"),
    .arpLo = 55, .arpHi = 79, .percLo = 33, .percHi = 48, .ext = 0,
    .riff = G::RiffStyle::Sludge, .riffLo = 30, .summit = G::Summit::Unison, .legato = true, .gaze = true, .pedal = true,
};

const G::Spec kStoner {
    .scene = "stoner", .title = "Working Class Curse",
    // 2026-11-04 (Kerem: "a faster doom, a completely different song, fast; take Doom's instruments, add
    // Colin Stetson like delayed saxophones, a faster stoner with high feedback saxophones"): after Kyuss,
    // Fu Manchu and Stetson's New History Warfare. The riff drives in muted eighths with chords on the
    // push; a low saxophone circles the chord tones in sixteenths without a breath, into long echoes and
    // the feedback bus, and an alto howls long notes over it (the drone track, as a singer). Dorian: the
    // minor with its bright sixth, the blues of it.
    .bpmLo = 142, .bpmHi = 146, .mode = &kDorian, .tonics = { 2, 4, 0, 9, 7 },
    .progs = { { { 0, 0, 3, 6 }, { 0, 6, 3, 0 }, { 0, 3, 0, 4 } } }, .preRoot = 6,
    .form = { { { "Shift Whistle", 16, .25f, "ATW" }, { "Clock In", 16, .55f, "PRBUTGYA" },
                { "Overtime", 32, .80f, "PRBUDCKSTGYMAW" }, { "Lunch Break", 16, .35f, "PAFTW" },
                { "Strike", 48, 1.0f, "PRBUDCKSTGYMAFIW" }, { "Last Shift", 16, .20f, "ARTW" } } },
    .pulse = part("X.......X.......", "X.....X...X.....", "X.....X...X...x."),
    .metal = part("", "..........1.....", "......1.......2."), // chains, as in Doom
    .tick = part("", "", ""),
    .perc = part("", "....1.......1...", "....1..1....1.2."), // struck iron on his backbeat's neighbours
    .bass = part("x---------------", "x-x-x-x-x-x-x-x-", "x-x-x-x-x-x-5-x-"), // eighths under the riff
    .chord = part("", "x-------------------------------", "x-------------------------------"),
    .sub = part("x---------------", "x-------x-------", "x-------x---^---"),
    .ground = part("", "1.......1.......", "1.....1...1...2."),
    .body = part("", "..o...o...o...o.", "..o.o.o...o.o.oo"),
    .mallet = part("", "1...............", "1.......3......."),
    .texture = part("x---------------", "x---------------", "x-------x-------"),
    // the low saxophone: sixteenths in the drive, eighths in the calm, a cycle against the bar
    .arpRate = { 8, 16, 16 }, .arpLenLo = 7, .arpLenHi = 11, .arpDensLo = .85f, .arpDensHi = 1.0f, .arpGate = .9f,
    .arpLo = 40, .arpHi = 64, .percLo = 33, .percHi = 48, .ext = 0, .hook = false,
    .swing = .04f, .riff = G::RiffStyle::Stoner, .riffLo = 33, .summit = G::Summit::Unison, .gaze = true,
    .lead = G::Drone, .sing = true,
};

const G::Spec kBlackgaze {
    .scene = "blackgaze", .title = "Blackgaze",
    // after Deafheaven, Alcest and Lantlos: tremolo-picked open chords (root, fifth, ninth) as a wall,
    // the minor with its major colours (the relative major, the sixth and seventh chords), the choir
    // high, and a melody that only the summit finds.
    // 2026-10-31 (Kerem: no high percussion, industrial, he may blast-beat it; a northern line like
    // Transilvanian Hunger or Spectral Wound, in that feel): no ticks, glass or bright metal; low struck
    // iron on a machine grid (eighths at the height, threes over it), a plate on the offbeats; the
    // tremolo carries a melody on its top string.
    .bpmLo = 150, .bpmHi = 158, .mode = &kAeolian, .tonics = { 4, 11, 9, 2, 6 },
    .progs = { { { 0, 5, 2, 6 }, { 5, 6, 0, 0 }, { 0, 3, 5, 6 } } }, .preRoot = 6,
    .form = { { { "Haze", 16, .20f, "PFTRG" }, { "Rise", 24, .50f, "PRBUTGYCA" }, { "Storm", 40, .80f, "PRBUKSCTGYI" },
                { "Clearing", 24, .35f, "PAFTG" }, { "Sunbather", 48, 1.0f, "PRBUKSCTGYFI" },
                { "Afterglow", 16, .20f, "PRT" } } },
    .pulse = part("X.......X.......", "X...X...X...X...", "X.X.X.X.X.X.X.X."), // the blast's weight, as ghost pumps
    .metal = part("", "..............1.", "......1.......2."),
    .tick = part("", "", ""),
    .perc = part("", "......1.......1.", "1..1..1...1..1.."),
    // the bass grooves where the song is between its storms (Rise, Ascent: 3+3+2, then a push to the
    // fifth) and is plain under the blasts (Kerem: "main parts are cool simple")
    // 2026-11-23 (Kerem: "more black metal", "not sounding like a machine"): in the storms the bass follows the
    // guitar's roots in eighths; the keys (piano) and the erhu sit the storms out, the organ and the choir stay.
    // Then ("keep the clarity of the first minute in all sections"): the eighths played soft, the Growlybass's soft
    // layer, cleaner (its loud one is grit above 400 Hz, over the guitar's notes: the storms' harmony measured 0.57
    // of its energy in its three strongest notes, 0.64 with this and the tonic pedal gone; Rise 0.73)
    .bass = part("x---------------", "x--x--x-.xx-5-x-", "o-o-o-o-o-o-o-o-"),
    .chord = part("", "x---------------", "x-------x-------"),
    .sub = part("x---------------", "x---------------", "x-------x-------"),
    .ground = part("1...............", "1.......1..1....", "1.1.1.1.1.1.1.1."),
    .body = part("", "....o.......o...", "..o...o...o.o.o."),
    .mallet = part("", "", ""),
    .texture = part("x---------------", "x---------------", "x-------x-------"),
    .arpLo = 64, .arpHi = 88, .percLo = 36, .percHi = 52,
    // the storms turn phrygian: i, the flat II, i, the flat VI, over and over (no tonic pedal under the guitar: it
    // clouded them); the summit is no new theme but the
    // Storm's own line come back, a second guitar joining it halfway (repetition, opening up)
    .chordSweep = .30f, .riff = G::RiffStyle::Tremolo, .riffLo = 40, .summit = G::Summit::Theme, .ends = false, .pedal = true,
    .darkMode = &kPhrygian, .darkProg = { 0, 1, 0, 5 },
};

// ---- 2026-11-21 (Kerem: "combine the ideas in wake and saint into one song, remove the fast one, I don't want abrupt
// part changes in that song, I want one good form song"; after 2026-11-20's "3 new songs in Doom's style ... vary the
// tempo, the darkness in the melodies and harmony is good"). Not in the concert yet. Doom's style on its own sounds
// (sounds/wake.txt): Lantern Wake's funeral doom and Hollow Saint's sludge as one song.

const G::Spec kWake {
    .scene = "wake", .title = "Hollow Wake",
    // one arc: from Lantern Wake's organ alone the band gathers part by part, each a small step up (a fifth of the
    // range at most, so every change is a level turn: the low end leans into the next root, nothing stops, no end
    // figures) to the summit, and down again the same way. Harmonic minor (Lantern Wake); Hollow Saint's phrygian
    // dominant is its fifth mode, the same notes, and the summit turns to it on the home tonic (the major third
    // over the flat second: the darkest colour of the song); the summit holds the tonic under moving chords
    // (Lantern Wake), and after it the song falls into half time (Hollow Saint), twice the weight, before it thins
    // to the riff and the drone alone. Funeral pace, between the two songs.
    .bpmLo = 66, .bpmHi = 70, .mode = &kHarmonicMinor, .tonics = { 9, 4, 2, 7, 11 },
    .progs = { { { 0, 5, 3, 4 }, { 0, 1, 4, 0 }, { 5, 3, 4, 0 } } }, .preRoot = 4,
    .form = { { { "Vigil", 6, .12f, "PATW" }, { "Salt", 8, .30f, "PRAFTW" }, { "Bearers", 10, .48f, "PRBUATGW" },
                { "Pilgrims", 10, .66f, "PRBUATGYMW" }, { "Scourge", 12, .84f, "PRBUADCKSTGYMW" },
                { "Requiem", 16, 1.0f, "PRBUDCKSTGYMAFIW" }, { "Martyr", 10, .80f, "PRBUDCTGYMAWh" },
                { "Reliquary", 8, .60f, "PRBUTGYMAW" }, { "Candle", 6, .40f, "PAFTMW" }, { "Earth", 6, .20f, "RATW" } } },
    .pulse = part("X...............", "X.......X.......", "X.......X......."),
    .metal = part("", "..............1.", "......1.......2."), // a far bell, chains
    .tick = part("", "", ""),
    .perc = part("", "........1.......", "....1..1....1.2."), // struck iron on his backbeat's neighbours
    .bass = part("x---------------", "x---------------", "x-------x-------"),
    .chord = part("", "x-----------------------------..", "x-----------------------------.."), // a breath at the end: in half time a held note would run into the next
    .sub = part("x---------------", "x---------------", "x-------^-------"),
    .ground = part("", "1...............", "1.....1...1...2."),
    .body = part("", "............o...", "..o...o...o.o..o"),
    .mallet = part("", "1...............", "1.......3......."),
    .texture = part("x---------------", "x---------------", "x-------x-------"),
    // 2026-11-21 (Kerem: "the choir as a soloist, and a good synth with arpeggios to support the long drones"): the arp
    // track an arpeggio, not the hook (eighths in the calm, sixteenths as it drives, five to seven notes against the
    // bar, so it turns slowly under the held chords); the soloist a choir, long notes only
    .arpRate = { 16, 16, 16 }, .arpLenLo = 5, .arpLenHi = 7, .arpDensLo = 1.0f, .arpDensHi = 1.0f, .arpGate = .8f, // more of it (2026-11-21)
    .arpLo = 55, .arpHi = 79, .percLo = 33, .percHi = 48, .ext = 0, .hook = false,
    .riff = G::RiffStyle::Doom, .riffLo = 30, .summit = G::Summit::Pedal, .ends = false, .legato = true, .gaze = true,
    .sing = true, .pedal = true, .flow = true, .peakMode = &kPhrygianDominant,
};

// ---- 2026-11-21 (Kerem: "one song like Ruby My Dear, Igorrr or Culprate", named Bleach). Not in the concert yet.
// Breakcore under baroque: a harpsichord running sixteenths and string stabs on a baroque harmonic-minor cadence
// (Igorrr), the electronic percussion chopped, gapped and stuttered into 64ths (Ruby My Dear; still no kick,
// snare or hats: his kit plays those), a growling synth bass (Culprate), a soprano soloist, the doom guitar
// chugging metal under it; the summit falls into half time, twice the weight. Sharp part changes (build).
const G::Spec kBleach {
    .scene = "bleach", .title = "Bleach",
    .bpmLo = 168, .bpmHi = 172, .mode = &kHarmonicMinor, .tonics = { 4, 11, 9, 2, 7 },
    .progs = { { { 0, 3, 4, 0 }, { 5, 1, 4, 0 }, { 0, 5, 3, 4 } } }, .preRoot = 4,
    .form = { { { "Lye", 16, .15f, "PAT" }, { "Starch", 24, .40f, "PADBUHTGYM" }, { "Bleach", 48, .85f, "PKSHCBUADTGYMIR" },
                { "Linen", 24, .35f, "PAFTM" }, { "Rinse", 8, .60f, "KYBU" }, // the break and the bass alone before the drop
                { "Burn", 64, 1.0f, "PKSHCBUADFTGYMIR" }, { "White", 16, .15f, "PAT" } } },
    // 2026-11-21 (Kerem: "more rhythm and percussion like dnb or hard jungle and breakcore", the high rhythms too loud):
    // the low log and the hand drum play a break (the amen's shape: the low on 1, the and of 1, the and of 3; the skin
    // on 2 and 4 with ghosts around them), the second bar rolling; chopped and stuttered on the skin, the ticks under them
    .pulse = part("X.........X.....", "X.X.......XX....", "X.X.......XX....X.X.......X...X."),
    .metal = part("", "....1.....2...1.", "..1.2..1.3.2..1."),
    .tick = part("x...x...x...x...", "x.x.x.x.x.x.x.x.", "x.xgx.oxx.xgx.ox"),
    .perc = part("", "..1.......1.....", "..1....1..1...2."), // iron, off his backbeat
    .bass = part("x---------------", "x-----x---x-----", "x--x--x-..x-5-8-"), // the growl's wobble rhythm
    .chord = part("", "x.....x...x.....", "x..x..x.x.x..x.."), // string stabs
    .sub = part("x---------------", "x-------x-------", "x-----x-x---^---"),
    .ground = part("1.........1.....", "1.1.......21....", "1.1.......21....1.1.......2...1."),
    .body = part("....x.......x...", "....x..g.g..x..g", "....x..g.gg.x..g....x..g.g..xg.x"),
    .mallet = part("", "1.....2.........", "1..3..2..4.1..3."),
    .texture = part("x---------------", "x-------x-------", "x---x---x---x---"),
    .arpRate = { 16, 16, 16 }, .arpLenLo = 6, .arpLenHi = 8, .arpDensLo = .9f, .arpDensHi = 1.0f, .arpGate = .6f,
    .arpLo = 60, .arpHi = 86, .percLo = 36, .percHi = 52, .harmLo = 45, .harmHi = 64, .ext = 0, .hook = false, // the male choir: G2-F4
    .chop = .35f, .gap = .10f, .stutter = .35f, .stutterTrack = G::Body, .stutterDiv = 32,
    .riff = G::RiffStyle::Chug, .riffLo = 33, .summit = G::Summit::Halftime, .build = true,
    .leadLo = 67, .leadHi = 84, .sing = true, // the soprano (samples G4-C6)
    .breaks = true, // the KAN amen chopped by BreakMachine's player (Kerem's exception for this song)
    .trade = true, .wobble = 2, .flip = true,
};

bool is(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

} // namespace

const std::vector<const G::Spec*>& GenreComposer::all()
{
    static const std::vector<const Spec*> v = { &kStoner, &kJungle, &kEcstatic, &kNoir, &kDeconstructed, &kHalftime, &kDoom, &kBlackgaze };
    return v;
}

const std::vector<const G::Spec*>& GenreComposer::extra()
{
    static const std::vector<const Spec*> v = { &kWake, &kBleach };
    return v;
}

const G::Spec* GenreComposer::find(const std::string& scene)
{
    for (auto* s : all()) if (scene == s->scene) return s;
    for (auto* s : extra()) if (scene == s->scene) return s;
    return nullptr;
}

const SceneInfo* genreSceneInfo(const std::string& scene)
{
    // The same eleven tracks for every genre: the pad, the choir and the chords into the reverb, the
    // arpeggio and the chords into the echo, the low end dry.
    static const std::vector<SceneInfo> infos = [] {
        std::vector<SceneInfo> v;
        std::vector<const char*> songs;
        for (auto* s : G::all()) songs.push_back(s->scene);
        for (auto* s : G::extra()) songs.push_back(s->scene);
        songs.push_back("hertz"); // Low Hertz (PulseComposer, Composer.cpp): the genre tracks, its own composer
        for (auto* s : songs)
            v.push_back({ s, 20, { "pad", "pulse", "metal", "tick", "perc", "bass", "sub", "arp", "chord", "fx", "harm",
                                          "ground", "body", "mallet", "texture", "impact", "riff", "drone", "lead", "boom" },
                          { 0, 1, 2, 2, 2, 1, 1, 3, 0, 0, 0, 1, 2, 2, 2, 1, 1, 0, 3, 1 },
                          { { 0, .30f }, { 0, .02f }, { .05f, .12f }, { .03f, .08f }, { .10f, .15f }, { 0, .03f }, { 0, 0 }, { .20f, .20f }, { .20f, .25f }, { .10f, .35f }, { .08f, .40f },
                            { 0, .10f }, { .05f, .12f }, { .20f, .25f }, { 0, .20f }, { .10f, .45f }, { .03f, .10f }, { .05f, .30f }, { .20f, .35f }, { 0, .12f } },
                          { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
                          { 1, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0 }, true });
        return v;
    }();
    for (auto& i : infos) if (scene == i.name) return &i;
    return nullptr;
}

GenreComposer::GenreComposer(const Spec& spec, uint64_t seed) : Composer(seed), spec_(spec)
{
    leadRng_ = Rng(seed * 0x9E3779B97F4A7C15ull + 7);
    booms_ = Sentence(seed);
    solo_ = Soloist(spec_.lead, range(spec_.lead)[0], range(spec_.lead)[1], spec_.sing);
    tonicPc_ = spec_.tonics[size_t(rng_.range(0, 4))];
    bpm_ = 2.0 * rng_.range(spec_.bpmLo / 2, spec_.bpmHi / 2);
    planForm();
    planHarmony();
    makeArp();
    arpHome_ = arp_;
    makeHook();
    if (spec_.summit == Summit::NewTheme)
    {   // the summit's own theme, not the first one moved: written now, the home theme kept
        const auto home = hookHome_;
        makeHook();
        hookSummit_ = hook_;
        hook_ = hookHome_ = home;
    }
    makeRiff();
    planEnds();
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
        // a + in its layers: a part added to a song Kerem had approved, which keeps the sounds it has there (the sound
        // plan passes it by, so the parts before sound as they did; Doom's new parts, 2026-11-25)
        sections_.push_back({ t.name, bar, t.bars, t.energy, layersOf(t.layers), theme, std::strchr(t.layers, '+') != nullptr });
        drummerLeads_.push_back(std::strchr(t.layers, 'L') != nullptr);
        halfSec_.push_back(std::strchr(t.layers, 'h') != nullptr);
        if (theme) { peakFrom_ = bar; peakTo_ = bar + t.bars; }
        for (int i = 0; i < t.bars; ++i)
        {
            sectionOfBar_.push_back(int(sections_.size()) - 1);
            const bool whole = bar > 0 && t.energy >= .6f && t.energy > prev + .1f; // drives(): at full energy from its downbeat
            const float k = whole ? 1.0f : std::min(1.0f, float(i + 1) / (t.bars * 0.5f));
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
        // The summit is not reached by transposing: it keeps the key (Kerem: find other ways to establish
        // the main parts). A new theme brings the third progression with it.
        std::array<int, 4> third = spec_.progs[size_t(3 - h - a)];
        const auto& prog = spec_.darkMode && sec.energy >= .75f ? spec_.darkProg
                         : sec.restoreTheme && spec_.summit == Summit::NewTheme ? third : away ? away_ : home_;
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
    const auto& m = modeAt(bar);
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
    if (sec.restoreTheme && in == 0) hook_ = spec_.summit == Summit::NewTheme ? hookSummit_ : hookHome_;
    else if (in == 0 && sec.startBar > 0 && sectionAt(sec.startBar - 1).restoreTheme) hook_ = hookHome_; // after the summit, the first theme
    if (summitUnison(bar) && in < sec.bars / 2) return; // the riff is the theme here: the hook waits
    else if (in > 0 && in % 8 == 0 && rng_.chance(.25 + .3 * e)) // it wears a little, as a phrase does when it is played on
    {
        auto& n = hook_[size_t(rng_.range(1, int(hook_.size()) - 1))];
        n.deg = std::clamp(n.deg + (rng_.chance(.5) ? 1 : -1) * 2, -2, 9);
    }

    // How this section develops it.
    const bool calm = e < .35f, away = awayBar_[size_t(bar)], summit = sec.restoreTheme;
    const bool slow = calm || summitHalftime(bar);
    const int span = slow ? 4 : 2;            // slowed to half speed in the calm (and in a half-time summit)
    const int cycle = in / span, within = in % span;
    if (calm && cycle % 2 == 1) return;       // alone, with room around it
    const int t0Cycle = (bar - within) * kBarTicks;
    const int64_t six = kSixteenth * (slow ? 2 : 1);
    const auto& m = modeAt(bar);
    const int r = rootAt(bar), lo = spec_.arpLo, hi = spec_.arpHi;
    const int rootP = pitchOf(bar, 0, lo + 5, lo + 16, 12 * (lo / 12)); // base on a C: it is a register, not a transposition
    // In eight bars: the question, its answer, the question, and a close (the answer cut to one
    // long note on the chord's root): a period, as a song has.
    const bool close = !calm && !away && cycle % 4 == 3;
    int answer = 32; // the answer's first note: the one the close holds (a slowed cycle spans four bars, so found once)
    for (auto& n : hook_) if (n.step >= 16) { answer = n.step; break; }
    for (auto n : hook_)
    {
        const int64_t t = int64_t(t0Cycle) + n.step * six;
        if (t < int64_t(bar) * kBarTicks || t >= int64_t(bar + 1) * kBarTicks) continue; // only this bar's notes
        if (close && n.step >= 16)
        {
            if (n.step != answer) continue;
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
    if (size_t(bar) < drop_.size() && (drop_[size_t(bar)] & bit(track))) return false;
    if (track == Harm) return (sectionAt(bar).layers & bit(Pad)) != 0 && sectionAt(bar).energy >= .4f;
    return (sectionAt(bar).layers & bit(track)) != 0;
}

std::array<int, 2> GenreComposer::range(int track) const
{
    if (spec_.breaks && track == Body) return { 36, 103 };
    switch (track)
    {
        case Pad: return { 36, 76 };
        case Pulse: return { 24, 35 };
        case Metal: return { 48, 76 };
        case Tick: return { 72, 100 };
        case Perc: return { spec_.percLo, spec_.percHi };
        case Bass: return spec_.walk ? std::array<int, 2> { 28, 64 } : std::array<int, 2> { 40, 64 };
        case Sub: return { 24, 52 };
        case Arp: return { spec_.arpLo, spec_.arpHi };
        case Chord: return { 46, 76 };
        case Harm: return { spec_.harmLo - 2, spec_.harmHi + 2 };
        case Ground: return { 28, 48 };
        case Body: return { 45, 69 };
        case Mallet: return { 60, 88 };
        case Texture: return { 48, 84 };
        case Impact: return { 30, 50 };
        case Riff: return { 24, 76 };
        case Drone: return spec_.lead == Drone ? std::array<int, 2> { 62, 84 } : std::array<int, 2> { 40, 67 };
        case Lead: return { spec_.leadLo, spec_.leadHi };
        case Boom: return { 26, 40 };
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
    if (track == spec_.lead) return bar >= 4 ? 1.0f : 0.0f; // the soloist plays everywhere
    if (track == Boom) return 1;                             // the low hits follow the parts themselves
    if (track == Riff && attackBar(bar) && active(Riff, bar + 1)) return 1;
    if (buildsWith(track, bar)) return 1;
    // an instrument resting before a part change (drop_) keeps its level, so it is back whole on the downbeat
    const bool resting = bar >= sections_[0].bars && (sectionAt(bar).layers & bit(track)) != 0;
    if (!active(track, bar) && !resting) return 0;
    auto& sec = sectionAt(bar);
    const int in = bar - sec.startBar, left = sec.startBar + sec.bars - bar;
    const float fade = track == Pad || track == Fx || track == Sub || track == Chord || track == Harm ? 4.0f : 2.0f;
    const float fadeIn = spec_.build || drives(sectionOfBar_[size_t(bar)]) || active(track, bar - in - 1) || (bar == 0 && track != Pad) ? 1.0f : std::min(1.0f, (in + 1) / fade);
    const float fadeOut = active(track, bar + left) ? 1.0f : std::min(1.0f, left / fade);
    return std::min(fadeIn, fadeOut);
}

int GenreComposer::pitchOf(int bar, int deg, int lo, int hi, int base) const
{
    return foldInto(base + tonicAt(bar) + degreeSemis(modeAt(bar), rootAt(bar) + deg), lo, hi);
}

int GenreComposer::ladderPitch(int bar, int idx, int lo, int hi) const
{
    int p = 12 * (lo / 12) + tonicAt(bar) + degreeSemis(modeAt(bar), rootAt(bar) + kLadder[idx % 7]);
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
    const bool wobbles = spec_.wobble > 0 && sec.energy >= .6f && active(Bass, bar) && !attackBar(bar);
    if (wobbles) // Culprate's growl: the bass's filter opening and closing on the grid
    {
        const int cycle = spec_.wobble * (sec.energy >= .95f ? 1 : 2);
        for (int s = 0; s < 16; ++s)
            out.push_back({ t0 + s * kSixteenth, Event::Filter, uint8_t(Bass), 0,
                            float(std::clamp(-0.3 + 0.5 * e0 + 0.9 * std::cos(kTwoPi * s / cycle), -1.0, 1.0)) });
    }
    for (int beat = 0; beat < 4; ++beat)
    {
        const double e = e0 + (e1 - e0) * beat / 4.0;
        const double ramp = (bar - sec.startBar + beat / 4.0) / sec.bars;
        const double pos = bar * 4 + beat;
        auto put = [&](int tr, double v) { out.push_back({ t0 + beat * kPPQ, Event::Filter, uint8_t(tr), 0, float(std::clamp(v, -1.0, 1.0)) }); };
        put(Pad, -0.6 + 0.8 * e + .15 * std::sin(kTwoPi * pos / 128 + 1.1));
        put(Chord, -0.5 + 0.9 * e + spec_.chordSweep * std::sin(kTwoPi * pos / 96 + 2.0));
        put(Arp, -0.7 + 1.3 * e + .25 * std::sin(kTwoPi * pos / 64));
        if (!wobbles) put(Bass, spec_.wobble > 0 ? 1.0 : -0.3 + 0.5 * e); // a wobbling bass is open between its wobbles
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
        const auto& m = modeAt(bar);
        const int r = rootAt(bar), lo = pitchOf(bar, 0, 36, 47, 36);
        want = { lo, lo + degreeSemis(m, r + 4) - degreeSemis(m, r) };
        for (int p : voice(bar, 3, 55, 76, true, padVoices_)) want.push_back(p);
    }
    // On a new chord its third arrives late: the voice holds the fourth over the bar line and falls
    // to the third on beat three (a suspension), once the music has some motion.
    int suspended = -1, sus = -1;
    if (!want.empty() && bar > 0 && rootAt(bar) != rootAt(bar - 1) && energyAt(bar) > .3f)
    {
        const int third = (tonicAt(bar) + degreeSemis(modeAt(bar), rootAt(bar) + 2)) % 12;
        for (size_t i = 2; i < want.size(); ++i)
            if (want[i] % 12 == third)
            {
                const int up = want[i] + degreeSemis(modeAt(bar), rootAt(bar) + 3) - degreeSemis(modeAt(bar), rootAt(bar) + 2);
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
    if (active(Harm, bar) && spec_.gaze)
    {   // open and wide: the root, its fifth in the mode, and above the octave the ninth, or the
        // eleventh where the ninth is only a half step (phrygian's flat second)
        const auto& m = modeAt(bar);
        const int r = rootAt(bar), pc = (tonicAt(bar) + degreeSemis(m, r)) % 12;
        const int root = 60 + (pc - 60 % 12 + 12) % 12;
        const int fifth = (degreeSemis(m, r + 4) - degreeSemis(m, r) + 24) % 12;
        const int ninth = (degreeSemis(m, r + 1) - degreeSemis(m, r) + 24) % 12;
        int top = root + 12 + (ninth == 2 ? 2 : (degreeSemis(m, r + 3) - degreeSemis(m, r) + 24) % 12);
        if (top > 84) top -= 12;
        want = { root, root + fifth, top };
        std::sort(want.begin(), want.end());
        want.erase(std::unique(want.begin(), want.end()), want.end());
    }
    else if (active(Harm, bar)) want = voice(bar, 3, spec_.harmLo, spec_.harmHi, true, harmVoices_);
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

void GenreComposer::emitDrone(int bar, std::vector<Event>& out)
{
    // The second guitar: the chord held as long as it lasts (root, fifth, octave, and the ninth
    // where it is a whole step), notes it shares with the next chord ringing on, into the feedback.
    const int64_t t0 = int64_t(bar) * kBarTicks;
    std::vector<int> want;
    if (active(Drone, bar))
    {
        const auto& m = modeAt(bar);
        const int r = rootAt(bar), root = 40 + ((tonicAt(bar) + degreeSemis(m, r)) % 12 - 40 % 12 + 12) % 12;
        const int fifth = (degreeSemis(m, r + 4) - degreeSemis(m, r) + 24) % 12, ninth = (degreeSemis(m, r + 1) - degreeSemis(m, r) + 24) % 12;
        want = { root, root + fifth, root + 12 };
        if (ninth == 2) want.push_back(root + 14);
    }
    for (int p : droneHeld_)
        if (std::find(want.begin(), want.end(), p) == want.end()) out.push_back({ t0, Event::NoteOff, Drone, uint8_t(p), 0 });
    // strummed, low string first, about 10 ms apart: a whole chord struck at once cost the sampled guitar 19 ms of one
    // audio block (Doom, 4 notes; the Mac dropped out, 2026-11-22), a string at a time spreads it over several
    const int64_t strum = std::max<int64_t>(1, std::llround(kPPQ * bpm_ / 6000.0));
    int64_t at = t0;
    for (int p : want)
        if (std::find(droneHeld_.begin(), droneHeld_.end(), p) == droneHeld_.end()) { out.push_back({ at, Event::NoteOn, Drone, uint8_t(p), .8f }); at += strum; }
    droneHeld_ = want;
    if (bar + 1 == totalBars())
    {
        for (int p : droneHeld_) out.push_back({ t0 + kBarTicks, Event::NoteOff, Drone, uint8_t(p), 0 });
        droneHeld_.clear();
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
        if ((tonicAt(bar) + degreeSemis(modeAt(bar), d)) % 12 == pitch % 12) return true;
    return false;
}

int GenreComposer::subPitch(int bar, char c) const
{
    // the 808: the root low (41 to 78 Hz), a digit the scale degree above it, ^ the root an octave up
    const auto& m = modeAt(bar);
    const int r = lowRootAt(bar), root = lowPitch(bar, 0, 28, 39, 24);
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
    const bool drum = track == Pulse || track == Metal || track == Tick || track == Perc || track == Ground || track == Body || track == Mallet;
    const bool tuned = track == Metal || track == Perc || track == Body; // the fills walk down their chord tones
    // The part's end: the low instruments play its figure together (emitAttack), the drums stop after a beat.
    const bool attack = attackBar(bar);
    if (attack && (track == Bass || track == Sub || track == Chord || track == Ground || track == Body || track == Pulse)) return;
    // A unison summit: the bass and the 808 play the riff with the guitar (emitRiff), the chords make room.
    if (summitUnison(bar) && (track == Bass || track == Sub || (track == Chord && in < sec.bars / 2))) return;
    if (track == Bass && spec_.walk) { emitWalk(bar, out); return; }
    const bool halfT = summitHalftime(bar);
    const int hm = halfT ? 2 : 1;
    const bool cutHold = bar + 1 < totalBars() && (attackBar(bar + 1) || summitUnison(bar + 1)); // the next bar plays its own low notes
    // half time doubles a note's length; when the next bar changes tier its own pattern takes over at the bar line
    const bool capBar = cutHold || (halfT && bar + 1 < totalBars() && tier(bar + 1) != tier(bar));
    auto cap = [&](int64_t t, int64_t l) { return capBar ? std::min(l, t0 + kBarTicks - t - 10) : l; };
    // a half-time note held over several bars (2026-11-20, Hollow Saint): it stops before a later bar that plays its own
    // low notes (the part's end figure, a unison) or another part, which cap only checks for the next bar
    auto capHalf = [&](int64_t t, int64_t l) {
        if (!halfT) return l;
        for (int b = bar + 2; b < totalBars() && int64_t(b) * kBarTicks < t + l; ++b)
            if (attackBar(b) || summitUnison(b) || sectionOfBar_[size_t(b)] != si) return std::max<int64_t>(10, int64_t(b) * kBarTicks - 10 - t);
        return l;
    };

    // The drums in phrases of sixteen bars: a turn at the end of every fourth bar, a fill at the
    // eighth, a bigger one at the sixteenth (and into a section that rises), the last hit of each
    // fill thrown into the echo.
    const int ph = in % 16;
    const bool fill16 = drum && ph == 15 && !attack;
    const bool fill8 = drum && ph == 7 && !fill16 && !attack;
    const bool turn = drum && ph % 4 == 3 && !fill8 && !fill16 && !attack;
    // A section the percussion enters: the ticks first, the metal a quarter in, the perc at half.
    const bool entering = !spec_.build && si > 0 && !(sections_[size_t(si - 1)].layers & (bit(Metal) | bit(Tick) | bit(Perc)));
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
        if (attack && beat >= 1) break;
        if (gapBeat_ == beat && track != Pad) continue;
        if (stutterBeat_ == beat && track == spec_.stutterTrack) continue;
        const int src = drum ? i + (chopMap_[size_t(beat)] - beat) * P / 4 : i;
        // The section's cells develop, phrase by phrase of eight bars: the hand drums displaced by a
        // dotted eighth every other phrase, the mallet phasing a sixteenth further each phrase
        // (Reich), the ground thinned to its first stroke at the fourth phrase.
        const int phrase = in / 8;
        const int rot = track == Body ? (phrase % 2) * 3 : track == Mallet ? phrase % P : 0;
        if (track == Ground && phrase % 4 == 3 && i > P / 2) break;
        // a half-time summit reads every pattern at half speed: each step lasts two
        const int64_t v = int64_t(in) * P + src;
        if (halfT && (v & 1)) continue;
        const int64_t pos = halfT ? int64_t(sec.startBar) * P + v / 2 : int64_t(bar) * P + src;
        const int at = int(((pos + rot) % len + len) % len);
        const char c = euclid ? (((i + percRot_) * k) % P < k ? 'e' : '.') : pat.s[at];
        if (c == '.' || c == '-') continue;
        if (live && track != Pulse && (i & 1)) continue; // half the percussion: the live kit has the room
        int holds = 0;
        if (!drum) while (holds + 1 < len && ((track != Sub && !cutHold) || i + holds + 1 < P) && pat.s[(at + holds + 1) % len] == '-') ++holds; // the 808 within its bar: the next bar may play another pattern
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
        const int64_t noteLen = capHalf(t, cap(t0 + i * stepT, int64_t(double(stepT * hm * (1 + holds)) * (spec_.legato ? 1.0 : holds ? .95 : .7)) - (spec_.legato ? 10 : 0)));
        if (track == Sub)
        {
            // Legato into a different note: the next one starts before this one ends, and the 808 slides.
            const int p = subPitch(bar, c), at2 = i + 1 + holds;
            const int nextBar = bar + at2 / P;
            const Pat& np = at2 < P ? pat : part.tier[size_t(tier(std::min(nextBar, totalBars() - 1)))];
            const int nl = int(std::strlen(np.s));
            const char nc = nl == 0 ? '.' : np.s[(int64_t(nextBar) * np.perBar + at2 % P) % nl];
            const bool slide = !halfT && !capBar && nc != '.' && nc != '-' && nextBar < totalBars() && active(Sub, nextBar) && subPitch(nextBar, nc) != p;
            const int64_t late = t - (t0 + i * stepT); // the swing: the note ends on the grid all the same
            hit(t, p, vel, cap(t, stepT * hm * (1 + holds) - late + (slide ? 40 : -10)));
            continue;
        }
        if (track == Texture) // held: the scrape, the rattle, the crackle, on a chord tone
        {
            const int deg = c >= '1' && c <= '9' ? c - '1' : 0;
            hit(t, ladderPitch(bar, deg, 48, 84), vel, cap(t, std::max<int64_t>(stepT, stepT * hm * (1 + holds) - 10)));
            continue;
        }
        if (track == Bass)
        {
            const int deg = c >= '1' && c <= '9' ? c - '1' : 0;
            const auto& m = modeAt(bar);
            const int r = lowRootAt(bar), root = lowPitch(bar, 0, 40, 51, 36); // written an octave up: the bass patches sound an octave down
            hit(t, root + degreeSemis(m, r + deg) - degreeSemis(m, r), vel, noteLen);
        }
        else // the chord, voice-led: four voices of it (rootless when it has a ninth: the bass has the root)
            for (int p : voice(bar, 4, 48, 74, spec_.ext == 2, chordVoices_)) hit(t, p, vel, noteLen);
    }

    // The bass walks into a new chord: on the last eighth of the bar, when the pattern rests there,
    // the scale step next to the coming root, from the side it is coming from.
    if (track == Bass && P == 16 && bar + 1 < totalBars() && active(Bass, bar + 1) && lowRootAt(bar + 1) != lowRootAt(bar) && !attack && !halfT
        && pat.s[(int64_t(bar) * P + 14) % len] == '.' && pat.s[(int64_t(bar) * P + 15) % len] == '.' && energyAt(bar) > .3f)
    {
        const auto& m = modeAt(bar);
        const int next = lowPitch(bar + 1, 0, 40, 51, 36), now = lowPitch(bar, 0, 40, 51, 36);
        const int r1 = lowRootAt(bar + 1), step = next > now ? -1 : 1;
        const int p = next + degreeSemis(m, r1 + step) - degreeSemis(m, r1);
        if (p >= 40 && p <= 64 && tonicAt(bar + 1) == tonicAt(bar)) hit(t0 + 14 * six, p, .7f, 2 * six - 20);
    }

    // What grows and what turns, on the sixteenth grid only (the triplet parts keep their own).
    if (drum && P == 16 && !live)
    {
        auto add = [&](int i, int pitch, float vel) {
            const int beat = i / 4;
            if (struck[size_t(i)] || i >= fillFrom || beat == gapBeat_ || (beat == stutterBeat_ && track == spec_.stutterTrack)) return;
            if (attack && beat >= 1) return;
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

    if (stutterBeat_ >= 0 && track == spec_.stutterTrack && !attack && stutterBeat_ * 4 < fillFrom)
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
    else if (in % spec_.arpMorph == 0 && rng_.chance(.3 + .4 * energyAt(bar)))
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
        if (tradeTurn(bar) == 1) break; // the soloist's two bars: the harpsichord listens
        out.push_back({ notes[j].first, Event::NoteOn, Arp, uint8_t(notes[j].second), vel });
        out.push_back({ notes[j].first + len, Event::NoteOff, Arp, uint8_t(notes[j].second), 0 });
    }
}

void GenreComposer::emitWalk(int bar, std::vector<Event>& out)
{
    // The contrabass walks (Kerem, 2026-11-01: "contra bass jazz walks"): a note on every beat, the
    // chord's root on the one, on two and three steps of the mode toward the next root (or up or down
    // through the chord when it is near), and on four the half step into the next bar's root from the
    // side the line comes from. In the calm it plays in two: the root and the fifth. Now and then
    // a beat is raked: a ghosted, swung eighth before it on the same note.
    const int64_t t0 = int64_t(bar) * kBarTicks;
    const int lo = 28, hi = 50; // E1 to D3, sounding where it is written
    const auto& m = modeAt(bar);
    const int r = lowRootAt(bar);
    auto nearest = [&](int pc, int to) {
        int best = -1;
        for (int p = lo + ((pc - lo) % 12 + 12) % 12; p <= hi; p += 12)
            if (best < 0 || std::abs(p - to) < std::abs(best - to)) best = p;
        return best;
    };
    auto fold = [&](int p) { while (p > hi) p -= 12; while (p < lo) p += 12; return p; };
    auto put = [&](int64_t t, int p, float vel, int64_t len) {
        out.push_back({ t, Event::NoteOn, Bass, uint8_t(fold(p)), std::clamp(vel, .05f, 1.0f) });
        out.push_back({ t + len, Event::NoteOff, Bass, uint8_t(fold(p)), 0 });
    };
    const int root = nearest((tonicAt(bar) + degreeSemis(m, r)) % 12, walkLast_ >= 0 ? walkLast_ : 36);
    const int third = (degreeSemis(m, r + 2) - degreeSemis(m, r) + 24) % 12, fifth = (degreeSemis(m, r + 4) - degreeSemis(m, r) + 24) % 12;
    const float lv = .72f + .2f * energyAt(bar);
    auto jitter = [&] { return float(rng_.uniform() * .06 - .03); };
    if (tier(bar) == 0) // in two
    {
        const int f = fold(root + fifth > hi ? root + fifth - 12 : root + fifth);
        put(t0, root, lv + jitter(), 2 * kPPQ - 30);
        put(t0 + 2 * kPPQ, f, lv - .08f + jitter(), 2 * kPPQ - 30);
        walkLast_ = f;
        return;
    }
    auto stepIn = [&](int p, int d) { int q = p + d; while (!inMode(bar, q)) q += d; return q; };
    const int nb = std::min(bar + 1, totalBars() - 1);
    int target = nearest((tonicAt(nb) + degreeSemis(m, lowRootAt(nb))) % 12, root);
    if (target == root && root + 12 <= hi && rng_.chance(.3)) target += 12; // the chord holds: the line climbs to its octave
    // the half step in; before a build (it plays its own low notes), a part without the bass, or the end, a step of the mode instead
    const bool into = bar + 1 < totalBars() && !attackBar(bar + 1) && active(Bass, bar + 1);
    const int side = target < root ? 1 : target > root ? -1 : rng_.chance(.5) ? 1 : -1;
    const int approach = into ? target + side : stepIn(target, side);
    const int dir = approach >= root ? 1 : -1;
    const int s2 = stepIn(root, dir), s3 = stepIn(s2, dir);
    int p2, p3;
    if (std::abs(approach - root) >= 4 && (dir > 0 ? s3 < approach : s3 > approach) && rng_.chance(.7)) { p2 = s2; p3 = s3; }
    else if (root + fifth <= hi) { p2 = root + third; p3 = root + fifth; }  // up through the chord
    else { p2 = root - (12 - fifth); p3 = root - (12 - third); }          // down through it
    const int notes[4] = { root, p2, p3, approach };
    const float accent[4] = { 0, -.06f, -.03f, -.05f };
    const bool rake = rng_.chance(.12 + .2 * energyAt(bar)); // the ghost before beat three
    for (int b = 0; b < 4; ++b)
    {
        const int64_t len = rake && b == 1 ? kPPQ * 2 / 3 - 20 : kPPQ - 25;
        put(t0 + b * kPPQ, notes[b], lv + accent[b] + jitter(), len);
    }
    if (rake) put(t0 + kPPQ + kPPQ * 2 / 3, notes[2], .38f, kPPQ / 3 - 20);
    walkLast_ = fold(approach);
}

void GenreComposer::emitLead(int bar, std::vector<Event>& out)
{
    // The soloist (Kerem, 2026-11-01: "a lead solo instrument like a saxophone solo"; 2026-11-02: "a haunted
    // opera voice as a lead soloist so it connects the parts"; 2026-11-09: "a leading soloist that connects
    // parts, continuous, but changing with the parts"): the Soloist's line from the fourth bar on.
    if (bar < 4) return;
    if (spec_.quietEdges && (sectionOfBar_[size_t(bar)] == 0 || sectionOfBar_[size_t(bar)] + 1 == int(sections_.size()))) return;
    auto& sec = sectionAt(bar);
    const int64_t t0 = int64_t(bar) * kBarTicks;
    const bool theme = spec_.hook && spec_.lead != Arp && active(Arp, bar); // the hook plays: the soloist sings long notes under it
    // where it falls silent: the stop's hit, a fall's cut (its last note into the echoes), all of the hole's bar
    const Turn tn = turn_[size_t(bar)];
    int64_t stopAt = tn == Turn::Stop ? t0 + 4 * kSixteenth : tn == Turn::Echo ? t0 + leadSix_ * kSixteenth : tn == Turn::Hole ? t0 : -1;
    // the arpeggio's answer, or a part of the break and the bass alone: the soloist listens
    if ((tradeTurn(bar) == 2 || (spec_.breaks && !(sec.layers & (bit(Pad) | bit(Arp)))))) stopAt = t0;
    solo_.play({ t0, energyAt(bar), sectionOfBar_[size_t(bar)], bar - sec.startBar, sec.bars, sec.restoreTheme, awayBar_[size_t(bar)], theme,
                 bar + 1 == totalBars(), chordTones(bar, false), chordTones(std::min(bar + 1, totalBars() - 1), false),
                 [&](int p) { return inMode(bar, p); }, stopAt },
               leadRng_, out);
}

void GenreComposer::emitBoom(int bar, std::vector<Event>& out)
{
    // the low hits (Sentence) wherever the music has some motion, none in a part change's silence nor on its
    // end figure (the band's hits are there)
    auto& sec = sectionAt(bar);
    if (sec.energy < .3f || bar < 4 || attackBar(bar)) return;
    const int64_t t0 = int64_t(bar) * kBarTicks;
    const Turn tn = turn_[size_t(bar)];
    const int64_t quiet = tn == Turn::Hole ? t0 : cuts(bar) ? t0 + leadSix_ * kSixteenth : -1;
    const bool last = bar + 1 == sec.startBar + sec.bars && bar + 1 < totalBars(); // a part's last bar closes the sentence: its pickup leads in
    for (auto& h : booms_.bar(last ? 7 : bar - sec.startBar, energyAt(bar)))
    {
        if (quiet >= 0 && t0 + h.at >= quiet - 20) continue;
        const int p = lowPitch(bar, h.fifth ? 4 : 0, 26, 40, 24);
        out.push_back({ t0 + h.at, Event::NoteOn, Boom, uint8_t(p), h.vel });
        out.push_back({ t0 + h.at + kPPQ / 4, Event::NoteOff, Boom, uint8_t(p), 0 });
    }
}

bool GenreComposer::summitHalftime(int bar) const
{
    return (spec_.summit == Summit::Halftime && sectionAt(bar).restoreTheme) || halfSec_[size_t(sectionOfBar_[size_t(bar)])];
}

void GenreComposer::emitMotif(int bar, std::vector<Event>& out)
{
    // The song before's theme, in this song's Seed (spec.motif): from the fourth bar, slowed to half speed, every
    // other four bars, softly; it stops two bars before the Seed ends, so the new song's own parts arrive clean.
    const auto& sec = sectionAt(bar);
    const int in = bar - sec.startBar;
    if (in < 4 || bar >= sec.startBar + sec.bars - 2 || ((in - 4) / 4) % 2 == 1) return;
    const int within = (in - 4) % 4;
    const int64_t t0Cycle = int64_t(bar - within) * kBarTicks, six = 2 * kSixteenth;
    const auto& m = modeAt(bar);
    const int r = rootAt(bar), lo = spec_.arpLo, hi = spec_.arpHi;
    const int rootP = pitchOf(bar, 0, lo + 5, lo + 16, 12 * (lo / 12));
    for (int i = 0; i < spec_.motifN; ++i)
    {
        const auto& n = spec_.motif[size_t(i)];
        const int64_t t = t0Cycle + n.step * six;
        if (t < int64_t(bar) * kBarTicks || t >= int64_t(bar + 1) * kBarTicks) continue;
        int p = rootP + degreeSemis(m, r + n.deg) - degreeSemis(m, r);
        while (p > hi) p -= 12;
        while (p < lo) p += 12;
        const int64_t len = std::max<int64_t>(kSixteenth, n.len * six - 12);
        out.push_back({ t, Event::NoteOn, Arp, uint8_t(p), n.vel * .6f });
        out.push_back({ t + len, Event::NoteOff, Arp, uint8_t(p), 0 });
    }
}

bool GenreComposer::summitUnison(int bar) const
{
    // the riff, the bass and the 808 as one instrument for three quarters of the summit, then each its own way again
    auto& sec = sectionAt(bar);
    return spec_.summit == Summit::Unison && spec_.riff != RiffStyle::None && sec.restoreTheme && bar - sec.startBar < sec.bars * 3 / 4;
}

int GenreComposer::lowRootAt(int bar) const
{
    // a pedal: the low end holds the tonic under three quarters of the summit while the chords move above it
    if (size_t(bar) < pedal_.size() && pedal_[size_t(bar)] >= 0) return pedal_[size_t(bar)]; // leaning into the next part
    auto& sec = sectionAt(bar);
    if (spec_.summit == Summit::Pedal && sec.restoreTheme && bar - sec.startBar < sec.bars * 3 / 4) return 0;
    return rootAt(bar);
}

int GenreComposer::lowPitch(int bar, int deg, int lo, int hi, int base) const
{
    return foldInto(base + tonicAt(bar) + degreeSemis(modeAt(bar), lowRootAt(bar) + deg), lo, hi);
}

int GenreComposer::over(int bar, int deg, int up) const
{
    const auto& m = modeAt(bar);
    const int r = lowRootAt(bar);
    return degreeSemis(m, r + deg + up) - degreeSemis(m, r + deg);
}

int GenreComposer::riffRoot(int bar, int deg) const
{
    // the root folded into the riff's octave first, the degree's distance added after, so the riff keeps its contour
    const auto& m = modeAt(bar);
    const int r = lowRootAt(bar);
    return lowPitch(bar, 0, spec_.riffLo, spec_.riffLo + 11, 12 * (spec_.riffLo / 12)) + degreeSemis(m, r + deg) - degreeSemis(m, r);
}

void GenreComposer::powerChord(int track, int64_t t, int64_t len, int root, int fifth, float vel, std::vector<Event>& out) const
{
    // the root, the mode's fifth over it and the octave (muted: root and fifth); the guitar tunes a
    // perfect fifth pure, and a diminished one on the dark degrees is the tritone doom leans on
    const bool palm = vel < .5f;
    for (int iv : { 0, fifth, 12 })
    {
        if (palm && iv == 12) continue;
        out.push_back({ t, Event::NoteOn, uint8_t(track), uint8_t(root + iv), vel });
        out.push_back({ t + len, Event::NoteOff, uint8_t(track), uint8_t(root + iv), 0 });
    }
}

void GenreComposer::makeRiff()
{
    // Two bars of sixteenths. Velocities here are dynamics (0..1); muted notes are sent under .5.
    riff_.clear();
    auto add = [&](int step, int len, int deg, float vel, bool palm) { riff_.push_back({ step, len, deg, vel, palm }); };
    switch (spec_.riff)
    {
        case RiffStyle::Doom:
        {
            // the chord, let ring; a figure moving off it (the flat second in phrygian, the third, the
            // fourth, the fifth, a step under) and home; a muted pair of roots before a chord now and then
            static const int degs[] = { 1, 1, 2, 3, 4, -1 };
            const int first = rng_.range(6, 10);
            add(0, first, 0, 1.0f, false);
            int step = first;
            while (step < 28)
            {
                if (rng_.chance(.35)) { add(step, 1, 0, .7f, true); add(step + 1, 1, 0, .75f, true); step += 2; }
                const int len = std::min(rng_.range(2, 6), 32 - step);
                add(step, len, degs[size_t(rng_.range(0, 5))], .85f, false);
                step += len;
            }
            if (step < 32) add(step, 32 - step, 0, .8f, false);
            break;
        }
        case RiffStyle::Chug:
        {
            // muted sixteenths on the root; open chords on a grouping of threes and twos, each bar its own
            static const int groups[][7] = { { 3, 3, 2, 3, 3, 2, 0 }, { 3, 3, 3, 3, 2, 2, 0 }, { 2, 3, 3, 2, 3, 3, 0 }, { 3, 3, 2, 2, 3, 3, 0 } };
            static const int degs[] = { 0, 1, 2, 3, -1 };
            for (int half = 0; half < 2; ++half)
            {
                const int* g = groups[rng_.range(0, 3)];
                int at = half * 16;
                for (int k = 0; g[k] && at < half * 16 + 16; ++k)
                {
                    add(at, std::min(2, g[k]), k == 0 ? 0 : degs[size_t(rng_.range(0, 4))], .95f, false);
                    for (int j = 2; j < g[k] && at + j < half * 16 + 16; ++j)
                        if (rng_.chance(.85)) add(at + j, 1, 0, .6f + .3f * float(j) / float(g[k]), true);
                    at += g[k];
                }
            }
            break;
        }
        case RiffStyle::Sludge:
        {
            // one chord a bar, let ring; the second bar moves (the fourth, the flat second, the fifth, the third)
            add(0, 14, 0, 1.0f, false);
            add(14, 2, 0, .65f, true);
            const int d = std::array<int, 4> { 3, 1, 4, 2 }[size_t(rng_.range(0, 3))];
            const bool split = rng_.chance(.5);
            add(16, split ? 10 : 16, d, .95f, false);
            if (split) add(26, 6, rng_.chance(.5) ? 1 : -1, .85f, false);
            break;
        }
        case RiffStyle::Tremolo:
        {
            // the northern line (after Transilvanian Hunger, Spectral Wound, in their feel): the top
            // string, two notes a bar, moving by step through the mode above the chord's root and
            // landing on its chord tones each two bars (the fifth, the octave, the tenth, the twelfth),
            // closing on the octave or the fifth; the summit gets a line of its own
            for (auto* mel : { &melA_, &melB_ })
            {
                mel->clear();
                int d = rng_.chance(.5) ? 7 : 9;
                for (int i = 0; i < 16; ++i)
                {
                    if (i == 15) d = rng_.chance(.6) ? 7 : 4;
                    else if (i > 0)
                    {
                        static const int steps[] = { -1, 1, -1, -2, 2, 0, -1 };
                        d = std::clamp(d + steps[size_t(rng_.range(0, 6))], 4, 11);
                        if (i % 4 == 0)
                        {
                            int best = 7;
                            for (int c : { 4, 7, 9, 11 }) if (std::abs(c - d) < std::abs(best - d)) best = c;
                            d = best;
                        }
                    }
                    mel->push_back(d);
                }
            }
            break;
        }
        case RiffStyle::Stoner:
        {
            // fast stoner (Working Class Curse): the root chugged in muted eighths under open chords, one
            // on the push (the and of two, or of three); the first bar turns to the third or the fourth,
            // the second bar's back half runs the minor's dark degrees, power chords all
            static const int runs[][4] = { { 2, 3, 4, 3 }, { 2, 3, 2, -1 }, { 6, 4, 3, 2 }, { 3, 4, 6, 4 } };
            const int push = rng_.chance(.5) ? 6 : 10, turn = rng_.chance(.5) ? 2 : 3;
            const int* run = runs[rng_.range(0, 3)];
            for (int half = 0; half < 2; ++half)
            {
                const int b = half * 16;
                add(b, 2, 0, 1.0f, false);
                for (int s = 2; s < (half ? 8 : 12); s += 2)
                    if (s == push) add(b + s, 2, 0, .9f, false);
                    else add(b + s, 1, 0, .65f, true);
                if (!half) { add(12, 2, turn, .9f, false); add(14, 1, 0, .7f, true); }
            }
            for (int k = 0; k < 4; ++k) add(24 + 2 * k, 2, run[k], k == 3 ? 1.0f : .85f, false);
            break;
        }
        default: break; // the drone follows the chords, bar by bar
    }
}

std::vector<GenreComposer::RHit> GenreComposer::riffHits(int bar) const
{
    std::vector<RHit> h;
    const int64_t t0 = int64_t(bar) * kBarTicks;
    const int tr = tier(bar);
    auto& sec = sectionAt(bar);
    const int in = bar - sec.startBar;
    if (spec_.riff == RiffStyle::Tremolo)
    {
        // picked eighths in the calm, sixteenths as it drives, thirty-seconds at the height; the beats leaning
        const int n = tr == 0 ? 8 : tr == 1 ? 16 : 32;
        const int64_t d = kBarTicks / n;
        // played, not sequenced (Kerem, 2026-11-23: "not sounding like a machine"): the hand drifts a few ms ahead and
        // behind over the bar, each stroke a little off that, the upstroke later and lighter than the down, and the
        // phrase breathes: harder into each two bars' head, easing off through them
        Rng hand(uint64_t(bar) * 0x9E3779B97F4A7C15ull + uint64_t(tonicPc_) * 131 + 17);
        const double drift = hand.uniform() * 6.283;
        const double ms = kPPQ * bpm_ / 60000.0; // ticks in a millisecond
        for (int i = 0; i < n; ++i)
        {
            const double ph = double(i) / n;
            int64_t off = int64_t(std::lround(ms * (2.5 * std::sin(drift + 6.283 * ph) + (hand.uniform() * 3 - 1.5) + ((i & 1) ? 1.5 : 0))));
            if (i == 0) off = std::max<int64_t>(0, off);
            const float head = (bar & 1) ? .9f - .08f * float(ph) : 1.0f - .1f * float(ph);
            const float v = (i % (n / 4) == 0 ? .85f : (i & 1) ? .55f : .65f) * head + float(hand.uniform() * .08 - .04);
            h.push_back({ t0 + i * d + off, 0, 0, std::clamp(v, .5f, 1.0f), false });
        }
        // each stroke rings up to the next one (the same strings: they must not overlap), the bar's last to the bar line
        for (size_t k = 0; k < h.size(); ++k) h[k].len = (k + 1 < h.size() ? h[k + 1].t : t0 + kBarTicks) - h[k].t - 8;
        return h;
    }
    if (spec_.riff == RiffStyle::Drone) { h.push_back({ t0, kBarTicks - 10, 0, tr == 2 ? .9f : tr == 1 ? .7f : .5f, false }); return h; }
    if (riff_.empty()) return h;
    // the breakdowns and a half-time summit play the riff at half speed: a bar takes half of it
    const bool half = awayBar_[size_t(bar)] || summitHalftime(bar);
    const int span = half ? 8 : 16, from = (in * span) % 32, mul = half ? 2 : 1;
    for (auto& n : riff_)
    {
        if (n.step < from || n.step >= from + span) continue;
        if (tr == 0 && n.step != 0) continue;                    // the calm: only its first chord, ringing on
        if (tr == 1 && in % 4 == 3 && n.step != from) continue;  // every fourth bar a breath: only the bar's first chord
        const int64_t len = tr == 0 ? kBarTicks : int64_t(n.len) * kSixteenth * mul;
        if (spec_.legato && n.palm) continue;
        h.push_back({ t0 + int64_t(n.step - from) * kSixteenth * mul, len - 12, n.deg, n.vel, n.palm });
    }
    if (spec_.legato) // each chord rings into the next, the last to the bar line
        for (size_t k = 0; k < h.size(); ++k) h[k].len = (k + 1 < h.size() ? h[k + 1].t : t0 + kBarTicks) - h[k].t - 12;
    return h;
}

void GenreComposer::emitRiff(int bar, std::vector<Event>& out)
{
    if (spec_.riff == RiffStyle::None || attackBar(bar)) return;
    const bool unison = summitUnison(bar);
    if (!active(Riff, bar) && !unison) return;
    const auto& m = modeAt(bar);
    const int r = lowRootAt(bar);
    auto note = [&](int track, int64_t t, int64_t len, int p, float vel) {
        out.push_back({ t, Event::NoteOn, uint8_t(track), uint8_t(p), vel });
        out.push_back({ t + len, Event::NoteOff, uint8_t(track), uint8_t(p), 0 });
    };
    const int64_t barEnd = int64_t(bar + 1) * kBarTicks - 12;
    const bool cut = bar + 1 >= totalBars() || attackBar(bar + 1) || sectionOfBar_[size_t(bar + 1)] != sectionOfBar_[size_t(bar)];
    auto rlen = [&](const RHit& x) { return cut ? std::min(x.len, barEnd - x.t) : x.len; };
    const bool cutLow = cut || !summitUnison(bar + 1);
    auto lowLen = [&](const RHit& x, int64_t l) { return cutLow ? std::min(l, barEnd - x.t) : l; };
    for (auto& x : riffHits(bar))
    {
        // the guitar's velocity: under .5 muted, the dynamics within each half
        const float v = x.palm ? .2f + .25f * x.vel : .5f + .5f * x.vel;
        const int root = riffRoot(bar, x.deg);
        if (active(Riff, bar))
        {
            if (spec_.riff == RiffStyle::Tremolo)
            {
                // calm: the open shimmer (root, fifth, ninth) let ring; driving: the power chord with the
                // northern line on the top string, at the height a third under it as a second guitar
                std::vector<int> ps { root, root + over(bar, x.deg, 4) };
                if (tier(bar) == 0) ps.push_back(root + over(bar, x.deg, 8));
                else
                {
                    const auto& mel = sectionAt(bar).restoreTheme && spec_.summit == Summit::NewTheme ? melB_ : melA_;
                    const int half = int((x.t - int64_t(bar) * kBarTicks) * 2 / kBarTicks);
                    const int d = mel[size_t(((bar - sectionAt(bar).startBar) * 2 + half) % 16)];
                    // the line glides: it takes the octave nearest its last note, so a chord's root
                    // folding to another octave does not throw it a ninth away
                    int top = root + over(bar, x.deg, d);
                    if (lastTop_ > 0)
                    {
                        while (top - lastTop_ > 6 && top - 12 > ps[1]) top -= 12;
                        while (lastTop_ - top > 6 && top + 12 <= 76) top += 12;
                    }
                    lastTop_ = top;
                    const int under = top - (over(bar, x.deg, d) - over(bar, x.deg, d - 2));
                    // the second guitar's third under the line: at the height, but in a dark summit only from its
                    // second half, so the repeated line opens up
                    const auto& sc = sectionAt(bar);
                    const bool second = tier(bar) == 2 && (!dark(bar) || (sc.restoreTheme && bar - sc.startBar >= sc.bars / 2));
                    for (int p : { top, second ? under : -1 })
                        if (p > ps[1] && std::find(ps.begin(), ps.end(), p) == ps.end()) ps.push_back(p);
                }
                for (int p : ps) note(Riff, x.t, rlen(x), p, v);
            }
            else powerChord(Riff, x.t, rlen(x), root, over(bar, x.deg, 4), v, out);
        }
        if (unison)
        {
            const int diff = degreeSemis(m, r + x.deg) - degreeSemis(m, r);
            if (active(Bass, bar)) note(Bass, x.t, lowLen(x, x.palm ? kSixteenth - 20 : x.len), up(lowPitch(bar, 0, 40, 51, 36) + diff, 40), x.palm ? .55f : .9f);
            if (active(Sub, bar) && !x.palm) note(Sub, x.t, lowLen(x, x.len), up(lowPitch(bar, 0, 28, 39, 24) + diff, 24), .9f);
        }
    }
}

void GenreComposer::planEnds()
{
    // Each section's end gets a figure: into a rise one that pushes (sixteenths swelling to the bar
    // line, threes across the bar, a roll), into a fall one that stops (hits and then air, stabs,
    // silence and three late hits). Never the same twice in a row; into a big rise two bars, threes first.
    //
    // 2026-10-31 (Kerem): into a big arrival (the summit, or a part of .65 and up clearly louder) no
    // figure: the part empties instead, one instrument out 16 bars before, the bass at 8, one more at
    // 4, and everything comes back on the new part's downbeat.
    //
    // 2026-11-01 (Kerem, Halftime: the attacks more obvious, the next part whole after them, "2, 4, 8"):
    // with spec.build every part ends in three bars of the low instruments in unison, two hits, then
    // four, then eight, and only the voice above leaves 16 bars before a big arrival.
    endFig_.assign(size_t(totalBars()), -1);
    drop_.assign(size_t(totalBars()), 0);
    int last = -1;
    for (size_t si = 0; si + 1 < sections_.size(); ++si)
    {
        const auto& sec = sections_[si];
        const auto& nx = sections_[si + 1];
        const int end = sec.startBar + sec.bars - 1;
        if (spec_.build && sec.bars >= 8)
            for (int k = 0; k < 3; ++k) endFig_[size_t(end - 2 + k)] = 6 + k;
        if (nx.restoreTheme || (nx.energy >= .65f && nx.energy > sec.energy + .1f))
        {
            static const std::vector<std::vector<int>> slots[3] = {
                { { Arp }, { Chord }, { Metal, Perc }, { Pad } },     // 16 bars out: a voice above
                { { Bass, Sub }, { Chord }, { Pad } },                 // 8: the bass
                { { Riff }, { Ground, Body, Perc, Metal }, { Pad } },  // 4: the guitar, or the iron
            };
            const int at[3] = { std::min(16, sec.bars / 2), std::min(8, sec.bars / 4), std::min(4, sec.bars / 8) };
            uint32_t mask = 0;
            for (int k = 0; k < (spec_.build ? 1 : 3); ++k)
            {
                if (at[k] < 1) continue;
                for (auto& g : slots[k])
                {
                    uint32_t m = 0;
                    for (int t : g) if ((sec.layers & bit(t)) && !(mask & bit(t))) m |= bit(t);
                    if (!m) continue;
                    mask |= m;
                    for (int b = end - at[k] + 1; b <= end; ++b) drop_[size_t(b)] |= mask;
                    break;
                }
            }
            continue;
        }
        if (!spec_.ends || spec_.build) continue;
        static const int up[] = { 1, 2, 4 }, down[] = { 0, 3, 5 };
        int f;
        do f = nx.energy > sec.energy ? up[rng_.range(0, 2)] : down[rng_.range(0, 2)]; while (f == last);
        last = f;
        endFig_[size_t(end)] = f;
        if ((nx.restoreTheme || nx.energy - sec.energy >= .3f) && sec.bars >= 8) endFig_[size_t(end - 1)] = f == 2 ? 3 : 2;
    }

    // 2026-11-05 (Kerem: "more deterministic part starts ... for main and driving parts, after the attack and
    // silence it should start full blast as a whole part"); 2026-11-07 (Kerem: "organic, musical, not cringe"):
    // every part change one of the turns (Composer.h). Out of a loud part into a driving one the stop: one hit
    // of the whole band, silence to the downbeat. A rise: the build's 2, 4, 8 (spec.build), the last beat or
    // two silent but a held part, the bass walking into the new root. The biggest rise: the build, then a bar
    // left to the live drums. A fall: a falling figure, the echoes ringing over the bar line. Level: the
    // instruments leave one by one, or the low end leans on the next root (spec.pedal).
    std::vector<bool> stops(sections_.size(), false);
    for (size_t si = 1; si < sections_.size(); ++si) stops[si] = drives(int(si)) && sections_[si - 1].energy >= .45f;
    if (spec_.flow) stops.assign(stops.size(), false);
    auto turns = planTurns(stops, spec_.pedal);
    if (spec_.flow) for (auto& t : turns) if (t == Turn::Hole) t = Turn::Pickup; // a held part, the bass walking in: never all silent
    turn_.assign(size_t(totalBars()), Turn::None);
    pedal_.assign(size_t(totalBars()), -1);
    leadSix_ = 16 - 4 * int(std::ceil(0.5 * bpm_ / 60.0 - 1e-6));
    for (size_t si = 1; si < sections_.size(); ++si)
    {
        const auto& sec = sections_[si - 1];
        if (sec.bars < 4) continue;
        const int end = sec.startBar + sec.bars - 1;
        const Turn k = turns[si];
        turn_[size_t(end)] = k;
        const bool rise = k == Turn::Stop || k == Turn::Pickup || k == Turn::Hole;
        if (spec_.build && sec.bars >= 8)
            for (int j = 0; j < 4; ++j) endFig_[size_t(end - 3 + j)] = rise && j < 3 ? 6 + j : -1;
        switch (k)
        {
            case Turn::Stop: endFig_[size_t(end)] = 9; drop_[size_t(end)] = 0; break; // the whole band on the stop
            case Turn::Hole: endFig_[size_t(end)] = -1; break;
            case Turn::Echo: if (spec_.build) endFig_[size_t(end)] = 0; break; // hits and air, the last into the echoes
            case Turn::Thin: case Turn::Pedal:
                endFig_[size_t(end - 1)] = endFig_[size_t(end)] = -1;
                if (k == Turn::Pedal) pedal_[size_t(end - 1)] = pedal_[size_t(end)] = rootAt(end + 1);
                else
                {   // over the last three bars: the voices above, then the middle, then the low drums and the guitar
                    static const uint32_t leave[3] = { bit(Arp) | bit(Mallet) | bit(Tick) | bit(Metal), bit(Chord) | bit(Harm) | bit(Perc) | bit(Body),
                                                       bit(Ground) | bit(Riff) | bit(Pulse) | bit(Impact) };
                    for (int j = 0; j < 3; ++j)
                        for (int b = end - 2 + j; b <= end; ++b) drop_[size_t(b)] |= leave[j];
                }
                break;
            default: break;
        }
    }

    // 2026-11-05 (Kerem: "each song starts with one component, not 3-4 voices; in 10 seconds or so they
    // reach their start"): the first part's layers come in one at a time, in the order its form names
    // them, the last about ten seconds in.
    std::vector<uint32_t> entries;
    for (const char* s = spec_.form[0].layers; *s; ++s)
        if (const char one[2] = { *s, 0 }; layersOf(one)) entries.push_back(layersOf(one));
    const int k = int(entries.size()), span = std::max(1, int(std::lround(10.0 * bpm_ / 240.0)));
    for (int i = 1; i < k; ++i)
        for (int b = 0; b < std::min(int(std::lround(double(i * span) / (k - 1))), sections_[0].bars - 1); ++b)
            drop_[size_t(b)] |= entries[size_t(i)];
}

bool GenreComposer::buildsWith(int track, int bar) const
{
    if (!spec_.build || bar < 0 || bar >= totalBars() || !attackBar(bar)) return false;
    if (track != Riff && track != Bass && track != Sub && track != Chord && track != Ground && track != Body && track != Pulse) return false;
    auto& sec = sectionAt(bar);
    return active(track, sec.startBar + sec.bars);
}

void GenreComposer::emitAttack(int bar, std::vector<Event>& out)
{
    if (!attackBar(bar)) return;
    static const char* kEnds[] = { "X.x.X...X.......", "X.......x.x.xXXX", "X.x.x.x.X.x.x.x.", "X.x...x.x...X.X.", "X.......x...X...", "........X...X...", // straight, no threes
                                   "X.......X.......", "X...X...X...X...", "X.X.X.X.X.X.X.X.", // the build: 2, 4, 8
                                   "X..............." }; // the stop before a driving part
    const int f = endFig_[size_t(bar)];
    const char* fig = kEnds[f];
    const int64_t t0 = int64_t(bar) * kBarTicks, six = kSixteenth;
    auto& sec = sectionAt(bar);
    const bool final = bar + 1 == sec.startBar + sec.bars && bar + 1 < totalBars();
    const auto& m = modeAt(bar);
    std::vector<int> at;
    for (int i = 0; i < (cuts(bar) ? leadSix_ : 16); ++i) if (fig[i] != '.') at.push_back(i); // none in the silence
    auto note = [&](int track, int64_t t, int64_t len, int p, float vel) {
        out.push_back({ t, Event::NoteOn, uint8_t(track), uint8_t(p), std::clamp(vel, .05f, 1.0f) });
        out.push_back({ t + len, Event::NoteOff, uint8_t(track), uint8_t(p), 0 });
    };
    const bool riffHere = spec_.riff != RiffStyle::None && (active(Riff, bar) || (final && active(Riff, bar + 1)) || buildsWith(Riff, bar)); // the guitar may enter with it
    auto on = [&](int track) { return active(track, bar) || buildsWith(track, bar) || (f == 9 && active(track, bar + 1)); }; // a build, and a stop, bring in the next part's
    for (size_t k = 0; k < at.size(); ++k)
    {
        const int i = at[k];
        const bool accent = fig[i] == 'X', lastHit = k + 1 == at.size();
        // the last two hits walk to the next part's first root from below: a third under, a step under
        const int walk = final && k + 2 >= at.size() && at.size() >= 3 ? (lastHit ? -1 : -2) : 0;
        const int b = walk ? bar + 1 : bar;
        const int64_t t = t0 + i * six;
        const int64_t len = (lastHit ? std::min(3, 16 - i) : at[k + 1] - i) * six - 12;
        const float vel = f >= 6 ? std::min(1.0f, .7f + .12f * float(f - 6) + .06f * float(i) / 15) : (.6f + .4f * float(i) / 15) * (accent ? 1.0f : .8f);
        const int r = lowRootAt(b), diff = degreeSemis(m, r + walk) - degreeSemis(m, r);
        if (riffHere) powerChord(Riff, t, len, riffRoot(b, walk), over(b, walk, 4), .5f + .5f * vel, out);
        if (on(Bass)) note(Bass, t, len, up(lowPitch(b, 0, 40, 51, 36) + diff, 40), vel);
        if (on(Sub) && accent) note(Sub, t, len, up(lowPitch(b, 0, 28, 39, 24) + diff, 24), vel);
        if (on(Chord) && accent) for (int p : voice(bar, 4, 48, 74, spec_.ext == 2, chordVoices_)) note(Chord, t, std::min<int64_t>(len, 2 * six), p, vel);
        if (on(Ground)) note(Ground, t, six, ladderPitch(b, 0, 28, 48), vel);
        if (on(Body)) note(Body, t, six / 2, ladderPitch(b, int(k % 2), 45, 69), vel * .9f);
        if (on(Pulse) && accent) note(Pulse, t, six / 2, foldInto(24 + tonicAt(bar), 24, 35), vel); // the mix pumps with it
        if (lastHit && (f < 6 || final) && on(Body)) out.push_back({ t, Event::Throw, Body, 0, 1.0f });
    }
    if (f == 4 && active(Body, bar) && !cuts(bar))
        for (int j = 0; j < 12; ++j) // the roll: sixteenths from the half, thirty-seconds in the last beat, swelling
        {
            const int64_t t = t0 + 8 * six + (j < 4 ? j * six : 4 * six + (j - 4) * six / 2);
            note(Body, t, six / 2 - 10, ladderPitch(bar, j % 3, 45, 69), .3f + .7f * float(j) / 11);
        }
}

int GenreComposer::heldTrack(int bar) const
{
    for (int tr : { Pad, Drone, Texture, Harm, Fx }) if (active(tr, bar)) return tr;
    return -1;
}

void GenreComposer::emitTurn(int bar, size_t from, std::vector<Event>& out)
{
    const int64_t t0 = int64_t(bar) * kBarTicks, cut = t0 + leadSix_ * kSixteenth, six = kSixteenth;
    const auto& m = modeAt(bar);
    auto note = [&](int track, int64_t t, int64_t len, int p, float vel) {
        out.push_back({ t, Event::NoteOn, uint8_t(track), uint8_t(p), vel });
        out.push_back({ t + len, Event::NoteOff, uint8_t(track), uint8_t(p), 0 });
    };
    // this bar's notes of a track from `at` on are dropped (each with its note-off), the ones ringing into it end there
    auto clip = [&](int track, int64_t at) {
        for (size_t i = from; i < out.size(); ++i)
        {
            auto& x = out[i];
            if (x.track != track) continue;
            if (x.type == Event::NoteOn && x.tick >= at)
            {
                for (size_t j = i + 1; j < out.size(); ++j)
                    if (out[j].track == track && out[j].pitch == x.pitch && out[j].type == Event::NoteOff) { out.erase(out.begin() + long(j)); break; }
                out.erase(out.begin() + long(i--));
            }
            else if (x.type == Event::NoteOff && x.tick > at) x.tick = at;
        }
    };
    // the silence from `at` to the downbeat: the kept tracks sound on, 7 dB down; with tails the echoes ring
    // soft (the hole): what is kept 14 dB down, so the bar is the drums'
    auto hush = [&](int64_t at, std::initializer_list<int> keep, bool tails, bool soft = false) {
        bool any = false;
        const int bits = (tails ? 2 : 0) | (soft ? 4 : 0);
        for (int k : keep) if (k >= 0) { out.push_back({ at, Event::Hush, uint8_t(k), uint8_t(1 | bits), 0 }); any = true; }
        if (!any) out.push_back({ at, Event::Hush, 0, uint8_t(bits), 0 });
        out.push_back({ t0 + kBarTicks, Event::Hush, 0, 0, 1 });
    };
    const int held = heldTrack(bar);
    switch (turn_[size_t(bar)])
    {
        case Turn::Stop: hush(t0 + 4 * six, {}, false); break; // the hit, then nothing, the tails too
        case Turn::Pickup:
        {   // the next part's bass walks into its root in eighths through the silence, up the scale (down where
            // the bottom of its range is too near)
            int walker = -1;
            if (active(Bass, bar + 1))
            {
                walker = Bass;
                clip(Bass, cut);
                const int r = lowRootAt(bar + 1), root = lowPitch(bar + 1, 0, 40, 51, 36), n = (16 - leadSix_) / 2;
                const int dir = root + degreeSemis(m, r - n) - degreeSemis(m, r) >= range(Bass)[0] ? -1 : 1;
                for (int j = 0; j < n; ++j)
                    note(Bass, cut + 2 * j * six, 2 * six - 20, root + degreeSemis(m, r + dir * (n - j)) - degreeSemis(m, r), .55f + .1f * float(j));
            }
            hush(cut, { held, walker, spec_.lead }, false);
            break;
        }
        case Turn::Echo:
        {   // the beat before the silence into the echoes; the held pad keeps only the notes the next chord shares
            for (int tr : { Arp, Chord, Body, Riff, Mallet, Drone, Lead }) out.push_back({ cut - kPPQ, Event::Throw, uint8_t(tr), 0, 1.0f });
            if (held == Pad && active(Pad, bar + 1) && rootAt(bar + 1) != rootAt(bar))
            {
                const auto next = chordTones(bar + 1, false);
                auto shared = [&](int p) { return std::find(next.begin(), next.end(), p % 12) != next.end(); };
                if (std::any_of(padHeld_.begin(), padHeld_.end(), shared))
                    for (size_t i = padHeld_.size(); i-- > 0;)
                        if (!shared(padHeld_[i])) // half a sixteenth late: after a suspension's resolution on beat three
                        {
                            out.push_back({ cut + six / 2, Event::NoteOff, Pad, uint8_t(padHeld_[i]), 0 });
                            padHeld_.erase(padHeld_.begin() + long(i));
                        }
            }
            hush(cut, { held }, true);
            break;
        }
        case Turn::Hole:
        {   // the bar is Kerem's: everything stops on its downbeat but a held part and, softly, the first notes
            // of the theme the next part brings
            int hint = -1;
            if (spec_.hook && spec_.lead != Arp && (sectionAt(bar).layers & bit(Arp)) && active(Arp, bar + 1))
            {
                hint = Arp;
                clip(Arp, t0);
                const auto& theme = spec_.summit == Summit::NewTheme && sectionAt(bar + 1).restoreTheme ? hookSummit_ : hookHome_;
                const int r = rootAt(bar + 1), lo = spec_.arpLo, hi = spec_.arpHi, rootP = pitchOf(bar + 1, 0, lo + 5, lo + 16, 12 * (lo / 12));
                int k = 0;
                for (auto& n : theme)
                {
                    if (n.step >= 16 || k++ == 3) break;
                    int p = rootP + degreeSemis(m, r + n.deg) - degreeSemis(m, r);
                    while (p > hi) p -= 12;
                    while (p < lo) p += 12;
                    note(Arp, t0 + n.step * six, std::min<int64_t>(n.len * six - 12, (16 - n.step) * six - 20), p, n.vel * .8f);
                }
            }
            hush(t0, { held, hint }, true, true);
            break;
        }
        default: break;
    }
}

void GenreComposer::generateBar(int bar, std::vector<Event>& out)
{
    if (bar < 0 || bar >= totalBars()) return;
    const size_t from = out.size();
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
    if (spec_.motifN > 0 && sectionOfBar_[size_t(bar)] == 0) emitMotif(bar, out);
    else if (spec_.lead == Arp) emitLead(bar, out);
    else if (spec_.hook) emitHook(bar, out);
    else emitArp(bar, out);
    emitPart(Sub, spec_.sub, bar, out);
    emitHarm(bar, out);
    if (spec_.lead == Drone) emitLead(bar, out);
    else emitDrone(bar, out);
    emitPart(Ground, spec_.ground, bar, out);
    if (!spec_.breaks) emitPart(Body, spec_.body, bar, out);
    emitPart(Mallet, spec_.mallet, bar, out);
    emitPart(Texture, spec_.texture, bar, out);
    emitImpact(bar, out);
    emitRiff(bar, out);
    if (spec_.lead == Lead) emitLead(bar, out);
    emitBoom(bar, out);
    emitAttack(bar, out);
    emitTurn(bar, from, out);
    emitHeld(Fx, bar, out, fxHeld_, active(Fx, bar) ? foldInto(48 + tonicAt(bar), 48, 59) : -1);
    if (spec_.breaks) // the break owns its track: the end figures' and builds' hand-drum hits leave it
    {
        out.erase(std::remove_if(out.begin() + std::ptrdiff_t(from), out.end(), [](const Event& e) {
            return e.track == Body && (e.type == Event::NoteOn || e.type == Event::NoteOff); }), out.end());
        emitBreaks(bar, out);
    }
    if (flipBar(bar)) // the harpsichord and the choir alone for a bar: everything else cut, the rooms ringing on
    {
        const int64_t t0 = int64_t(bar) * kBarTicks;
        for (int k : { int(Arp), int(Harm) }) out.push_back({ t0, Event::Hush, uint8_t(k), uint8_t(1 | 2), 0 });
        out.push_back({ t0 + kBarTicks, Event::Hush, 0, 0, 1 });
    }
}

int GenreComposer::tradeTurn(int bar) const
{
    if (!spec_.trade || bar < 0 || bar >= totalBars()) return 0;
    auto& sec = sectionAt(bar);
    const int in = bar - sec.startBar, left = sec.startBar + sec.bars - bar;
    if (sec.energy < .6f || !active(Arp, bar) || attackBar(bar) || in < 2 || left <= 2) return 0;
    return (in / 2) % 2 == 0 ? 1 : 2;
}

bool GenreComposer::flipBar(int bar) const
{
    if (!spec_.flip || bar < 0 || bar >= totalBars()) return false;
    auto& sec = sectionAt(bar);
    const int in = bar - sec.startBar, left = sec.startBar + sec.bars - bar;
    return sec.energy >= .8f && in % 16 == 11 && left > 2 && tradeTurn(bar) == 2;
}

// The break's beat notes (BreakInstrument, Engine.cpp): every beat of the song, pitch 36 + its place in the eight-bar
// phrase (68 + it on a part's first beat: a new pattern), velocity how wild, the lowest silent. It plays where the body
// would, louder with the energy, and stops on the parts' end figures, like the other drums.
void GenreComposer::emitBreaks(int bar, std::vector<Event>& out)
{
    const int si = sectionOfBar_[size_t(bar)];
    const bool first = bar == 0 || sectionOfBar_[size_t(bar - 1)] != si;
    // the part's own layers, not active(): the subtraction before a big arrival never takes the break
    // it plays through the builds and falls silent only in a part's last bar (the band's figure, the stop)
    auto lastOfPart = [&](int b) { return b + 1 >= totalBars() || sectionOfBar_[size_t(b + 1)] != sectionOfBar_[size_t(b)]; };
    const bool loud = (sectionAt(bar).layers & bit(Body)) && bar >= sections_[0].bars && !lastOfPart(bar) && !flipBar(bar);
    const float wild = std::clamp(.15f + .75f * energyAt(bar), .1f, .95f);
    // the bar before a driving part's last bar: a tape stop, the break slowing to a quarter over the bar
    const bool dive = loud && bar + 2 < totalBars() && lastOfPart(bar + 1) && drives(sectionOfBar_[size_t(bar + 2)]);
    for (int b = 0; b < 4; ++b)
    {
        const int64_t t = int64_t(bar) * kBarTicks + b * kPPQ;
        const int pitch = dive ? 100 + b : (first && b == 0 ? 68 : 36) + (bar % 8) * 4 + b;
        out.push_back({ t, Event::NoteOn, uint8_t(Body), uint8_t(pitch), loud ? wild : .005f });
        out.push_back({ t + kPPQ / 2, Event::NoteOff, uint8_t(Body), uint8_t(pitch), 0 });
    }
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
