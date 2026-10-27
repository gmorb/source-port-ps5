#!/usr/bin/env python3
"""Minimal ATRAC9 encoder (48 kHz stereo, 192 kb/s, PS4/PS5 snd0.at9 profile).

Bitstream syntax follows FFmpeg's atrac9dec.c (the decoder is the spec here);
every file this writes is checked by decoding it with FFmpeg.
Coding choices are deliberately simple: one CPE block per frame, no band
extension, no intensity stereo, gradient mode 0, CLC scalefactors for the
first channel and baseline-delta (VLC) scalefactors for the second.
"""
import json, os, struct, sys, wave
import numpy as np

T = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "tables.json")))
SR_IDX = 7                      # 48000 Hz
FRAME_LOG2 = T["at9_tab_sri_frame_log2"][SR_IDX]
NFR = 1 << FRAME_LOG2           # 256 samples per frame
FRAMES_PER_SF = 4               # superframe index 2
SF_BYTES = 512                  # 192 kb/s stereo
FRAME_BITS = SF_BYTES * 8 // FRAMES_PER_SF
Q2C = T["at9_q_unit_to_coeff_idx"]
Q2N = T["at9_q_unit_to_coeff_cnt"]
Q2CB = T["at9_q_unit_to_codebookidx"]
QSC = np.array(T["at9_quant_step_coarse"])
QSF = np.array(T["at9_quant_step_fine"])
SFC = np.array(T["at9_scalefactor_c"])
BANDMAP = T["at9_tab_band_q_unit_map"]


# ------------------------------------------------------------------ VLCs
def build_codes(entries):
    """FFmpeg ff_vlc_init_from_lengths: codes assigned in table order."""
    code = 0
    out = {}
    for sym, ln in entries:
        if ln > 0:
            out[sym] = (code >> (32 - ln), ln)
            code += 1 << (32 - ln)
        elif ln < 0:
            code += 1 << (32 + ln)
    return out


SF_SIGNED = {}
_tab = T["sfb_b"]
_pos = 0
for i in range(2, 6):
    size = T["sf_signed"][i][0]
    SF_SIGNED[i] = {s - 16: c for s, c in build_codes(_tab[_pos:_pos + size]).items()}
    _pos += size

SF_UNSIGNED = {}
_tab = T["sfb_a"]
_pos = 0
for i in range(1, 7):
    size = T["sf_unsigned"][i][0]
    SF_UNSIGNED[i] = build_codes(_tab[_pos:_pos + size])
    _pos += size
SF_WEIGHTS = T["sf_weights"]

COEFF = {}  # (cb, prec, cbi) -> (codes{sym:(code,len)}, value_cnt, value_cnt_pow, value_bits)
_tab = T["coeffs_tab"]
_pos = 0
for i in range(2):
    for j in range(2, 8):
        for k in range(i, 4):
            size, vc, vcp, vb = T["huff_coeffs"][i][j][k]
            COEFF[(i, j, k)] = (build_codes(_tab[_pos:_pos + size]), vc, vcp, vb)
            _pos += size
assert _pos == len(_tab), (_pos, len(_tab))


def sext(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


# Pre-decode every coefficient codebook into value tuples for a nearest search.
COEFF_VALUES = {}
for key, (codes, vc, vcp, vb) in COEFF.items():
    syms = sorted(codes)
    vals = np.array([[sext(s >> (k * vb), vb) for k in range(vc)] for s in syms], float)
    COEFF_VALUES[key] = (np.array(syms), vals)


CODE_LIST = {key: [COEFF[key][0][int(sym)] for sym in COEFF_VALUES[key][0]] for key in COEFF}


class BitWriter:
    def __init__(self):
        self.bits = []

    def put(self, v, n):
        for i in range(n - 1, -1, -1):
            self.bits.append((v >> i) & 1)

    def align(self):
        while len(self.bits) % 8:
            self.bits.append(0)

    def bytes(self):
        b = bytearray()
        for i in range(0, len(self.bits), 8):
            byte = 0
            for bit in self.bits[i:i + 8]:
                byte = byte << 1 | bit
            b.append(byte)
        return bytes(b)


# ------------------------------------------------------------------ transform
_n = np.arange(NFR)
_s = np.sin(((_n + 0.5) / NFR) * np.pi - np.pi / 2) * 0.5 + 0.5      # rising, analysis
WIN = np.concatenate([_s, _s[::-1]])                                     # 2N analysis window
_N = NFR
_k = np.arange(_N)
_nn = np.arange(2 * _N)
MDCT_M = np.cos(np.pi / _N * (_nn[None, :] + 0.5 + _N / 2) * (_k[:, None] + 0.5))


def mdct(block):
    return MDCT_M @ (block * WIN)


# ------------------------------------------------------------------ helpers
def codebook_set(sf, q_unit_cnt):
    """calc_codebook_idx() from the decoder."""
    cbs = [0] * 30
    if q_unit_cnt <= 1:
        return cbs
    s = list(sf) + [0] * (31 - len(sf))
    s[q_unit_cnt] = s[q_unit_cnt - 1]
    avg = 0
    if q_unit_cnt > 12:
        avg = sum(s[:12])
        avg = (avg + 6) // 12
    for i in range(8, q_unit_cnt):
        prev, cur, nxt = s[i - 1], s[i], s[i + 1]
        mn = min(prev, nxt)
        if cur - mn >= 3 or 2 * cur - prev - nxt >= 3:
            cbs[i] = 1
    for i in range(12, q_unit_cnt):
        cur = s[i]
        cnd = 1 if Q2N[i] == 16 else 0
        mn = min(s[i + 1], s[i - 1])
        if cbs[i]:
            continue
        cbs[i] = 1 if (cur - mn >= 2 and cur >= avg - cnd) else 0
    return cbs


B_DIST = T["at9_tab_b_dist"]
ALLOC_CURVE = [[B_DIST[(j * len(B_DIST)) // i] for j in range(i)] for i in range(1, len(B_DIST) + 1)]
GRAD_MODE = 1      # Sony's encoder uses mode 1; 0 = flat gradient
GRAD_BOUNDARY = 15


def grad_start_unit(q_unit_cnt):
    return max(20, q_unit_cnt - 1)


def gradient_mode1(q_unit_cnt, gsv):
    gsu = grad_start_unit(q_unit_cnt)
    grad = [0] * 31
    values = 31 - gsv
    sign = 1 - 2 * (values < 0)
    base = gsv + sign
    scale = (abs(values) - 1) / 31.0
    curve = ALLOC_CURVE[31 - gsu - 1]
    for i in range(q_unit_cnt + 1):
        grad[i] = gsv if i < gsu else 31
    for i in range(gsu, 31):
        grad[i] = base + sign * int(scale * curve[i - gsu])
    return grad


def precisions(sf, q_unit_cnt, g):
    if GRAD_MODE == 1:
        grad = gradient_mode1(q_unit_cnt, g)
        mask = [0] * 30
        for i in range(1, q_unit_cnt):
            delta = abs(sf[i] - sf[i - 1]) - 1
            if delta > 0:
                neg = 1 if sf[i - 1] > sf[i] else 0
                mask[i - neg] += min(delta, 5)
        pc, pf = [], []
        gb = min(GRAD_BOUNDARY, q_unit_cnt)
        for i in range(q_unit_cnt):
            p = sf[i] + mask[i] - grad[i]
            if p >= 0:
                p >>= 1
            p = max(p, 1)
            if i < gb:
                p += 1
            f = 0
            if p > 15:
                f = min(p, 30) - 15
                p = 15
            pc.append(p); pf.append(f)
        return pc, pf
    return precisions_flat(sf, q_unit_cnt, g)


def precisions_flat(sf, q_unit_cnt, g):
    pc, pf = [], []
    for i in range(q_unit_cnt):
        p = max(sf[i] - g, 1)
        f = 0
        if p > 15:
            f = min(p, 30) - 15
            p = 15
        pc.append(p); pf.append(f)
    return pc, pf


def quantize_channel(x, sf, q_unit_cnt, g):
    """Returns (bitcost, writer_fn, reconstruction) for one channel."""
    pc, pf = precisions(sf, q_unit_cnt, g)
    cbs = codebook_set(sf, q_unit_cnt)
    out_ops = []   # list of (value, nbits)
    recon = np.zeros(256)
    for i in range(q_unit_cnt):
        a, b = Q2C[i], Q2C[i + 1]
        scale = SFC[sf[i]]
        v = x[a:b] / scale
        prec = pc[i] + 1
        step = QSC[pc[i]]
        if prec <= 7:
            key = (cbs[i], prec, Q2CB[i])
            codes, vc, vcp, vb = COEFF[key]
            syms, vals = COEFF_VALUES[key]
            targets = (v / step).reshape(-1, vc)
            d = ((targets[:, None, :] - vals[None, :, :]) ** 2).sum(axis=2)
            js = np.argmin(d, axis=1)
            for j in js:
                out_ops.append(CODE_LIST[key][j])
            q = vals[js].reshape(-1)
        else:
            lim = (1 << (prec - 1))
            q = np.clip(np.round(v / step), -lim, lim - 1)
            for qq in q:
                out_ops.append((int(qq) & ((1 << prec) - 1), prec))
        rec = q * step
        recon[a:b] = rec
    fine_ops = []
    for i in range(q_unit_cnt):
        if pf[i] <= 0:
            continue
        a, b = Q2C[i], Q2C[i + 1]
        ln = pf[i] + 1
        lim = 1 << (ln - 1)
        r = x[a:b] / SFC[sf[i]] - recon[a:b]
        qf = np.clip(np.round(r / QSF[pf[i]]), -lim, lim - 1)
        recon[a:b] += qf * QSF[pf[i]]
        for qq in qf:
            fine_ops.append((int(qq) & ((1 << ln) - 1), ln))
    for i in range(q_unit_cnt):
        a, b = Q2C[i], Q2C[i + 1]
        recon[a:b] *= SFC[sf[i]]
    ops = out_ops + fine_ops
    return sum(n for _, n in ops), ops, recon


def choose_sf(x, q_unit_cnt):
    sf = []
    for i in range(q_unit_cnt):
        m = np.max(np.abs(x[Q2C[i]:Q2C[i + 1]]))
        s = int(np.searchsorted(SFC, m * 0.999))
        sf.append(min(max(s, 0), 31))
    return sf


IS_BAND = 99  # no intensity stereo (Sony's encoder does not use it either)


def sf0_vlc_plan(sf):
    """Best mode-0 (VLC delta) coding of channel 0's scalefactors."""
    best = None
    for wi, w in enumerate(SF_WEIGHTS):
        v = [s_ + w[i] for i, s_ in enumerate(sf)]
        base = min(min(v), 31)
        stored = [x - base for x in v]
        top = max(stored)
        for ln in range(3, 7):
            if top >= (1 << ln):
                continue
            bits = 2 + 3 + 5 + 2 + ln
            m = (1 << ln) - 1
            for i in range(1, len(stored)):
                bits += SF_UNSIGNED[ln][(stored[i] - stored[i - 1]) & m][1]
            if best is None or bits < best[0]:
                best = (bits, wi, base, ln, stored)
    clc = 2 + 2 + 5 * len(sf)
    if best is None or best[0] >= clc:
        return (clc, None)
    return best


def frame_setup(X, band_count):
    q_unit_cnt = BANDMAP[band_count]
    is_band = min(IS_BAND, band_count)
    stereo_q = BANDMAP[is_band]
    L, R = X
    Mid = (L + R) * 0.5
    X0 = L.copy(); X1 = R.copy()
    a = Q2C[stereo_q]
    X0[a:] = Mid[a:]
    sf0 = choose_sf(X0, q_unit_cnt)
    sf1 = choose_sf(X1, q_unit_cnt)
    sf1[stereo_q:] = sf0[stereo_q:]      # IS units: same level in both channels
    sf1 = [min(max(s1, s0 - 16), s0 + 15) for s0, s1 in zip(sf0, sf1)]
    sf1 = [min(max(s, 0), 31) for s in sf1]
    header_bits = 2 + 4 + 4 + 1 + ((2 + 5 + 5 + 4) if GRAD_MODE == 1 else (2 + 6 + 6 + 5 + 5 + 4)) + 2 + 1
    sf0p = sf0_vlc_plan(sf0)
    sf_bits1 = 2 + 2 + sum(SF_SIGNED[5][s1 - s0][1] for s0, s1 in zip(sf0, sf1))
    return (q_unit_cnt, stereo_q, is_band, sf0, sf1, sf0p, header_bits + sf0p[0] + sf_bits1, X0, X1)


def frame_trial(X, setup, g):
    q_unit_cnt, stereo_q, is_band, sf0, sf1, sf0p, fixed, X0, X1 = setup
    c0 = quantize_channel(X0, sf0, q_unit_cnt, g)
    c1 = quantize_channel(X1, sf1, stereo_q, g)
    bits = fixed + c0[0] + c1[0]
    return (bits + 7) // 8 * 8, c0, c1


def write_frame(bw, first, band_count, setup, g, c0, c1):
    q_unit_cnt, stereo_q, is_band, sf0, sf1, sf0p, _, _, _ = setup
    bw.put(0 if first else 1, 1)     # first_in_pkt = !bit
    bw.put(0, 1)                     # reuse_params
    bw.put(band_count - 3, 4)
    bw.put(is_band - 3, 4)           # intensity stereo above this band
    bw.put(0, 1)                     # has_band_ext
    if GRAD_MODE == 1:
        bw.put(1, 2)                 # grad_mode 1 (as Sony's encoder)
        bw.put(grad_start_unit(q_unit_cnt), 5)
        bw.put(g, 5)
        bw.put(min(GRAD_BOUNDARY, q_unit_cnt), 4)
    else:
        bw.put(0, 2)                 # grad_mode 0
        bw.put(30, 6); bw.put(30, 6) # grad range [30, 31)
        bw.put(g, 5); bw.put(31, 5)  # grad values
        bw.put(0, 4)                 # grad boundary
    bw.put(0, 1)                     # cpe base channel 0
    bw.put(0, 1)                     # IS signs all +1
    bw.put(0, 1)                     # no band ext data
    if sf0p[1] is None:
        bw.put(1, 2); bw.put(3, 2)   # channel 0: mode 1 (CLC), len 5
        for s_ in sf0:
            bw.put(s_, 5)
    else:
        _, wi, base, ln, stored = sf0p
        bw.put(0, 2)                 # mode 0: VLC delta offset
        bw.put(wi, 3); bw.put(base, 5); bw.put(ln - 3, 2)
        bw.put(stored[0], ln)
        m = (1 << ln) - 1
        for i in range(1, len(stored)):
            code, cl = SF_UNSIGNED[ln][(stored[i] - stored[i - 1]) & m]
            bw.put(code, cl)
    for v, n in c0[1]:
        bw.put(v, n)
    bw.put(1, 2); bw.put(3, 2)       # channel 1: mode 2 (VLC distance to channel 0), len 5
    for s0, s1 in zip(sf0, sf1):
        code, ln = SF_SIGNED[5][s1 - s0]
        bw.put(code, ln)
    for v, n in c1[1]:
        bw.put(v, n)
    bw.align()


def encode_superframe(bw, Xs, band_count):
    """One gradient for all four frames, pooled over the 512-byte superframe."""
    setups = [frame_setup(X, band_count) for X in Xs]
    budget = SF_BYTES * 8

    def total(g):
        trials = [frame_trial(X, st, g) for X, st in zip(Xs, setups)]
        return sum(t[0] for t in trials), trials
    lo, hi = 0, 31
    if total(hi)[0] > budget:
        raise RuntimeError("superframe does not fit")
    while lo < hi:
        mid = (lo + hi) // 2
        if total(mid)[0] <= budget:
            hi = mid
        else:
            lo = mid + 1
    _, trials = total(lo)
    for fi, (st, tr) in enumerate(zip(setups, trials)):
        write_frame(bw, fi == 0, band_count, st, lo, tr[1], tr[2])
    return [lo * 100 + band_count] * len(Xs)


def config_bytes(band_frame_bytes=SF_BYTES // FRAMES_PER_SF):
    bw = BitWriter()
    bw.put(0xFE, 8)
    bw.put(SR_IDX, 4)
    bw.put(2, 3)            # stereo CPE
    bw.put(0, 1)
    bw.put(band_frame_bytes - 1, 11)
    bw.put(2, 2)            # superframe index -> 4 frames
    bw.put(0, 3)
    return bw.bytes()


def encode(pcm, out_path, band_count=12, loop=True, scale=None):
    """pcm: float array (n, 2) in [-1, 1], 48 kHz."""
    n = len(pcm)
    delay = NFR  # samples of encoder delay we report in 'fact'
    # pad: one frame of lead-in so the first real samples are fully reconstructed
    total_frames = int(np.ceil((n + NFR) / NFR))
    total_frames = int(np.ceil(total_frames / FRAMES_PER_SF)) * FRAMES_PER_SF
    padded = np.zeros(((total_frames + 1) * NFR, 2))
    padded[NFR:NFR + n] = pcm
    if loop:
        # wrap: the lead-in holds the end of the loop so the seam overlaps cleanly
        padded[:NFR] = pcm[-NFR:]
        rem = len(padded) - (NFR + n)
        if rem > 0:
            padded[NFR + n:] = pcm[:rem]
    s = scale if scale is not None else -256.0  # matches FFmpeg/Sony decoder gain and sign
    nsf = total_frames // FRAMES_PER_SF
    jobs = [(padded[i * FRAMES_PER_SF * NFR:(i + 1) * FRAMES_PER_SF * NFR + NFR], s, band_count)
            for i in range(nsf)]
    from multiprocessing import Pool
    with Pool() as pool:
        results = pool.map(_encode_superframe, jobs, chunksize=8)
    data = bytearray()
    gs = []
    for b, g in results:
        data += b
        gs += g
    write_riff(out_path, data, n, delay, loop)
    return np.array(gs)


def _encode_superframe(job):
    seg, s, band_count = job
    bw = BitWriter()
    Xs = []
    for fi in range(FRAMES_PER_SF):
        blk = seg[fi * NFR:fi * NFR + 2 * NFR]
        Xs.append([mdct(blk[:, c]) * s for c in range(2)])
    gs = encode_superframe(bw, Xs, band_count)
    b = bw.bytes()
    assert len(b) <= SF_BYTES, len(b)
    return b + bytes(SF_BYTES - len(b)), gs


def write_riff(path, data, nsamples, delay, loop):
    cfg = config_bytes()
    guid = bytes.fromhex("d242e147ba368d4d88fc61654f8c836c")
    fmt = struct.pack("<HHIIHHHHI", 0xFFFE, 2, 48000, SF_BYTES * 48000 // (NFR * FRAMES_PER_SF),
                      SF_BYTES, 0, 34, NFR * FRAMES_PER_SF, 3)
    fmt += guid + struct.pack("<I", 1) + cfg + bytes(4)
    fact = struct.pack("<III", nsamples, delay, delay)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    chunks += b"fact" + struct.pack("<I", len(fact)) + fact
    if loop:
        smpl = struct.pack("<IIIIIIIII", 0, 0, 0x5161, 60, 0, 0, 0, 1, 0x18)
        smpl += struct.pack("<IIIIII", 0, 0, delay, delay + nsamples - 1, 0, 0)
        chunks += b"smpl" + struct.pack("<I", len(smpl)) + smpl
    chunks += b"data" + struct.pack("<I", len(data)) + data
    riff = b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks
    open(path, "wb").write(riff)


def read_wav(path):
    w = wave.open(path)
    assert w.getframerate() == 48000 and w.getnchannels() == 2 and w.getsampwidth() == 2
    return np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2) / 32768.0


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    pcm = read_wav(src)
    gs = encode(pcm, dst)
    print(f"wrote {dst}: gradient min/mean/max {gs.min()}/{gs.mean():.1f}/{gs.max()}")
