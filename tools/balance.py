# Balance each genre song at its summit: render 16 bars 32 bars into it (where the energy has
# arrived), read each track's RMS in the mix, move the palette's `gain <track>` toward the target,
# and lower every target together while the master limiter takes more than 1 dB. Run a few times.
import json, os, re, subprocess
from concurrent.futures import ThreadPoolExecutor
R = r"C:\Users\kerem\build\Lisztmachine\liszt_render_artefacts\Release\liszt_render.exe"
S = r"C:\Users\kerem\OneDrive\Desktop\Claude\Projects\Lisztmachine\sounds"
G = open(r"C:\Users\kerem\OneDrive\Desktop\Claude\Projects\Lisztmachine\src\core\Genres.cpp", encoding="utf8").read()
PEAK = dict(jungle="Ascension", footwork="Black", ecstatic="Computation", dubtechno="Submersion", glitch="Supercodex",
            gqom="Possessed", deconstructed="Rebirth", halftime="Gravity")
BASE = dict(pad=-27, kick=-21, snare=-27, hats=-30, perc=-30, bass=-25, sub=-27, arp=-26, chord=-28)  # no fx: a riser, quiet at the summit and loud before it, so one place cannot judge it
OVER = dict(glitch=dict(perc=-26, arp=-27, hats=-29), ecstatic=dict(arp=-23, chord=-26), dubtechno=dict(chord=-25, snare=-29),
            footwork=dict(perc=-27, chord=-27), gqom=dict(perc=-26), deconstructed=dict(chord=-25, perc=-28))
OFF = os.path.join(os.path.dirname(__file__), "balance-offsets.json")   # per song: how far everything went down for the limiter
off = json.load(open(OFF)) if os.path.exists(OFF) else {}

def peakBar(scene):
    block = G[G.index(f'.scene = "{scene}"'):]
    block = block[block.index(".form"):block.index(".kick")]
    bar = 0
    for name, bars in re.findall(r'\{ "([^"]+)", (\d+),', block):
        if name == PEAK[scene]: return bar + 32
        bar += int(bars)

def run(scene):
    out = subprocess.run([R, "--scene", scene, "--seed", "1", "--from", str(peakBar(scene)), "--bars", "16",
                          "--out", rf"C:\Users\kerem\Music\Lisztmachine\genres\{scene}_peak.wav"], capture_output=True, text=True).stdout
    rms = {m[0]: float(m[1]) for m in re.findall(r"^\s+(\w+): peak [-\d.]+ dBFS, rms ([-\d.]+) dBFS", out, re.M)}
    lim = float(re.search(r"limiter deepest ([-\d.]+)", out).group(1))
    off[scene] = off.get(scene, 0) + max(0.0, -lim - 1.0)
    p = rf"{S}\{scene}.txt"; txt = open(p, encoding="utf8").read()
    moves = []
    for t, target in {**BASE, **OVER.get(scene, {})}.items():
        if rms.get(t, -150) < -100: continue
        d = max(-10, min(10, target - off[scene] - rms[t]))
        txt = re.sub(rf"^gain {t}\s+([-\d.]+)$", lambda m: f"gain {t:<6}{float(m.group(1)) + d:>5.1f}", txt, flags=re.M)
        moves.append(f"{t} {rms[t]:.1f}{d:+.1f}")
    open(p, "w", encoding="utf8", newline="").write(txt)
    return f"{scene}: limiter {lim:.1f} off {off[scene]:.1f} | " + ", ".join(moves)

with ThreadPoolExecutor(4) as ex:
    for line in ex.map(run, PEAK): print(line, flush=True)
json.dump(off, open(OFF, "w"))
