# Levels the sampled and Dexed instruments in context (and pulls down any sound far over its track). A held note misjudges them (a plucked bass
# decays, a sax waits for its breath), so eight bars of every sixteen in each section are rendered, each track's
# RMS read, and the sound playing there (from the render's sound plan) compared with the same track
# at the summit, and with the track's target level (balance.py's): an instrument more than 4 dB off
# the summit, or more than 4 dB over the target or 8 under it in any section from energy .35 up, has
# its palette gain moved that far. (VSCO's dynamic layers differ by 20 dB: only context tells.)
# Usage: python tools/patch_level.py [songs...]   (then balance.py)
import os, re, subprocess, sys, collections
from concurrent.futures import ThreadPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__))
R = r"C:\Users\kerem\build\Lisztmachine\liszt_render_artefacts\Release\liszt_render.exe"
S = os.path.join(HERE, "..", "sounds")
G = open(os.path.join(HERE, "..", "src", "core", "Genres.cpp"), encoding="utf8").read()
TRACKS = ("pad", "chord", "arp", "bass", "harm", "metal", "perc")
TARGET = dict(pad=-27, metal=-28, perc=-29, bass=-25, arp=-26, chord=-28, harm=-29)
songs = sys.argv[1:] or ["stoner", "jungle", "ecstatic", "noir", "deconstructed", "halftime", "doom", "blackgaze"]
TMP = os.environ.get("TEMP", ".")

def sections(scene):
    b = G[G.index(f'.scene = "{scene}"'):]; b = b[b.index(".form"):b.index(".pulse")]
    out, bar = [], 0
    for n, bars, e in re.findall(r'\{ "([^"]+)", (\d+), ([\d.]+)f,', b):
        out.append((n, bar, int(bars), float(e))); bar += int(bars)
    return out

def render(args):
    scene, at, bars = args
    t = subprocess.run([R, "--scene", scene, "--seed", "1", "--from", str(at), "--bars", str(bars),
                        "--out", os.path.join(TMP, f"pl-{scene}-{at}.wav")], capture_output=True, text=True).stdout
    for ext in (".wav", ".mid", ".sections.txt", ".show.json"):
        f = os.path.join(TMP, f"pl-{scene}-{at}{ext}")
        if os.path.exists(f): os.remove(f)
    return t

for scene in songs:
    secs = sections(scene)
    plan = render((scene, 0, 1))  # the sound plan, printed whole
    bpm = float(re.search(r"([\d.]+) bpm", plan).group(1))
    changes = collections.defaultdict(list)  # track -> [(seconds, patch)]
    for m, s_, t, p in re.findall(r"^\s+(\d+):(\d+)\s+(\w+) -> (.+)$", plan, re.M): changes[t].append((int(m) * 60 + int(s_), p.strip()))
    def playing(track, bar):
        sec, now = bar * 4 * 60 / bpm, None
        for at, p in changes[track]:
            if at <= sec + 1: now = p
        return now
    peak = max(e for *_, e in secs)
    # every 16 bars of every section from energy .35 up, so each sound in the plan is heard where it plays
    jobs = [(scene, b + k + 2, min(8, n - k - 2)) for _, b, n, e in secs if e >= .35 for k in range(0, n - 4, 16)]
    with ThreadPoolExecutor(2) as ex: outs = list(ex.map(render, jobs))
    rows = []
    for (sc, at, bars), t in zip(jobs, outs):
        rms = {m[0]: float(m[1]) for m in re.findall(r"^\s+(\w+): peak [-\d.]+ dBFS, rms ([-\d.]+)", t, re.M)}
        rows.append((at, rms))
    summit = next(b + n // 2 for _, b, n, e in secs if e == peak)
    ref = min(rows, key=lambda r: abs(r[0] - summit))[1]
    moves = collections.defaultdict(list)
    for at, rms in rows:
        for tr in TRACKS:
            if rms.get(tr, -150) < -90: continue
            p = playing(tr, at)
            if not p or p.startswith("internal/"): continue
            over = rms[tr] - TARGET[tr]
            if over > 4: moves[(tr, p)].append(-over)           # any sound far over its track's level
            elif not (p.startswith("sfz/") or p.startswith("dexed/")): continue
            elif over < -8: moves[(tr, p)].append(-over)
            elif ref.get(tr, -150) > -90: moves[(tr, p)].append(ref[tr] - rms[tr])
    path = os.path.join(S, scene + ".txt"); txt = open(path, encoding="utf8").read()
    for (tr, p), ds in moves.items():
        d = sum(ds) / len(ds)
        if abs(d) < 4: continue
        d = max(-15, min(15, d))
        txt = re.sub(rf"^{tr}(\s+)([-\d.]+)(\s+\d+\s+){re.escape(p)}$", lambda m: f"{tr}{m.group(1)}{float(m.group(2)) + d:.1f}{m.group(3)}{p}", txt, flags=re.M)
        print(f"{scene:<14}{tr:<6}{p:<70} {d:+.1f} dB")
    open(path, "w", encoding="utf8", newline="").write(txt)
print("done")
