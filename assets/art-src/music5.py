#!/usr/bin/env python3
"""Original seamless loop: modern dark cinematic ambient.

No beat, no retro synths: sub drone, a granular shimmer cloud in F minor,
bowed-metal resonances, reversed swells into soft low 'braam' hits, wind.
64 s, 48 kHz stereo; circular FX keep the loop seamless.
"""
import sys, wave
import numpy as np
from scipy.signal import butter, sosfilt, fftconvolve, lfilter

SR = 48000
DUR = 64.0
N = int(DUR * SR)
T = np.arange(N) / SR
rng = np.random.default_rng(2026)


def midi(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def filt(x, fc, kind="low", order=2):
    if kind == "band":
        sos = butter(order, [fc[0] / (SR / 2), fc[1] / (SR / 2)], btype="band", output="sos")
    else:
        sos = butter(order, fc / (SR / 2), btype=kind, output="sos")
    return sosfilt(sos, x)


def circ_add(buf, sig, start):
    start %= N
    end = start + len(sig)
    if end <= N:
        buf[start:end] += sig
    else:
        k = N - start
        buf[start:] += sig[:k]
        rest = sig[k:]
        while len(rest):
            m = min(len(rest), N)
            buf[:m] += rest[:m]
            rest = rest[m:]


def circ_conv(x, ir):
    y = fftconvolve(x, ir)
    out = y[:N].copy()
    tail = y[N:]
    while len(tail):
        m = min(len(tail), N)
        out[:m] += tail[:m]
        tail = tail[m:]
    return out


def reverb_ir(seconds, decay, seed, bright=6000):
    r = np.random.default_rng(seed)
    n = int(seconds * SR)
    t = np.arange(n) / SR
    ir = filt(r.normal(0, 1, n) * np.exp(-t / decay), bright)
    # slow diffuse onset (big space)
    a = int(0.08 * SR)
    ir[:a] *= np.linspace(0, 1, a) ** 2
    return ir / np.sqrt(np.sum(ir ** 2))


def loop_lfo(cycles, phase=0.0):
    return 0.5 - 0.5 * np.cos(2 * np.pi * (T / DUR * cycles + phase))


SCALE = [41, 44, 48, 51, 53, 55, 56, 60, 63, 65, 67, 68, 72, 75, 77]  # F minor (F Ab C Eb G)


# ---------------------------------------------------------------- sub drone
def drone():
    f = midi(29)  # F1
    x = np.sin(2 * np.pi * f * T) + 0.35 * np.sin(2 * np.pi * f * 2 * T + 0.3)
    x += 0.25 * np.sin(2 * np.pi * midi(36) * T)  # C2, a fifth above
    amp = 0.6 + 0.4 * loop_lfo(4)
    return x * amp * 0.09


# ---------------------------------------------------------------- granular cloud
def grains(count, octave_shift, gain, seed, lp):
    r = np.random.default_rng(seed)
    L = np.zeros(N); R = np.zeros(N)
    for _ in range(count):
        note = SCALE[r.integers(len(SCALE))] + octave_shift
        f = midi(note) * 2 ** (r.normal(0, 0.03) / 12)
        dur = r.uniform(1.5, 4.5)
        n = int(dur * SR); t = np.arange(n) / SR
        env = np.sin(np.pi * t / dur) ** 2
        # soft, slightly detuned partial pair: glassy but warm
        x = np.sin(2 * np.pi * f * t + r.random() * 6) + 0.3 * np.sin(2 * np.pi * f * 2.003 * t)
        x *= env * r.uniform(0.4, 1.0)
        pan = r.uniform(0.1, 0.9)
        st = int(r.uniform(0, DUR) * SR)
        circ_add(L, x * np.sqrt(1 - pan), st); circ_add(R, x * np.sqrt(pan), st)
    # density breathes over the loop
    breathe = 0.45 + 0.55 * loop_lfo(2, 0.25)
    return filt(L, lp) * breathe * gain, filt(R, lp) * breathe * gain


# ---------------------------------------------------------------- bowed metal
def bowed_metal(f0, start_s, dur_s, seed, gain):
    r = np.random.default_rng(seed)
    n = int(dur_s * SR); t = np.arange(n) / SR
    exc = filt(r.normal(0, 1, n), (f0 * 0.8, f0 * 9), "band")
    out = np.zeros(n)
    for ratio, q in ((1.0, 0.9993), (2.32, 0.9990), (4.25, 0.9986), (6.63, 0.998)):
        f = f0 * ratio
        w = 2 * np.pi * f / SR
        a = [1, -2 * q * np.cos(w), q * q]          # 2-pole resonator
        out += lfilter([1 - q], a, exc) / ratio
    env = np.sin(np.pi * t / dur_s) ** 1.5
    out = out / (np.max(np.abs(out)) + 1e-9) * env * gain
    sig = np.zeros(N)
    circ_add(sig, out, int(start_s * SR))
    return sig


# ---------------------------------------------------------------- reversed swell + braam
def swell_and_braam(hit_s, seed):
    r = np.random.default_rng(seed)
    L = np.zeros(N); R = np.zeros(N)
    # reversed swell: noise + tonal cloud rising for 4 s into the hit
    sw = 4.0
    n = int(sw * SR); t = np.arange(n) / SR
    rise = (t / sw) ** 3
    nz = filt(r.normal(0, 1, n), (200, 2500), "band")
    tone = sum(np.sin(2 * np.pi * midi(m) * t + r.random() * 6) for m in (53, 60, 65)) / 3
    s = (nz * 0.25 + tone * 0.6) * rise
    circ_add(L, s, int((hit_s - sw) * SR)); circ_add(R, s[::-1][::-1] * 0.95, int((hit_s - sw) * SR))
    # braam: low detuned saws, saturated and low-passed, long decay
    bd = 6.0
    n = int(bd * SR); t = np.arange(n) / SR
    b = np.zeros(n)
    for m, det in ((29, -0.08), (29, 0.08), (41, 0.0), (36, 0.05), (44, -0.04)):
        f = midi(m) * 2 ** (det / 12)
        b += 2 * ((f * t + r.random()) % 1.0) - 1
    b = np.tanh(b * 0.9)
    env = np.minimum(1, t / 0.06) * np.exp(-t / 1.8)
    fc_env = 250 + 900 * np.exp(-t / 0.8)
    b = filt(b, 900) * 0.6 + filt(b, 300) * 0.4
    b *= env * 0.09
    _ = fc_env
    circ_add(L, b, int(hit_s * SR)); circ_add(R, b, int(hit_s * SR))
    return L, R


# ---------------------------------------------------------------- wind / air
def air():
    w = filt(rng.normal(0, 1, N), (120, 900), "band") * (0.4 + 0.6 * loop_lfo(3, 0.1)) * 0.022
    h = filt(rng.normal(0, 1, N), (2500, 6000), "band") * (0.3 + 0.7 * loop_lfo(5, 0.6)) * 0.0035
    return w + h, w * 0.9 + h * 1.1


def main(out):
    dr = drone()
    gl1, gr1 = grains(140, 0, 0.022, 1, 2500)        # mid cloud
    gl2, gr2 = grains(90, 12, 0.016, 2, 6000)        # high shimmer
    gl3, gr3 = grains(40, -12, 0.03, 3, 700)         # low body
    m1 = bowed_metal(midi(44), 6.0, 12.0, 10, 0.05)
    m2 = bowed_metal(midi(51), 30.0, 14.0, 11, 0.045)
    m3 = bowed_metal(midi(39), 47.0, 13.0, 12, 0.05)
    s1L, s1R = swell_and_braam(16.0, 20)
    s2L, s2R = swell_and_braam(48.0, 21)
    aL, aR = air()

    L = dr + gl1 + gl2 + gl3 + m1 * 0.8 + m2 * 0.4 + m3 * 0.9 + s1L + s2L + aL
    R = dr + gr1 + gr2 + gr3 + m1 * 0.4 + m2 * 0.9 + m3 * 0.5 + s1R + s2R + aR

    # big modern space: long dark hall plus a brighter 'shimmer' send
    hall_L = circ_conv(L - dr, reverb_ir(7.0, 2.2, 31, 4500))
    hall_R = circ_conv(R - dr, reverb_ir(7.0, 2.2, 32, 4500))
    shim_L = circ_conv(gl2 + gl1 * 0.3, reverb_ir(9.0, 3.0, 33, 9000))
    shim_R = circ_conv(gr2 + gr1 * 0.3, reverb_ir(9.0, 3.0, 34, 9000))
    L = L * 0.55 + hall_L * 0.75 + shim_L * 0.5
    R = R * 0.55 + hall_R * 0.75 + shim_R * 0.5
    L = filt(filt(L, 25, "high"), 11000); R = filt(filt(R, 25, "high"), 11000)
    mix = np.stack([L, R], 1)
    mix /= np.max(np.abs(mix)) + 1e-9
    mix = np.tanh(mix * 1.2) / np.tanh(1.2)
    mix *= 10 ** (-1 / 20)
    with wave.open(out, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((mix * 32767).astype(np.int16).tobytes())
    print(f"wrote {out}: {DUR:.1f} s")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "modern-loop.wav")
