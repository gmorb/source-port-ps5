#!/usr/bin/env python3
"""Original seamless loop: dark downtempo EBM with a bleak, industrial sci-fi atmosphere.

124 BPM, F minor, 32 bars (~61.9 s), 48 kHz stereo. Four-on-the-floor kick
with sidechained pads and a rolling bass, metallic clangs, wind and radio
static in the distance. All synthesized here; circular FX keep it seamless.
"""
import sys, wave
import numpy as np
from scipy.signal import butter, sosfilt, fftconvolve

SR = 48000
BPM = 88.0
BEAT = 60.0 / BPM
BAR = 4 * BEAT
NBARS = 24
N = int(round(NBARS * BAR * SR))
T = np.arange(N) / SR
rng = np.random.default_rng(1998)


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


def reverb_ir(seconds, decay, seed, bright=5000):
    r = np.random.default_rng(seed)
    n = int(seconds * SR)
    t = np.arange(n) / SR
    ir = filt(r.normal(0, 1, n) * np.exp(-t / decay), bright)
    fade = int(0.02 * SR)
    ir[:fade] *= np.linspace(0, 1, fade)
    return ir / np.sqrt(np.sum(ir ** 2))


def circ_lp_sweep(x, lfo, lo_fc, hi_fc):
    """Crossfade between two low-passed copies (cheap moving filter)."""
    a = filt(x, lo_fc); b = filt(x, hi_fc)
    return a * (1 - lfo) + b * lfo


# Fm - Db - Eb - Cm (2 bars each), repeated
PROG = [
    [53, 56, 60, 63],   # Fm(add9-ish: F Ab C Eb)
    [49, 53, 56, 60],   # Db maj7 (Db F Ab C)
    [51, 55, 58, 62],   # Eb (Eb G Bb D)
    [48, 51, 55, 58],   # Cm7 (C Eb G Bb)
]
CH = 2 * BAR


def chord_at(bar):
    return PROG[(bar // 2) % 4]


# ---------------------------------------------------------------- sidechain
def sidechain():
    env = np.ones(N)
    beat_n = int(BEAT * SR)
    t = np.arange(beat_n) / SR
    duck = 1 - 0.35 * np.exp(-t / 0.15)
    for bar in range(NBARS):
        for bt in (0, 2.5):
            circ_add(env, duck - 1, int((bar * BAR + bt * BEAT) * SR))
    return env


# ---------------------------------------------------------------- drums
def kick():
    n = int(0.42 * SR); t = np.arange(n) / SR
    f = 46 + 120 * np.exp(-t / 0.028)
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / 0.22)
    click = filt(rng.normal(0, 1, n), 3000, "high") * np.exp(-t / 0.003) * 0.25
    return np.tanh((body + click) * 1.6) * 0.9


def clap():
    n = int(0.5 * SR); t = np.arange(n) / SR
    noise = filt(rng.normal(0, 1, n), (900, 5000), "band")
    env = np.zeros(n)
    for d in (0, 0.009, 0.019, 0.03):
        k = int(d * SR)
        env[k:] += np.exp(-(t[: n - k]) / (0.012 if d < 0.03 else 0.12))
    return noise * env * 0.35


def hat(open_=False):
    n = int((0.22 if open_ else 0.05) * SR); t = np.arange(n) / SR
    x = filt(filt(rng.normal(0, 1, n), 6000, "high", 4), 11000)   # dark, rolled-off top
    return x * np.exp(-t / (0.07 if open_ else 0.014)) * 0.16


def clang(f0, seed):
    """Inharmonic metallic hit (industrial)."""
    r = np.random.default_rng(seed)
    n = int(2.5 * SR); t = np.arange(n) / SR
    x = np.zeros(n)
    for ratio, amp, dec in ((1.0, 1, 1.2), (2.76, 0.6, 0.8), (5.40, 0.4, 0.5), (8.93, 0.25, 0.3), (1.51, 0.5, 1.6)):
        x += amp * np.sin(2 * np.pi * f0 * ratio * t + r.random() * 6) * np.exp(-t / dec)
    return filt(x, 5000) * np.minimum(1, t / 0.002) * 0.12


def drums():
    D = np.zeros(N); H = np.zeros(N); C = np.zeros(N)
    K, CL, HC = kick(), clap(), hat()
    s16 = BEAT / 4
    for bar in range(NBARS):
        intro = bar < 2
        base = bar * BAR
        circ_add(D, K * (0.6 if intro else 1.0), int(base * SR))
        if not intro:
            circ_add(D, K * 0.8, int((base + 2.5 * BEAT) * SR))
            circ_add(D, CL * 1.3, int((base + 2 * BEAT) * SR))          # half-time snare on 3
            if bar % 2 == 1:
                circ_add(D, K * 0.55, int((base + 3.5 * BEAT) * SR))
            for k in range(8):                                            # quiet mechanical 8ths
                circ_add(H, HC * (0.55 if k % 2 else 0.3), int((base + k * 2 * s16) * SR))
        if bar % 2 == 0:
            circ_add(C, clang(midi(41 + (bar // 2) % 3 * 2), bar), int((base + 1.0 * BEAT) * SR))
        if bar % 4 == 3:
            circ_add(C, clang(midi(56), 100 + bar) * 0.5, int((base + 3.25 * BEAT) * SR))
    return D, H, C


# ---------------------------------------------------------------- bass (rolling offbeat 16ths)
def bass():
    out = np.zeros(N)
    seq = [0, 0, 12, 0, 0, 7, 0, 10]   # sequenced 8ths (root, octave, fifth, minor 7th)
    e8 = BEAT / 2
    for bar in range(NBARS):
        root = chord_at(bar)[0] - 24
        for i, iv in enumerate(seq):
            if bar < 2 and i % 2:
                continue
            n = int(e8 * SR * 0.7); t = np.arange(n) / SR
            f = midi(root + iv)
            saw = 2 * ((f * t) % 1.0) - 1
            env = np.minimum(1, t / 0.003) * np.exp(-t / 0.12)
            tone = filt(saw * env, 650)
            circ_add(out, tone * (0.55 if i % 2 == 0 else 0.42), int((bar * BAR + i * e8) * SR))
    sub = np.zeros(N)
    for bar in range(NBARS):
        root = chord_at(bar)[0] - 24
        n = int(BAR * SR); t = np.arange(n) / SR
        circ_add(sub, np.sin(2 * np.pi * midi(root) * t) * 0.18, int(bar * BAR * SR))
    return filt(out, 1200) + filt(sub, 110)


# ---------------------------------------------------------------- pads
def pads():
    L = np.zeros(N); R = np.zeros(N)
    seg = int(CH * SR); xf = int(0.8 * SR)
    for ci in range(NBARS // 2):
        ch = PROG[ci % 4]
        n = seg + xf; t = np.arange(n) / SR
        sl = np.zeros(n); sr_ = np.zeros(n)
        for note in ch:
            for det, pan in ((-0.12, 0.15), (0.0, 0.5), (0.13, 0.85)):
                f = midi(note) * 2 ** (det / 12)
                saw = 2 * ((f * t + rng.random()) % 1.0) - 1
                sl += saw * (1 - pan); sr_ += saw * pan
        env = np.ones(n)
        a = int(1.0 * SR)
        env[:a] = np.linspace(0, 1, a) ** 2
        env[-xf:] = np.linspace(1, 0, xf) ** 1.5
        start = int(ci * CH * SR) - xf // 2
        circ_add(L, sl * env, start); circ_add(R, sr_ * env, start)
    lfo = 0.5 - 0.5 * np.cos(2 * np.pi * T / (N / SR) * 2)
    L = circ_lp_sweep(L, lfo, 380, 1100); R = circ_lp_sweep(R, lfo, 380, 1100)
    return L * 0.036, R * 0.036


# ---------------------------------------------------------------- lead (sparse dark FM pluck)
def lead():
    L = np.zeros(N); R = np.zeros(N)
    pattern = [0, None, None, None, None, None, 2, None, None, None, 1, None, None, None, None, None]
    s16 = BEAT / 4
    for bar in range(4, NBARS):
        if bar % 8 in (6, 7):
            continue   # breathing space
        ch = chord_at(bar)
        for i, p in enumerate(pattern):
            if p is None:
                continue
            note = ch[p]
            f = midi(note)
            n = int(0.6 * SR); t = np.arange(n) / SR
            mod = np.sin(2 * np.pi * f * 2.0 * t) * 1.6 * np.exp(-t / 0.06)
            s = np.sin(2 * np.pi * f * t + mod) * np.exp(-t / 0.22) * np.minimum(1, t / 0.002)
            pan = 0.35 + 0.3 * (i % 3) / 2
            si = int((bar * BAR + i * s16) * SR)
            circ_add(L, s * (1 - pan), si); circ_add(R, s * pan, si)
    return filt(L, 3500) * 0.05, filt(R, 3500) * 0.05


def delay(x, time, fb, mix, damp=2500):
    d = int(time * SR)
    y = np.zeros(N); tap = x.copy(); g = 1.0
    for _ in range(7):
        tap = filt(np.roll(tap, d), damp, order=1)
        g *= fb
        y += tap * g
    return x + y * mix


# ---------------------------------------------------------------- atmosphere
def atmosphere():
    # low drone on the root, slowly beating
    drone = (np.sin(2 * np.pi * midi(29) * T) + 0.5 * np.sin(2 * np.pi * midi(29) * 1.003 * T) + 0.3 * np.sin(2 * np.pi * midi(36) * T)) * 0.06
    # wind: band-limited noise with slow movement (seamless LFO)
    w = filt(rng.normal(0, 1, N), (150, 1200), "band")
    w *= (0.5 + 0.5 * np.sin(2 * np.pi * T / (N / SR) * 3)) * 0.02
    # distant radio static bursts
    st = np.zeros(N)
    for bar in (3, 11, 19):
        n = int(1.2 * SR); t = np.arange(n) / SR
        b = filt(rng.normal(0, 1, n), (1500, 4000), "band") * (rng.random(n) < 0.3)
        b *= np.sin(np.pi * t / t[-1]) * 0.03
        circ_add(st, b, int((bar * BAR + 2 * BEAT) * SR))
    return drone, w, st


def main(out):
    sc = sidechain()
    D, H, C = drums()
    b = bass() * sc
    pl, pr = pads()
    pl *= sc; pr *= sc
    ll, lr = lead()
    ll = delay(ll, BEAT * 0.75, 0.5, 0.7); lr = delay(lr, BEAT * 0.5, 0.5, 0.7)
    drone, wind, static = atmosphere()

    ir_hall = reverb_ir(4.0, 1.2, 1, 3500)
    ir_hall2 = reverb_ir(4.0, 1.2, 2, 3500)
    ir_room = reverb_ir(1.0, 0.25, 3, 6000)

    dryL = D + H * 0.9 + b + pl + ll + drone + wind + static * 0.8 + C * 0.5
    dryR = D + H * 0.9 + b + pr + lr + drone + wind * 0.9 + static * 0.6 + C * 0.5
    sendL = pl * 0.8 + ll + C * 1.5 + static + D * 0.04
    sendR = pr * 0.8 + lr + C * 1.5 + static + D * 0.04
    wetL = circ_conv(sendL, ir_hall) * 0.6 + circ_conv(H * 0.3 + D * 0.08, ir_room) * 0.4
    wetR = circ_conv(sendR, ir_hall2) * 0.6 + circ_conv(H * 0.3 + D * 0.08, ir_room) * 0.4
    L = filt(dryL + wetL, 25, "high"); R = filt(dryR + wetR, 25, "high")
    # dark master: gentle top roll-off, glue saturation
    L = filt(L, 9000); R = filt(R, 9000)
    mix = np.stack([L, R], 1)
    mix /= np.max(np.abs(mix)) + 1e-9
    mix = np.tanh(mix * 1.5) / np.tanh(1.5)
    mix *= 10 ** (-1 / 20)
    pcm = (mix * 32767).astype(np.int16)
    with wave.open(out, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    print(f"wrote {out}: {N / SR:.2f} s")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "ebm-loop.wav")
