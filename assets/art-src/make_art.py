#!/usr/bin/env python3
"""Original launcher art for the PS5 Source port.

Half-Life / Source *mood* (amber on graphite, industrial haze), but every
shape is original: no lambda, no Valve marks or wordmarks.
Outputs: icon0.png (512), selection.png and launch.png (3840x2160).
"""
import math, random, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else "."
import os
FONTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts")  # Barlow Condensed (OFL), from google/fonts
FONT_B = os.path.join(FONTS, "BarlowCondensed-SemiBold.ttf")
FONT_M = os.path.join(FONTS, "BarlowCondensed-Medium.ttf")
FONT_L = os.path.join(FONTS, "BarlowCondensed-Light.ttf")
AMBER = (242, 140, 40)
AMBER_HI = (255, 186, 92)
rng = np.random.default_rng(7)
random.seed(7)


# ---------------------------------------------------------------- emblem
def emblem(size, ss=4, color=AMBER, glow=True):
    """Ring + two interlocking angular brackets (a 'port' / pass-through)."""
    S = size * ss
    im = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(im)
    c = S / 2
    R = S * 0.46
    w = S * 0.075
    d.ellipse([c - R, c - R, c + R, c + R], fill=255)
    d.ellipse([c - R + w, c - R + w, c + R - w, c + R - w], fill=0)
    # inner thin ring
    r2 = R - w * 1.7
    w2 = S * 0.012
    d.ellipse([c - r2, c - r2, c + r2, c + r2], fill=255)
    d.ellipse([c - r2 + w2, c - r2 + w2, c + r2 - w2, c + r2 - w2], fill=0)
    # glyph: two offset brackets, rotated 45 deg, interlocking
    g = Image.new("L", (S, S), 0)
    gd = ImageDraw.Draw(g)
    t = S * 0.085          # stroke
    a = S * 0.30           # arm length
    off = S * 0.075
    # bracket 1 (upper-left)
    x0, y0 = c - off - a / 2, c - off - a / 2
    gd.rectangle([x0, y0, x0 + a, y0 + t], fill=255)
    gd.rectangle([x0, y0, x0 + t, y0 + a], fill=255)
    # bracket 2 (lower-right)
    x1, y1 = c + off + a / 2, c + off + a / 2
    gd.rectangle([x1 - a, y1 - t, x1, y1], fill=255)
    gd.rectangle([x1 - t, y1 - a, x1, y1], fill=255)
    # connecting bar through the middle
    gd.polygon([(c - S * 0.03, c - S * 0.16), (c + S * 0.03, c - S * 0.16),
                (c + S * 0.03, c + S * 0.16), (c - S * 0.03, c + S * 0.16)], fill=255)
    g = g.rotate(45, resample=Image.BICUBIC, center=(c, c))
    im = Image.fromarray(np.maximum(np.array(im), np.array(g)))
    # notch cuts in the outer ring (industrial detail)
    d = ImageDraw.Draw(im)
    for ang in (90, 270):
        rad = math.radians(ang)
        cx, cy = c + math.cos(rad) * (R - w / 2), c + math.sin(rad) * (R - w / 2)
        d.rectangle([cx - S * 0.018, cy - w, cx + S * 0.018, cy + w], fill=0)
    mask = im.resize((size, size), Image.LANCZOS)
    rgba = Image.new("RGBA", (size, size), color + (0,))
    rgba.putalpha(mask)
    if glow:
        gl = mask.filter(ImageFilter.GaussianBlur(size * 0.03))
        glow_img = Image.new("RGBA", (size, size), AMBER_HI + (0,))
        glow_img.putalpha(gl.point(lambda v: int(v * 0.55)))
        base = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        base.alpha_composite(glow_img)
        base.alpha_composite(rgba)
        return base
    return rgba


def text_spaced(draw, xy, text, font, fill, spacing):
    x, y = xy
    for ch in text:
        draw.text((x, y), ch, font=font, fill=fill)
        x += draw.textlength(ch, font=font) + spacing
    return x


def text_width(draw, text, font, spacing):
    return sum(draw.textlength(ch, font=font) for ch in text) + spacing * (len(text) - 1)


# ---------------------------------------------------------------- scene
def scene(W, H, emblem_pos, emblem_size, title=True):
    y = np.linspace(0, 1, H)[:, None]
    x = np.linspace(0, 1, W)[None, :]
    # graphite sky with a cold teal top and warm haze near the horizon
    top = np.array([14, 20, 24], float)
    mid = np.array([30, 36, 38], float)
    hz = np.array([72, 52, 34], float)
    t = y
    sky = (top * (1 - t) + mid * t)[..., None].squeeze()
    img = np.zeros((H, W, 3), float)
    for k in range(3):
        img[..., k] = top[k] * (1 - t) + mid[k] * t
    horizon = 0.70
    band = np.exp(-((y - horizon) / 0.09) ** 2)
    for k in range(3):
        img[..., k] += (hz[k] - mid[k]) * band * 0.9
    # warm glow behind the emblem
    ex, ey = emblem_pos[0] / W, emblem_pos[1] / H
    d2 = ((x - ex) * W / H) ** 2 + (y - ey) ** 2
    glow = np.exp(-d2 / 0.09)
    img += glow[..., None] * np.array([70, 38, 10], float)

    # drifting fog (low-frequency noise)
    def fog(scale, amp, seed):
        r = np.random.default_rng(seed)
        small = r.random((max(2, H // scale), max(2, W // scale)))
        f = Image.fromarray((small * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC)
        f = f.filter(ImageFilter.GaussianBlur(scale / 2))
        return (np.asarray(f, float) / 255 - 0.5) * amp
    img += fog(220, 26, 1)[..., None] * np.array([1.0, 1.0, 0.95])
    img += fog(90, 10, 2)[..., None]

    base = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).convert("RGBA")

    # distant city: three parallax silhouette layers with haze
    def skyline(layer_y, hmin, hmax, wmin, wmax, color, alpha, seed, antennas=0.15, lights=0.0):
        r = random.Random(seed)
        lay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        dd = ImageDraw.Draw(lay)
        xx = -50
        while xx < W + 50:
            bw = r.randint(wmin, wmax)
            bh = r.randint(hmin, hmax)
            top_y = layer_y - bh
            dd.rectangle([xx, top_y, xx + bw, H], fill=color + (alpha,))
            if r.random() < 0.25:  # stepped roof
                sw = int(bw * r.uniform(0.3, 0.6))
                sh = r.randint(10, max(11, bh // 5))
                sx = xx + r.randint(0, bw - sw)
                dd.rectangle([sx, top_y - sh, sx + sw, top_y], fill=color + (alpha,))
            if r.random() < antennas:
                ax = xx + r.randint(4, max(5, bw - 4))
                ah = r.randint(30, 140)
                dd.rectangle([ax, top_y - ah, ax + 3, top_y], fill=color + (alpha,))
            if lights:
                for _ in range(int(bw * bh * lights / 4000)):
                    lx = xx + r.randint(2, max(3, bw - 6))
                    ly = top_y + r.randint(8, max(9, bh - 8))
                    if ly < H:
                        dd.rectangle([lx, ly, lx + 4, ly + 3], fill=(255, 170, 80, r.randint(40, 120)))
            xx += bw + r.randint(0, 18)
        return lay
    hy = int(H * horizon)
    base.alpha_composite(skyline(hy + 20, 30, 170, 30, 110, (52, 48, 44), 170, 11, 0.25, 0.01))
    base.alpha_composite(skyline(hy + 150, 60, 300, 50, 170, (30, 30, 31), 225, 12, 0.2, 0.015))
    base.alpha_composite(skyline(int(H * 0.98), 60, 260, 90, 260, (11, 11, 12), 255, 13, 0.12, 0.006))

    # one tall structure on the far left-centre, lost in haze (original shape)
    tower = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    td = ImageDraw.Draw(tower)
    tx = int(W * 0.30)
    tw = int(W * 0.022)
    td.polygon([(tx - tw, hy + 120), (tx - tw * 0.55, int(H * 0.16)), (tx + tw * 0.55, int(H * 0.16)),
                (tx + tw, hy + 120)], fill=(40, 42, 44, 120))
    td.rectangle([tx - 2, int(H * 0.08), tx + 2, int(H * 0.16)], fill=(40, 42, 44, 120))
    tower = tower.filter(ImageFilter.GaussianBlur(3))
    base.alpha_composite(tower)
    # beacon on the tower
    bl = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(bl).ellipse([tx - 14, int(H * 0.08) - 14, tx + 14, int(H * 0.08) + 14], fill=(255, 120, 60, 200))
    base.alpha_composite(bl.filter(ImageFilter.GaussianBlur(10)))

    # foreground haze band over the skyline
    haze = np.zeros((H, W, 4), np.uint8)
    hb = np.exp(-((y - (horizon + 0.12)) / 0.10) ** 2)[:, 0]
    haze[..., 0], haze[..., 1], haze[..., 2] = 60, 54, 48
    haze[..., 3] = (hb * 150).astype(np.uint8)[:, None]
    base.alpha_composite(Image.fromarray(haze))

    # emblem
    em = emblem(emblem_size)
    base.alpha_composite(em, (int(emblem_pos[0] - emblem_size / 2), int(emblem_pos[1] - emblem_size / 2)))

    if title:
        d = ImageDraw.Draw(base)
        f1 = ImageFont.truetype(FONT_B, int(emblem_size * 0.30))
        f2 = ImageFont.truetype(FONT_M, int(emblem_size * 0.085))
        t1, t2 = "SOURCE PORT", "PS5  HOMEBREW  EDITION"
        s1, s2 = emblem_size * 0.03, emblem_size * 0.05
        w1 = text_width(d, t1, f1, s1)
        w2 = text_width(d, t2, f2, s2)
        cx = emblem_pos[0]
        ty = emblem_pos[1] + emblem_size * 0.58
        # soft shadow
        sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        sd = ImageDraw.Draw(sh)
        text_spaced(sd, (cx - w1 / 2 + 6, ty + 6), t1, f1, (0, 0, 0, 180), s1)
        base.alpha_composite(sh.filter(ImageFilter.GaussianBlur(8)))
        text_spaced(d, (cx - w1 / 2, ty), t1, f1, (236, 230, 220, 255), s1)
        ly = ty + emblem_size * 0.36
        d.rectangle([cx - w2 / 2, ly, cx + w2 / 2, ly + max(3, emblem_size // 160)], fill=AMBER + (255,))
        text_spaced(d, (cx - w2 / 2, ly + emblem_size * 0.03), t2, f2, AMBER + (255,), s2)

    # grain, scanlines, vignette
    arr = np.asarray(base.convert("RGB"), float)
    arr += rng.normal(0, 4.0, arr.shape[:2])[..., None]
    arr *= (1 - 0.035 * ((np.arange(H) % 4) < 1))[:, None, None]
    vx = (x - 0.5) * 2
    vy = (y - 0.5) * 2
    vig = 1 - 0.45 * np.clip(vx ** 2 * 0.6 + vy ** 2 * 0.8, 0, 1) ** 1.3
    arr *= vig[..., None]
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))


def icon(size=512):
    S = size
    y = np.linspace(0, 1, S)[:, None]
    x = np.linspace(0, 1, S)[None, :]
    r = np.sqrt((x - 0.5) ** 2 + (y - 0.42) ** 2)
    arr = np.zeros((S, S, 3), float)
    c0 = np.array([46, 40, 34]); c1 = np.array([12, 13, 15])
    t = np.clip(r / 0.75, 0, 1)[..., None]
    arr = c0 * (1 - t) + c1 * t
    arr += rng.normal(0, 3, (S, S))[..., None]
    im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).convert("RGBA")
    em = emblem(int(S * 0.62))
    im.alpha_composite(em, (int(S * 0.19), int(S * 0.08)))
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(FONT_B, int(S * 0.15))
    t1, sp = "SOURCE PORT", S * 0.008
    w = text_width(d, t1, f, sp)
    text_spaced(d, ((S - w) / 2, S * 0.72), t1, f, (236, 230, 220, 255), sp)
    d.rectangle([S * 0.2, S * 0.905, S * 0.8, S * 0.915], fill=AMBER + (255,))
    # thin frame
    d.rectangle([6, 6, S - 7, S - 7], outline=(242, 140, 40, 90), width=3)
    return im.convert("RGB")


if __name__ == "__main__":
    icon().save(f"{OUT}/icon0.png")
    W, H = 3840, 2160
    # selection: subject on the right, left kept calm for the Shell title/Play button
    scene(W, H, (int(W * 0.73), int(H * 0.48)), 760).save(f"{OUT}/selection.png")
    # launch: centred
    scene(W, H, (W // 2, int(H * 0.36)), 820).save(f"{OUT}/launch.png")
    print("done")
