"""Measure a liszt_render output instead of trusting it.

    python tools/analyze.py seed1.wav            loudness per section, silent gaps
    python tools/analyze.py sq.wav --timing      onset timing vs the .mid
        (render it with: liszt_render --solo bass --patch "bass=Templates/Init Square" --out sq.wav)
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


def timing(path, track='bass'):
    """Onset timing against the true note times in the render's .mid. Render a solo with a
    patch that starts instantly (Templates/Init Square): only notes that start from digital
    silence are measured, and the onset is the first non-zero sample, so the attack shape and
    velocity cannot move it. (A level-threshold onset reported +-35 ms of "jitter" that was
    only the envelopes.)"""
    import mido
    sr, x, mono, bpm, sections = load(path)
    spq = 60 / bpm
    ons, name = [], None
    for tr in mido.MidiFile(path.rsplit('.', 1)[0] + '.mid').tracks:
        t = 0
        for m in tr:
            t += m.time
            if m.type == 'track_name':
                name = m.name
            if name == track and m.type == 'note_on' and m.velocity > 0:
                ons.append(t / 960 * spq)
    delays = []
    for o in ons:
        i0 = int(round(o * sr))
        if i0 + int(0.02 * sr) >= len(mono) or np.abs(mono[i0 - int(0.02 * sr): i0 - 48]).max() > 0:
            continue
        seg = np.abs(mono[i0 - 48: i0 + int(0.02 * sr)])
        delays.append(i0 - 48 + int(np.argmax(seg > 0)) - i0)
    d = np.array(delays)
    print(f'{path}: {len(d)} {track} onsets from silence; first sound minus true note time: '
          f'mean {d.mean() / sr * 1000:.2f} ms, SD {d.std() / sr * 1000:.3f} ms, range {d.min()}..{d.max()} samples')


if __name__ == '__main__':
    (timing if '--timing' in sys.argv else overview)(sys.argv[1])
