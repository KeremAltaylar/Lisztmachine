# Levels a song's percussion patches against each other in context. One-shots cannot be levelled from
# a held note (a ringing gong and a dry cowbell measure the same held and nothing alike in a
# pattern), so each Surge patch on the metal and perc tracks plays the summit's 16 bars alone, and
# its palette gain is set so they all sit at the same RMS. Then run balance.py for the track gain.
# Usage: python tools/perc_level.py [songs...]
import os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__))
R = r"C:\Users\kerem\build\Lisztmachine\liszt_render_artefacts\Release\liszt_render.exe"
S = os.path.join(HERE, "..", "sounds")
G = open(os.path.join(HERE, "..", "src", "core", "Genres.cpp"), encoding="utf8").read()
REF = -30.0  # dBFS RMS every patch is brought to before the track gain
songs = sys.argv[1:] or ["jungle", "triphop", "ecstatic", "dubtechno", "noir", "gqom", "deconstructed", "halftime"]

def summit(scene):
    block = G[G.index(f'.scene = "{scene}"'):]
    block = block[block.index(".form"):block.index(".pulse")]
    secs = [(int(b), float(e)) for b, e in re.findall(r'\{ "[^"]+", (\d+), ([\d.]+)f,', block)]
    peak, bar = max(e for _, e in secs), 0
    for b, e in secs:
        if e == peak: return bar + b // 2  # past the half: every part has entered
        bar += b

def measure(job):
    scene, track, patch = job
    out = os.path.join(os.environ.get("TEMP", "."), f"perc-{scene}-{track}-{abs(hash(patch))}.wav")
    txt = subprocess.run([R, "--scene", scene, "--seed", "1", "--from", str(summit(scene)), "--bars", "16", "--solo", track,
                          "--patch", f"{track}={patch}", "--out", out], capture_output=True, text=True).stdout
    for ext in (".wav", ".mid", ".sections.txt", ".show.json"):
        if os.path.exists(out[:-4] + ext): os.remove(out[:-4] + ext)
    m = re.search(rf"^\s+{track}: peak [-\d.]+ dBFS, rms ([-\d.]+)", txt, re.M)
    return scene, track, patch, float(m.group(1)) if m else -150.0

jobs = []
for scene in songs:
    for l in open(os.path.join(S, scene + ".txt"), encoding="utf8"):
        w = l.split()
        if len(w) >= 4 and w[0] in ("metal", "perc") and (w[3].startswith("3rd/") or w[3] in ("internal/deep", "internal/metal")): jobs.append((scene, w[0], l.split(None, 3)[3].strip()))
with ThreadPoolExecutor(4) as ex: rows = list(ex.map(measure, jobs))
for scene in songs:
    p = os.path.join(S, scene + ".txt"); txt = open(p, encoding="utf8").read()
    for sc, track, patch, rms in rows:
        if sc != scene or rms < -100: continue
        g = max(-18.0, min(18.0, REF - rms))
        txt = re.sub(rf"^{track}(\s+)\S+(\s+\S+\s+){re.escape(patch)}$", lambda m: f"{track}{m.group(1)}{g:.1f}{m.group(2)}{patch}", txt, flags=re.M)
        print(f"{scene:<14}{track:<6}{patch:<45} {rms:6.1f} dB -> gain {g:+.1f}")
    open(p, "w", encoding="utf8", newline="").write(txt)
