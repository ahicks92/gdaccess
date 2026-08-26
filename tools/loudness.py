"""Loudness of short cue files, K-weighted (ITU-R BS.1770) -- the basis for the sonar's per-cue dB trims.
Usage: uv run tools/loudness.py [--ref <name>] <wav>...   (default ref: the first file)
Prints, per file: length, K-weighted loudness (LKFS, ungated -- these are one-shots), A-weighted RMS (the
older wall-tone measure), true peak (dBFS), and the trim in dB that would bring it to the reference's K
loudness plus the headroom left after that trim (negative = the trim would clip). Files are decoded the way the
mod plays them: mixed to mono, resampled to 48 kHz (where the BS.1770 filters are defined)."""
import sys, os
import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter, resample_poly

RATE = 48000
# BS.1770-4 K-weighting at 48 kHz: stage 1 high shelf (+4 dB above ~1.5 kHz), stage 2 high-pass (~38 Hz).
K1_B = [1.53512485958697, -2.69169618940638, 1.19839281085285]; K1_A = [1.0, -1.69065929318241, 0.73248077421585]
K2_B = [1.0, -2.0, 1.0];                                          K2_A = [1.0, -1.99004745483398, 0.99007225036621]

def load_mono_48k(path):
    sr, d = wavfile.read(path)
    if d.dtype == np.uint8: x = (d.astype(np.float64) - 128.0) / 128.0
    elif d.dtype == np.int16: x = d.astype(np.float64) / 32768.0
    elif d.dtype == np.int32: x = d.astype(np.float64) / 2147483648.0
    else: x = d.astype(np.float64)
    if x.ndim == 2: x = x.mean(axis=1)
    if sr != RATE:
        from math import gcd
        g = gcd(sr, RATE); x = resample_poly(x, RATE // g, sr // g)
    return x

def k_loudness(x):
    y = lfilter(K2_B, K2_A, lfilter(K1_B, K1_A, x))
    ms = np.mean(y * y)
    return -0.691 + 10 * np.log10(ms) if ms > 0 else -np.inf

def a_weight_db(x):
    # A-weighting by FFT magnitude (IEC 61672), RMS in dB(A) relative to full scale
    n = len(x); X = np.fft.rfft(x * np.hanning(n)); f = np.fft.rfftfreq(n, 1 / RATE)
    f2 = f * f
    ra = (12194**2 * f2**2) / ((f2 + 20.6**2) * np.sqrt((f2 + 107.7**2) * (f2 + 737.9**2)) * (f2 + 12194**2) + 1e-30)
    a = 20 * np.log10(ra + 1e-30) + 2.0
    w = 10 ** (a / 20)
    p = np.sum((np.abs(X) * w) ** 2) / (np.sum(np.hanning(n) ** 2) * n / 2)
    return 10 * np.log10(p + 1e-30)

def true_peak_db(x):
    up = resample_poly(x, 4, 1)
    return 20 * np.log10(np.max(np.abs(up)) + 1e-30)

def main(argv):
    ref = None
    if len(argv) > 1 and argv[0] == "--ref": ref, argv = argv[1], argv[2:]
    rows = []
    for p in argv:
        x = load_mono_48k(p)
        rows.append((p, len(x) / RATE, k_loudness(x), a_weight_db(x), true_peak_db(x)))
    if not rows: print(__doc__); return
    ref_row = next((r for r in rows if ref and (os.path.basename(r[0]) == ref or r[0] == ref)), rows[0])
    print(f"reference: {os.path.basename(ref_row[0])}  K {ref_row[2]:.1f} LKFS")
    for p, secs, k, a, pk in rows:
        trim = ref_row[2] - k
        print(f"{os.path.basename(p):22} {secs:6.3f}s  K {k:6.1f} LKFS  A {a:6.1f} dB(A)  peak {pk:6.1f} dBFS  trim {trim:+5.1f} dB  headroom {-(pk + trim):+5.1f} dB")

if __name__ == "__main__": main(sys.argv[1:])
