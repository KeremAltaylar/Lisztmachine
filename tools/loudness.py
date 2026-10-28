# Measures a render the way a mastering engineer reads it: integrated loudness (BS.1770, LUFS),
# sample peak, peak-to-loudness ratio, and the stereo picture (side against mid, overall and below
# 150 Hz, where it should be near zero). Usage: python tools/loudness.py a.wav [b.wav ...]
import sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter, butter, sosfilt

def kweight(x, sr):
    assert sr == 48000, "K-weighting coefficients here are for 48 kHz"
    x = lfilter([1.53512485958697, -2.69169618940638, 1.19839281085285], [1, -1.69065929318241, 0.73248077421585], x, axis=0)
    return lfilter([1, -2, 1], [1, -1.99004745483398, 0.99007225036621], x, axis=0)

def lufs(x, sr):
    y = kweight(x, sr)
    block, hop = int(0.4 * sr), int(0.1 * sr)
    z = np.array([np.mean(y[i:i + block] ** 2, axis=0).sum() for i in range(0, len(y) - block, hop)])
    l = -0.691 + 10 * np.log10(z + 1e-12)
    z = z[l > -70]
    rel = -0.691 + 10 * np.log10(z.mean()) - 10
    z = z[-0.691 + 10 * np.log10(z) > rel]
    return -0.691 + 10 * np.log10(z.mean())

def measure(path):
    sr, x = wavfile.read(path)
    x = x / float(np.iinfo(x.dtype).max + 1) if x.dtype.kind == 'i' else x.astype(np.float64)
    mid, side = (x[:, 0] + x[:, 1]) / 2, (x[:, 0] - x[:, 1]) / 2
    low = butter(4, 150, 'low', fs=sr, output='sos')
    db = lambda a, b: 10 * np.log10(np.mean(a ** 2) / max(np.mean(b ** 2), 1e-20))
    L = lufs(x, sr)
    pk = 20 * np.log10(np.abs(x).max())
    return dict(lufs=L, peak=pk, plr=pk - L, side=db(side, mid), lowside=db(sosfilt(low, side), sosfilt(low, mid)))

if __name__ == "__main__":
    for p in sys.argv[1:]:
        m = measure(p)
        print(f"{p.split(chr(92))[-1]:<34} {m['lufs']:6.1f} LUFS  peak {m['peak']:5.1f}  PLR {m['plr']:4.1f}  side/mid {m['side']:6.1f} dB  below 150 Hz {m['lowside']:6.1f} dB")
