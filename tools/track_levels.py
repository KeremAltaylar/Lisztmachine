"""How loud each part sits in each genre song's mix, at its summit: the level Improvisation balances a borrowed part to.

    python tools/track_levels.py     # renders 16 summit bars of each genre song, prints the table for src/core/Improv.cpp

Level = the part's RMS minus the whole mix's RMS (dB), from liszt_render's per-track meters. Parts silent at the
summit print as None.
"""
import os, re, subprocess, sys, json
import numpy as np, soundfile as sf

RENDER = r'C:\Users\kerem\build\Lisztmachine\liszt_render_artefacts\Release\liszt_render.exe'
OUT = os.path.join(os.path.expanduser('~'), 'Music', 'Lisztmachine', 'levels')
os.makedirs(OUT, exist_ok=True)
SUMMITS = {'stoner': 'Strike', 'jungle': 'Liquid Gold', 'ecstatic': 'Computation', 'noir': 'Cathedral of Smoke',
           'deconstructed': 'Rebirth', 'halftime': 'Mass', 'doom': 'Monument', 'blackgaze': 'Sunbather'}
TRACKS = ['pad', 'pulse', 'metal', 'tick', 'perc', 'bass', 'sub', 'arp', 'chord', 'fx', 'harm', 'ground', 'body',
          'mallet', 'texture', 'impact', 'riff', 'drone', 'lead', 'boom']
table = {}
for scene, summit in SUMMITS.items():
    wav = os.path.join(OUT, scene + '.wav')
    log = wav.replace('.wav', '.log')
    if not os.path.exists(log):
        r = subprocess.run([RENDER, '--scene', scene, '--seed', '1', '--from', summit, '--bars', '16', '--out', wav],
                           capture_output=True, text=True, cwd=OUT)
        open(log, 'w').write(r.stdout + r.stderr)
    text = open(log).read()
    x, sr = sf.read(wav, dtype='float32')
    mix = float(20 * np.log10(np.sqrt(np.mean(x[: int(len(x) * 0.9)] ** 2)) + 1e-12))   # the bars, not the tail
    row = {}
    for t in TRACKS:
        m = re.search(r'^\s+%s: peak [-\d.]+ dBFS, rms ([-\d.]+) dBFS' % t, text, re.M)
        row[t] = None if not m or float(m.group(1)) < -100 else round(float(m.group(1)) - mix, 1)
    table[scene] = row
    print(scene, 'mix %.1f dB' % mix, json.dumps(row), flush=True)
print('MEDIAN', json.dumps({t: (round(float(np.median([table[s][t] for s in table if table[s][t] is not None])), 1)
                                if any(table[s][t] is not None for s in table) else None) for t in TRACKS}))
