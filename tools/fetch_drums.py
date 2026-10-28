# Fetches the sampled drum kit: Karoryfer's Gogodze Phu Vol II (CC0), only the close mics the engine
# uses (kick mic: kick, snare mic: snare, overhead: hi-hat, front: toms). 219 WAVs, 66 MB.
# Usage: python tools/fetch_drums.py [destination]   (default ~/LisztmachineSounds/Gogodze)
import json, os, re, sys, urllib.request
from concurrent.futures import ThreadPoolExecutor

REPO, REV = "sfzinstruments/karoryfer.gogodze-phu-vol-ii", "HEAD"
WANT = re.compile(r"^Samples/(kick_mic/ks_|snare_mic/(sc|se|ss)_|oh_mic/h[cfhlo]_|front_mic/t[hml]_).*\.wav$")
dest = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.expanduser("~"), "LisztmachineSounds", "Gogodze")

tree = json.load(urllib.request.urlopen(f"https://api.github.com/repos/{REPO}/git/trees/{REV}?recursive=1"))["tree"]
files = [(t["path"], t["size"]) for t in tree if t["type"] == "blob" and (WANT.match(t["path"]) or t["path"] in ("LICENSE", "readme.txt"))]

def get(item):
    path, size = item
    out = os.path.join(dest, *path.split("/"))
    if os.path.exists(out) and os.path.getsize(out) == size: return 0
    os.makedirs(os.path.dirname(out), exist_ok=True)
    data = urllib.request.urlopen(f"https://raw.githubusercontent.com/{REPO}/{REV}/{path}").read()
    assert len(data) == size, f"{path}: {len(data)} bytes, expected {size}"
    open(out, "wb").write(data)
    return 1

with ThreadPoolExecutor(8) as ex: got = sum(ex.map(get, files))
print(f"{len(files)} files in {dest} ({got} downloaded, {sum(s for _, s in files) / 1e6:.1f} MB)")
