"""Measure a liszt_render output instead of trusting it.

    python tools/analyze.py seed1.wav            loudness per section, silent gaps
    python tools/analyze.py bass.wav --timing    onset timing against the sixteenth grid (solo renders)
"""
import sys
import numpy as np
from scipy.io import wavfile


def db(x):
    return 20 * np.log10(np.maximum(x, 1e-9))


def load(path):
    sr, raw = wavfile.read(path)
    x = raw.astype(np.float64)
    if raw.dtype == np.int32:  # scipy left-justifies 24-bit samples into int32
        x /= 2 ** 31
    elif raw.dtype == np.int16:
        x /= 2 ** 15
    mono = x.mean(axis=1) if x.ndim > 1 else x
    side = path.rsplit('.', 1)[0] + '.sections.txt'
    lines = open(side).read().split('\n')
    bpm = float(lines[0].split()[1])
    sections = [(float(a), float(b), c) for a, b, c in (l.split(' ', 2) for l in lines[1:] if l.strip())]
    return sr, x, mono, bpm, sections


def windows_rms(x, n):
    k = len(x) // n
    return np.sqrt((x[: k * n].reshape(k, n) ** 2).mean(axis=1))


def overview(path):
    sr, x, mono, bpm, sections = load(path)
    print(f'{path}: {len(mono) / sr:.1f} s, peak {db(np.abs(x).max()):.1f} dBFS')
    levels, energies = [], []
    for (t0, e, name), (t1, _, _) in zip(sections, sections[1:]):
        seg = mono[int(t0 * sr): int(t1 * sr)]
        r = db(np.sqrt((seg ** 2).mean()))
        levels.append(r)
        energies.append(e)
        print(f'  {name:10s} energy {e:.2f}  rms {r:6.1f} dBFS')
    print(f'  loudness follows energy: correlation {np.corrcoef(levels, energies)[0, 1]:.2f}')

    # Silence inside the piece: 100 ms windows below -70 dBFS.
    end = sections[-1][0]
    w = windows_rms(mono[: int(end * sr)], int(0.1 * sr))
    quiet = np.where(db(w[5:]) < -70)[0] + 5
    print(f'  silent 100 ms windows inside the piece: {len(quiet)}' + (f' (first at {quiet[0] * 0.1:.1f} s)' if len(quiet) else ''))

    # No click detector here on purpose: saw and square oscillators have a true discontinuity
    # every cycle, so a sample-difference detector flagged thousands of "clicks" that were the
    # waveform itself. Clicks are checked by ear until there is a detector that survives that.


def timing(path):
    sr, x, mono, bpm, sections = load(path)
    hop = int(0.001 * sr)
    env = windows_rms(mono, hop)  # 1 ms frames
    e = db(env)
    onsets = []
    for i in range(10, len(e)):
        if e[i] > -50 and e[i] - e[i - 10: i].min() > 12 and (not onsets or i - onsets[-1] > 40):
            onsets.append(i)
    t = np.array(onsets) * hop / sr
    grid = 60 / bpm / 4
    dev = (t - np.round(t / grid) * grid) * 1000
    print(f'{path}: {len(t)} onsets at {bpm} bpm; offset from the sixteenth grid: '
          f'mean {dev.mean():.2f} ms, SD {dev.std():.3f} ms, worst {np.abs(dev - dev.mean()).max():.2f} ms')


if __name__ == '__main__':
    (timing if '--timing' in sys.argv else overview)(sys.argv[1])
