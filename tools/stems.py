# Cuts a --stems render into one mp3 per section and stem for the mixer page, and checks that the
# stems through the glue rebuild the mix. Usage: python tools/stems.py <render.wav> <page dir> <master dB>
import sys, json, os, numpy as np, soundfile as sf

wav, page, master = sys.argv[1], sys.argv[2], float(sys.argv[3])
CHECK = "--check" in sys.argv  # only the rebuild check, no mp3s
base = wav[:-4]
names = [l.split()[2] for l in open(base + ".sections.txt").read().splitlines()[1:]]
times = [float(l.split()[0]) for l in open(base + ".sections.txt").read().splitlines()[1:]]
stems = [s for s in "pad pulse metal tick perc bass sub arp chord fx harm ground body mallet texture impact riff delay reverb".split()
         if os.path.exists(f"{base}.{s}.wav")]
os.makedirs(f"{page}/media", exist_ok=True)

def glue(x, sr):  # the engine's GlueBus, sample by sample
    a, r = 1 - np.exp(-1 / (0.010 * sr)), 1 - np.exp(-1 / (0.150 * sr))
    peak = np.abs(x).max(1); env = np.empty_like(peak); e = 0.0
    for i, p in enumerate(peak): e += (p - e) * (a if p > e else r); env[i] = e
    over = 20 * np.log10(np.maximum(env, 1e-6)) + 18
    cut = np.where(over <= -3, 0, np.where(over >= 3, over / 2, 0.5 * (over + 3) ** 2 / 12))
    g = 10 ** (-cut / 20) * 10 ** (master / 20)
    return 0.89 * np.tanh(x * g[:, None] / 0.89)

data = {s: sf.read(f"{base}.{s}.wav", dtype="float32")[0] for s in stems}
sr = 48000
LOOK = 72  # PeakLimiter look_
mix = sf.read(wav, dtype="float32")[0]
man = {"sections": []}
for k, name in enumerate(names[:-1]):
    a, b = int(times[k] * sr), int(times[k + 1] * sr)
    present = []
    for s in stems:
        x = data[s][a:b]
        if np.sqrt((x ** 2).mean()) < 10 ** (-70 / 20): continue
        if not CHECK: sf.write(f"{page}/media/{k}_{s}.mp3", x, sr, format="MP3", compression_level=0.3)
        present.append(s)
    man["sections"].append({"name": name, "start": times[k], "end": times[k + 1], "stems": present})
    rebuilt = glue(sum(data[s][a:b] for s in stems), sr)[:-LOOK]
    ref = mix[a + LOOK:b]  # the mix passed the limiter, LOOK samples later
    err = 20 * np.log10(np.sqrt(((rebuilt - ref) ** 2).mean()) / np.sqrt((ref ** 2).mean()))
    print(f"{name:<10} {len(present):2} stems, rebuilt vs mix {err:6.1f} dB")
json.dump(man, open(f"{page}/manifest.json", "w"), indent=1)
