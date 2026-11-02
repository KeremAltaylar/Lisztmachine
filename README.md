# Lisztmachine

A generative piano/synth player for a one-hour live concert with an acoustic drummer,
with audio-reactive visuals to follow. This is milestone 1: one Berlin-school scene,
about ten minutes, played by four Surge XT instances.

## Layout

| | |
| --- | --- |
| `src/core` | The composer. Plain C++, no JUCE: form, harmony, voice leading, sequences, melody. Deterministic per seed on every platform. |
| `src/engine` | Hosts Surge XT, plays the composer's events sample-accurately, mixes, limits, renders the click. |
| `src/app` | The rehearsal panel. Outputs 1/2 = PA mix, 3/4 = click. Space = play/stop. |
| `src/render` | `liszt_render`: a concert offline to WAV + MIDI, ~9x real time. |
| `tests` | `liszt_tests`: composer checks over 40 seeds. |
| `tools/analyze.py` | Measures a render: loudness per section, silent gaps, onset timing. |

## Build (Windows)

    cmake -S . -B %USERPROFILE%\build\Lisztmachine -G "Visual Studio 17 2022" -A x64
    cmake --build %USERPROFILE%\build\Lisztmachine --config Release

Needs Surge XT installed. The build directory lives outside OneDrive on purpose.

## Render and listen

    liszt_render --seed 7 --out seed7.wav
    liszt_render --seed 7 --seconds 120 --solo seq --patch "seq=Plucks/Saw Pluck" --out try.wav
    liszt_render --audition "Pads/Juno-60 Strings"

## Sounds

Each part rotates through a palette of Surge XT patches over the concert: calm sections
take the darker ones, the peak the brightest, and the parts change at different times with
a one-bar crossfade. The palette is a text file, `sounds/berlin.txt`: one patch per line
with its level (dB) and measured brightness. Edit it to add, drop or rebalance sounds.

`--patch track=Category/Name` makes one part play a single patch instead.
