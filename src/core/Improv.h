#pragma once
#include "Genres.h"
#include <functional>
#include <string>

namespace liszt {

// Improvisation (Kerem, 2026-11-14: "a generator ... it will randomize and use everything we have and establish a
// song, near its end it will generate another song and transition to it component by component ... the next song
// should be different than the previous one in every manner, make a measurement for it ... max song duration 5
// [minutes] ... fuse songs to each other in a good harmonic mixing way").
//
// A chain of songs, each one planned against the one before it. A song is the genre composer driven by a table
// crossbred from all eight genre songs: its form from one, its percussion from another, its bass from a third
// (or a walking contrabass), its progressions, its melody (hook or arpeggio, and the soloist), its riff, and each
// track's instrument borrowed from any song that has a fitting one. Tempo, key, mode and the summit's way are its
// own. Of many candidates the one furthest from the song before (and from the one before that) is taken, among
// those that keep every hard rule: every donor different, the tempo at least 15 % away, the mode different.
//
// The fusion: every song ends in a Release (pad and drone on the tonic, the rhythm gone part by part) and
// begins with a Seed (its drone alone). The page lays the next song's Seed under the last one's Release, so the
// changeover is a pivot: the new song's drone may only sound notes of the old song's final chord.
struct ImprovSong
{
    GenreComposer::Spec spec;                              // the table the composer plays
    int k = 0;                                             // its place in the chain
    int tonic = 0, bpm = 120, seconds = 0, bars = 0;
    const Mode* mode = nullptr;
    int form = 0, rhythm = 0, bass = 0, harmony = 0, melody = 0, riff = 0; // donors: indices into GenreComposer::all()
    bool walk = false;
    std::array<int, GenreComposer::kGenreTracks> sound {}; // whose instrument each track plays (an index into all())
    int space = 0;                                         // whose reverb, echoes and master
    std::string name, move;                                // its title; the harmonic move into it
    double distance = 1;                                   // 0..1 from the song before: the share of dimensions that differ
    std::vector<std::pair<std::string, double>> dims;      // each dimension's difference from the song before
};

// "improv/<k>": song k of the chain (the chain is the seed); false for any other scene
bool improvScene(const std::string& scene, int& k);
// The chain's song k (planned from song 0 on, cached; the reference stays valid)
const ImprovSong& improvSong(uint64_t seed, int k);
// The song's palette: each track's lines from its donor's palette (read by name), levels moved to one master
std::string improvPalette(const ImprovSong& s, const std::function<std::string(const std::string&)>& readPalette);
// The plan as JSON, for the page and for checking the chain
std::string improvJson(const ImprovSong& s);

} // namespace liszt
