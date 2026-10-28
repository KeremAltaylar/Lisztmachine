# The curation loop: renders the same sixteen bars (around the summit's lift) for several seeds of
# each genre song, as mp3 for the audition page, plus a JSON of what each seed is.
# Usage: python tools/audition.py <out dir> [seeds=1,2,3,4] [songs...]
import json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
import soundfile as sf
HERE = os.path.dirname(os.path.abspath(__file__))
R = r"C:\Users\kerem\build\Lisztmachine\liszt_render_artefacts\Release\liszt_render.exe"
G = open(os.path.join(HERE, "..", "src", "core", "Genres.cpp"), encoding="utf8").read()
out = sys.argv[1]
seeds = [int(s) for s in (sys.argv[2] if len(sys.argv) > 2 else "1,2,3,4").split(",")]
songs = sys.argv[3:] or ["jungle", "footwork", "ecstatic", "dubtechno", "glitch", "gqom", "deconstructed", "halftime"]
os.makedirs(out, exist_ok=True)

def window(scene):
    # the summit is the first section at the highest energy; the lift is halfway into it
    block = G[G.index(f'.scene = "{scene}"'):]
    block = block[block.index(".form"):block.index(".pulse")]
    secs = [(n, int(b), float(e)) for n, b, e in re.findall(r'\{ "([^"]+)", (\d+), ([\d.]+)f,', block)]
    peak = max(e for _, _, e in secs)
    bar = 0
    for n, b, e in secs:
        if e == peak: return bar + b // 2 - 8, n
        bar += b

def run(job):
    scene, seed = job
    start, name = window(scene)
    wav = os.path.join(out, f"{scene}-{seed}.wav")
    txt = subprocess.run([R, "--scene", scene, "--seed", str(seed), "--from", str(start), "--bars", "16", "--out", wav], capture_output=True, text=True).stdout
    x, sr = sf.read(wav)
    sf.write(wav[:-4] + ".mp3", x, sr, format="MP3", compression_level=0.6)
    for ext in (".wav", ".mid", ".sections.txt", ".show.json"):
        if os.path.exists(wav[:-4] + ext): os.remove(wav[:-4] + ext)
    m = re.search(rf"seed {seed}: (.+)", txt)
    return scene, seed, m.group(1).strip() if m else "", name

with ThreadPoolExecutor(4) as ex:
    rows = list(ex.map(run, [(s, n) for s in songs for n in seeds]))
info = {}
for scene, seed, desc, name in rows: info.setdefault(scene, {})[str(seed)] = {"describe": desc, "section": name}
json.dump(info, open(os.path.join(out, "auditions.json"), "w"), indent=1)
for r in rows: print(*r)
