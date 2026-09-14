"""Candidate hazard-lane loops, built like the wall tones: one mono 4 s loop per compass direction, the
direction's pitch band matching the wall bank (north ~1230 Hz, east/west ~830, south ~430; measured
2026-09-13 from assets/audio/walltones/2), RMS -20 dBFS like the walls, seamless loop seam. What differs
is the TEXTURE, so a hazard lane and a wall lane can share a pitch and still read as two things.

  sizzle    dense irregular impulses, ~35/s, 3 ms bursts (rough buzz) -- the lane bank (chosen by ear 2026-09-13
            over crackle [collided with the enemy sonar pulse], pulsed and whistle [collided with the walls])
  inside_*  the "you are standing in it" bed: a left/right PAIR from independent seeds, played wide, not lane-panned
    inside_throb   broadband sizzle, denser (60/s), with a 1 Hz swell matching the damage tick
    inside_low     dense steady sizzle (80/s) pitched at 260 Hz, below the south wall band (200 was "a bit too
                   low" by ear, 2026-09-13; a third layer is planned, so it stays modest). Chosen over inside_throb.
  Each bed also gets a preview.wav: the left/right pair as one stereo file for auditioning only (the mixer plays
  the two monos, hard-panned; it decodes every file to mono).

Usage: uv run tools/gen_hazard_cues.py [out_dir]   (default assets/audio/hazards)"""
import os, sys, wave
import numpy as np
from scipy import signal

SR = 44100
LEN = 4.0
SEAM = 0.5
TARGET_RMS_DB = -20.0
BANDS = {"north": 1234.0, "east": 834.0, "south": 426.0, "west": 834.0}   # centre Hz, per wall-tone direction
SEEDS = {"north": 11, "east": 22, "south": 33, "west": 44}
BW_RATIO = 1.73   # f90/f10 of the wall tones


def bandpass(x, fc, ratio=BW_RATIO, order=4):
    lo, hi = fc / np.sqrt(ratio), fc * np.sqrt(ratio)
    sos = signal.butter(order, [lo, hi], btype="band", fs=SR, output="sos")
    return signal.sosfiltfilt(sos, x)


def loopify(x):
    """Crossfade the tail into the head so the loop point is silent-seam free, then cut to LEN."""
    n = int(LEN * SR); m = int(SEAM * SR)
    head = x[:n].copy()
    tail = x[n:n + m]
    ramp = np.linspace(0, 1, m)
    head[:m] = head[:m] * ramp + tail * (1 - ramp)
    return head


def normalize(x):
    rms = np.sqrt(np.mean(x ** 2)) + 1e-12
    x = x * (10 ** (TARGET_RMS_DB / 20) / rms)
    peak = np.max(np.abs(x))
    if peak > 0.98: x = x * (0.98 / peak)
    return x


def impulses(rng, rate_per_s, total_s, jitter=1.0):
    """Impulse onset times: Poisson (jitter 1) down to regular (jitter 0)."""
    n = int(rate_per_s * total_s)
    if jitter >= 1.0:
        return np.sort(rng.uniform(0, total_s, n))
    base = np.arange(n) / rate_per_s
    return np.clip(base + rng.uniform(-jitter, jitter, n) / rate_per_s, 0, total_s - 1e-3)


def burst(fc, decay_s, detune, rng):
    t = np.arange(int(decay_s * 6 * SR)) / SR
    f = fc * (1 + rng.uniform(-detune, detune))
    return np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi)) * np.exp(-t / decay_s)


def render_impulses(rng, fc, rate, decay_s, detune, jitter, amp_spread, total_s):
    out = np.zeros(int(total_s * SR) + int(decay_s * 6 * SR) + 1)
    for t0 in impulses(rng, rate, total_s, jitter):
        b = burst(fc, decay_s, detune, rng) * rng.uniform(1 - amp_spread, 1)
        i = int(t0 * SR); out[i:i + len(b)] += b
    return out[: int(total_s * SR)]


def gen_crackle(rng, fc):
    x = render_impulses(rng, fc, rate=28, decay_s=0.008, detune=0.12, jitter=1.0, amp_spread=0.7, total_s=LEN + SEAM)
    return bandpass(x, fc, ratio=3.0, order=2)   # keep the clicks' transients, trim the extremes


def gen_sizzle(rng, fc):
    x = render_impulses(rng, fc, rate=35, decay_s=0.003, detune=0.06, jitter=0.6, amp_spread=0.4, total_s=LEN + SEAM)
    return bandpass(x, fc, ratio=3.0, order=2)


def gen_pulsed(rng, fc):
    n = int((LEN + SEAM) * SR)
    noise = bandpass(rng.standard_normal(n), fc)
    t = np.arange(n) / SR
    phase = (t % 1.0)
    env = np.clip(np.minimum(phase / 0.02, (0.45 - phase) / 0.06), 0, 1)   # 1 Hz, ~45 % duty, 20 ms in / 60 ms out
    env = env * (1 - 0.1) + 0.1                                            # -20 dB floor so the lane never vanishes
    return noise * env


def gen_whistle(rng, fc):
    n = int((LEN + SEAM) * SR)
    noise = rng.standard_normal(n)
    # slow pitch wobble: resonate in short blocks with a wandering centre
    out = np.zeros(n); blk = 2048; hop = 1024; win = np.hanning(blk)
    wob = signal.sosfiltfilt(signal.butter(2, 1.5, fs=SR / hop, output="sos"), rng.standard_normal(n // hop + 2))
    wob = wob / (np.max(np.abs(wob)) + 1e-9) * 0.06
    for k, i in enumerate(range(0, n - blk, hop)):
        f = fc * (1 + wob[k]); q = 18.0
        b, a = signal.iirpeak(f, q, fs=SR)
        out[i:i + blk] += signal.lfilter(b, a, noise[i:i + blk]) * win
    return out


def gen_inside_throb(rng, fc):
    x = render_impulses(rng, fc, rate=60, decay_s=0.003, detune=0.5, jitter=0.6, amp_spread=0.4, total_s=LEN + SEAM)
    n = len(x); t = np.arange(n) / SR
    swell = 0.55 + 0.45 * np.clip(np.sin(2 * np.pi * (t % 1.0) * 0.5) ** 0.5, 0, 1)   # one swell per second, never off
    return x * swell


def gen_inside_low(rng, fc):
    x = render_impulses(rng, 260.0, rate=80, decay_s=0.005, detune=0.15, jitter=0.6, amp_spread=0.4, total_s=LEN + SEAM)
    return bandpass(x, 260.0, ratio=3.0, order=2)


GENS = {"sizzle": gen_sizzle}

# The pointer is synthesized in the mod (audio::pulse: triangle, 10 ms rise, linear decay); this preview is the
# sound glossary's demo of it and must track src/hazard.cpp's defaults: 90 ms, level 0.7, a round of three
# islands 0.2 s apart then a 0.35 s gap, pitch 220 Hz due south .. 880 due north (log), pan = east/west.
PULSE_MS, PULSE_VOL, PERIOD, GAP, LO, HI = 90, 0.7, 0.2, 0.35, 220.0, 880.0


def gen_exit_preview():
    def tri(f, n):
        t = np.arange(n) / SR; ph = (t * f) % 1.0
        return 4 * np.abs(ph - 0.5) - 1
    def pulse(f):
        n = int(SR * PULSE_MS / 1000)
        env = np.clip(np.minimum(np.linspace(0, 1, n) / 0.11, np.linspace(1, 0, n)), 0, 1)
        return tri(f, n) * env * PULSE_VOL
    def gains(pan):
        a = (pan + 1) / 4 * np.pi; return np.cos(a), np.sin(a)
    # three islands: south-west, east, north-north-east -- one round repeated four times
    islands = [(-0.6, -0.8), (1.0, 0.0), (0.4, 0.9)]   # (east, north)
    total = 4 * (len(islands) * PERIOD + GAP) + 0.5
    out = np.zeros((int(SR * total), 2)); t0 = 0.0
    for _ in range(4):
        for east, north in islands:
            f = LO * (HI / LO) ** ((north + 1) / 2); gl, gr = gains(east)
            p = pulse(f); i = int(t0 * SR)
            out[i:i + len(p), 0] += p * gl; out[i:i + len(p), 1] += p * gr
            t0 += PERIOD
        t0 += GAP
    return out

BEDS = {"inside_throb": gen_inside_throb, "inside_low": gen_inside_low}


def write(path, x):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    w = wave.open(path, "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((np.clip(x, -1, 1) * 32767).astype("<i2").tobytes()); w.close()


def write_stereo(path, l, r):
    w = wave.open(path, "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    inter = np.empty(len(l) * 2, dtype="<i2"); inter[0::2] = (np.clip(l, -1, 1) * 32767).astype("<i2"); inter[1::2] = (np.clip(r, -1, 1) * 32767).astype("<i2")
    w.writeframes(inter.tobytes()); w.close()


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join("assets", "audio", "hazards")
    for name, gen in GENS.items():
        for d, fc in BANDS.items():
            rng = np.random.default_rng(SEEDS[d] * 100 + len(name))
            x = normalize(loopify(gen(rng, fc)))
            write(os.path.join(out, name, f"{d}.wav"), x)
            print(f"{name}/{d}.wav  fc {fc:.0f} Hz  rms {20*np.log10(np.sqrt(np.mean(x**2))):.1f} dBFS  peak {20*np.log10(np.max(np.abs(x))):.1f} dBFS")
    for name, gen in BEDS.items():
        pair = []
        for side, seed in (("left", 7), ("right", 8)):
            rng = np.random.default_rng(seed * 1000 + len(name))
            x = normalize(loopify(gen(rng, 834.0)))
            write(os.path.join(out, name, f"{side}.wav"), x); pair.append(x)
            print(f"{name}/{side}.wav  rms {20*np.log10(np.sqrt(np.mean(x**2))):.1f} dBFS  peak {20*np.log10(np.max(np.abs(x))):.1f} dBFS")
        write_stereo(os.path.join(out, name, "preview.wav"), pair[0], pair[1])
    prev = gen_exit_preview()
    write_stereo(os.path.join(out, "exit_pulse_preview.wav"), prev[:, 0], prev[:, 1])
    print(f"exit_pulse_preview.wav  {len(prev)/SR:.1f} s, peak {20*np.log10(np.max(np.abs(prev))):.1f} dBFS")


if __name__ == "__main__":
    main()
