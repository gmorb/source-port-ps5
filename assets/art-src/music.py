#!/usr/bin/env python3
"""Original seamless loop: early-2000s atmospheric electronica (downtempo).

92 BPM, D minor, 24 bars (~62.6 s), 48 kHz stereo 16-bit.
Everything is synthesized here; reverb/delay are circular so the loop is seamless.
"""
import numpy as np
import sys
from scipy.signal import butter, sosfilt, fftconvolve

SR = 48000
BPM = 92.0
BEAT = 60.0 / BPM
BAR = 4 * BEAT
NBARS = 24
N = int(round(NBARS * BAR * SR))
rng = np.random.default_rng(2002)
t_all = np.arange(N) / SR


def midi(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def lp(x, fc, order=2):
    sos = butter(order, fc / (SR / 2), btype="low", output="sos")
    return sosfilt(sos, x)


def hp(x, fc, order=2):
    sos = butter(order, fc / (SR / 2), btype="high", output="sos")
    return sosfilt(sos, x)


def bp(x, lo, hi, order=2):
    sos = butter(order, [lo / (SR / 2), hi / (SR / 2)], btype="band", output="sos")
    return sosfilt(sos, x)


def circ_add(buf, sig, start):
    """Add sig into buf at start, wrapping around the loop end."""
    start %= N
    n = len(sig)
    end = start + n
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
    """Circular convolution (seamless reverb across the loop point)."""
    L = N + len(ir)
    y = fftconvolve(x, ir)[:L]
    out = y[:N].copy()
    tail = y[N:]
    out[:len(tail)] += tail
    return out


def reverb_ir(seconds, decay, seed, bright=6000):
    r = np.random.default_rng(seed)
    n = int(seconds * SR)
    t = np.arange(n) / SR
    ir = r.normal(0, 1, n) * np.exp(-t / decay)
    ir = lp(ir, bright)
    ir[: int(0.012 * SR)] *= np.linspace(0, 1, int(0.012 * SR))  # predelay-ish soft onset
    return ir / np.sqrt(np.sum(ir ** 2))


# chord progression (2 bars each, 12 changes): Dm9 Bbmaj7 Gm9 A7sus4 ... variation
CHORDS = [
    [50, 57, 60, 64, 65],   # Dm9 (D A C E F)
    [46, 53, 57, 60, 62],   # Bbmaj7(add9-ish)
    [43, 50, 53, 57, 58],   # Gm9
    [45, 52, 55, 59, 62],   # A7sus-ish (A E G B D)
    [50, 57, 60, 64, 65],   # Dm9
    [41, 48, 52, 55, 57],   # Fmaj7
    [46, 53, 57, 60, 62],   # Bbmaj7
    [45, 52, 55, 61, 64],   # A7 (with C#)
    [50, 57, 60, 64, 65],   # Dm9
    [46, 53, 57, 60, 62],   # Bbmaj7
    [48, 55, 58, 62, 64],   # C(add9)/Cm-ish colour
    [45, 52, 55, 59, 62],   # A7sus -> back to Dm
]
CH_LEN = 2 * BAR


# ---------------------------------------------------------------- pad
def pad():
    L = np.zeros(N); R = np.zeros(N)
    seg = int(round(CH_LEN * SR))
    xf = int(0.6 * SR)  # crossfade between chords
    for ci, ch in enumerate(CHORDS):
        n = seg + xf
        t = np.arange(n) / SR
        sl = np.zeros(n); sr_ = np.zeros(n)
        for note in ch:
            f = midi(note + 12)
            for d, pan in ((-0.09, 0.2), (0.0, 0.5), (0.11, 0.8)):
                ff = f * 2 ** (d / 12)
                ph = rng.random()
                saw = 2 * ((ff * t + ph) % 1.0) - 1
                sl += saw * (1 - pan); sr_ += saw * pan
        env = np.ones(n)
        a = int(0.5 * SR)
        env[:a] = np.linspace(0, 1, a) ** 1.5
        env[-xf:] = np.linspace(1, 0, xf) ** 1.5
        sl *= env; sr_ *= env
        start = int(round(ci * CH_LEN * SR)) - xf // 2
        circ_add(L, sl, start); circ_add(R, sr_, start)
    # slow filter sweep (one full cycle per loop -> seamless)
    lfo = 0.5 - 0.5 * np.cos(2 * np.pi * t_all / (N / SR) * 2)
    out = []
    for x in (L, R):
        lo = lp(x, 900, 2); hi = lp(x, 2600, 2)
        y = lo * (1 - lfo) + hi * lfo
        out.append(y)
    L, R = out
    g = 0.035
    return L * g, R * g


# ---------------------------------------------------------------- sub bass
def bass():
    out = np.zeros(N)
    pattern = [(0, 1.5), (1.75, 0.5), (2.5, 1.25)]  # beats within bar: (start, len)
    for bar in range(NBARS):
        root = CHORDS[(bar // 2) % len(CHORDS)][0] - 12
        f = midi(root)
        for (b, ln) in pattern:
            n = int(ln * BEAT * SR)
            t = np.arange(n) / SR
            env = np.minimum(1, t / 0.01) * np.exp(-t / (ln * BEAT * 0.9))
            s = np.sin(2 * np.pi * f * t) + 0.25 * np.sin(2 * np.pi * 2 * f * t)
            circ_add(out, s * env * 0.32, int((bar * BAR + b * BEAT) * SR))
    return lp(out, 300)


# ---------------------------------------------------------------- drums
def kick():
    n = int(0.45 * SR); t = np.arange(n) / SR
    f = 48 + 90 * np.exp(-t / 0.035)
    ph = 2 * np.pi * np.cumsum(f) / SR
    return np.sin(ph) * np.exp(-t / 0.16) * 0.9


def snare():
    n = int(0.35 * SR); t = np.arange(n) / SR
    noise = bp(rng.normal(0, 1, n), 1200, 7000) * np.exp(-t / 0.08)
    tone = np.sin(2 * np.pi * 185 * t) * np.exp(-t / 0.05)
    return (noise * 0.5 + tone * 0.35)


def hat(open_=False):
    n = int((0.25 if open_ else 0.06) * SR); t = np.arange(n) / SR
    x = hp(rng.normal(0, 1, n), 7000, 4)
    return x * np.exp(-t / (0.09 if open_ else 0.018)) * 0.18


def rim():
    n = int(0.08 * SR); t = np.arange(n) / SR
    return (np.sin(2 * np.pi * 1700 * t) + 0.5 * bp(rng.normal(0, 1, n), 2000, 5000)) * np.exp(-t / 0.012) * 0.18


def drums():
    D = np.zeros(N); Hs = np.zeros(N)
    K, S, Hc, Ho, Rm = kick(), snare(), hat(), hat(True), rim()
    swing = 0.07 * BEAT  # shuffle on off-16ths
    # 2 bar breakbeat-ish pattern in 16ths
    kick_pat = [0, 7, 10, 16, 23, 26]          # positions in 32 sixteenths
    snare_pat = [8, 24]
    ghost = [14, 29]
    for bar in range(NBARS):
        if bar < 2:
            continue  # intro breathing space
        base = bar * BAR
        two = (bar % 2) * 16
        for i in range(16):
            pos = two + i
            tt = base + i * BEAT / 4 + (swing if i % 2 else 0)
            si = int(tt * SR)
            if pos in kick_pat:
                circ_add(D, K, si)
            if pos in snare_pat:
                circ_add(D, S, si)
            if pos in ghost:
                circ_add(D, S * 0.25, si)
            if i % 2 == 0:
                circ_add(Hs, Hc * (1.0 if i % 4 == 2 else 0.6), si)
            elif rng.random() < 0.35:
                circ_add(Hs, Hc * 0.35, si)
            if i == 14 and bar % 4 == 3:
                circ_add(Hs, Ho, si)
            if i == 6 and bar % 2 == 1:
                circ_add(D, Rm, si)
    # first bars after the intro: low-passed drums open up over 4 bars
    return D, Hs


# ---------------------------------------------------------------- arp (glassy FM pluck)
def arp():
    L = np.zeros(N); R = np.zeros(N)
    order = [2, 3, 4, 3, 1, 3, 4, 2]
    step = BEAT / 2
    for bar in range(NBARS):
        if bar < 4:
            continue
        ch = CHORDS[(bar // 2) % len(CHORDS)]
        for i in range(8):
            note = ch[order[i]] + 24 + (12 if (i == 4 and bar % 4 == 3) else 0)
            f = midi(note)
            n = int(0.9 * SR); t = np.arange(n) / SR
            mod = np.sin(2 * np.pi * f * 3.5 * t) * 2.2 * np.exp(-t / 0.08)
            s = np.sin(2 * np.pi * f * t + mod) * np.exp(-t / 0.28) * np.minimum(1, t / 0.003)
            vel = 0.75 if i % 2 else 1.0
            pan = 0.3 + 0.4 * ((i * 3) % 8) / 7
            si = int((bar * BAR + i * step) * SR)
            circ_add(L, s * vel * (1 - pan), si); circ_add(R, s * vel * pan, si)
    return L * 0.055, R * 0.055


def delay(x, time, fb, mix, damp=4000):
    d = int(time * SR)
    y = np.zeros(N)
    tap = x.copy()
    gain = 1.0
    for _ in range(8):
        tap = lp(np.roll(tap, d), damp, 1)
        gain *= fb
        y += tap * gain
    return x + y * mix


# ---------------------------------------------------------------- air / texture
def air():
    x = rng.normal(0, 1, N)
    x = bp(x, 300, 5000)
    lfo = 0.6 + 0.4 * np.sin(2 * np.pi * t_all / (N / SR) * 3)
    return x * lfo * 0.006


def main(out_wav):
    pl, pr = pad()
    b = bass()
    d, h = drums()
    al, ar = arp()
    al = delay(al, BEAT * 0.75, 0.45, 0.6)
    ar = delay(ar, BEAT * 0.5, 0.45, 0.6)
    a_ = air()

    # drum filter opening over bars 2..6, gentle overall
    open_env = np.clip((t_all - 2 * BAR) / (4 * BAR), 0, 1)
    dl = lp(d, 1200); dh = d
    d = dl * (1 - open_env) + dh * open_env
    hh = h * (0.3 + 0.7 * open_env)

    ir_big = reverb_ir(3.2, 0.9, 1)
    ir_room = reverb_ir(1.2, 0.3, 2)

    dry_L = pl + al + b + d * 0.9 + hh * 0.8 + a_
    dry_R = pr + ar + b + d * 0.9 + hh * 0.8 + a_
    send_L = pl * 0.9 + al * 1.2 + d * 0.15 + a_
    send_R = pr * 0.9 + ar * 1.2 + d * 0.15 + a_
    wet_L = circ_conv(send_L, ir_big) * 0.55 + circ_conv(d * 0.3 + hh * 0.3, ir_room) * 0.3
    wet_R = circ_conv(send_R, reverb_ir(3.2, 0.9, 3)) * 0.55 + circ_conv(d * 0.3 + hh * 0.3, reverb_ir(1.2, 0.3, 4)) * 0.3

    L = hp(dry_L + wet_L, 25)
    R = hp(dry_R + wet_R, 25)
    # soft glue compression-ish: tanh saturation then normalize to -1 dBFS peak
    mix = np.stack([L, R], 1)
    mix /= np.max(np.abs(mix)) + 1e-9
    mix = np.tanh(mix * 1.4) / np.tanh(1.4)
    mix *= 10 ** (-1.0 / 20)
    pcm = (mix * 32767).astype(np.int16)

    import wave
    with wave.open(out_wav, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    print(f"wrote {out_wav}: {N / SR:.2f} s, {N} samples")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "launcher-loop.wav")
