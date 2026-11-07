"""Generates TurkishDPI's own icons (app + tray status variants) and logo.

    python tools/make_icons.py

Writes src/app/res/*.ico and assets/logo.png. Requires Pillow.
"""
import os
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "src", "app", "res")
ASSETS = os.path.join(ROOT, "assets")
S = 1024  # supersampled canvas

def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))

def shield_mask():
    m = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(m)
    # Shield: rounded top, pointed bottom.
    top, left, right, bottom = 70, 120, S - 120, S - 50
    pts = []
    import math
    # top edge with slight inward curve
    for i in range(41):
        x = left + (right - left) * i / 40
        y = top + 40 * math.sin(math.pi * i / 40) * 0.6
        pts.append((x, y))
    # right side down then curve to tip
    for i in range(1, 61):
        t = i / 60
        x = right - (right - S / 2) * (t ** 2.2)
        y = top + 20 + (bottom - top - 20) * t
        pts.append((x, y))
    for i in range(1, 60):
        t = 1 - i / 60
        x = left + (S / 2 - left) * (t ** 2.2)
        y = top + 20 + (bottom - top - 20) * t
        pts.append((x, y))
    d.polygon(pts, fill=255)
    return m.filter(ImageFilter.GaussianBlur(2))

def gradient(c1, c2):
    g = Image.new("RGBA", (S, S))
    px = g.load()
    for y in range(S):
        col = lerp(c1, c2, y / (S - 1))
        for x in range(S):
            px[x, y] = col + (255,)
    return g

def emblem():
    """White crescent embracing a forward chevron: traffic passing through."""
    e = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(e)
    cx, cy, r = S * 0.46, S * 0.47, S * 0.25
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    r2 = r * 0.80
    ox = r * 0.30
    d.ellipse((cx - r2 + ox, cy - r2, cx + r2 + ox, cy + r2), fill=0)
    # chevron
    w = S * 0.075
    x0, x1, y0 = S * 0.50, S * 0.70, cy
    h = S * 0.15
    d.line([(x0, y0 - h), (x1, y0), (x0, y0 + h)], fill=255, width=int(w), joint="curve")
    for (x, y) in [(x0, y0 - h), (x0, y0 + h), (x1, y0)]:
        d.ellipse((x - w / 2, y - w / 2, x + w / 2, y + w / 2), fill=255)
    return e

def build(dot=None):
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    mask = shield_mask()
    # soft shadow
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sh.putalpha(mask.point(lambda v: v * 0.45).filter(ImageFilter.GaussianBlur(18)))
    base.alpha_composite(sh, (0, 14))
    body = gradient((242, 38, 52), (140, 8, 24))
    body.putalpha(mask)
    base.alpha_composite(body)
    # glossy highlight on upper half
    hl = Image.new("RGBA", (S, S), (255, 255, 255, 0))
    hm = Image.new("L", (S, S), 0)
    ImageDraw.Draw(hm).ellipse((-S * 0.2, -S * 0.75, S * 1.2, S * 0.42), fill=55)
    hm = Image.composite(hm, Image.new("L", (S, S), 0), mask)
    hl.putalpha(hm)
    base.alpha_composite(hl)
    white = Image.new("RGBA", (S, S), (255, 255, 255, 255))
    white.putalpha(emblem())
    base.alpha_composite(white)
    if dot:
        r = S * 0.19
        cx, cy = S - r - 10, S - r - 10
        d = ImageDraw.Draw(base)
        d.ellipse((cx - r - 34, cy - r - 34, cx + r + 34, cy + r + 34), fill=(255, 255, 255, 255))
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=dot + (255,))
    return base

def save_ico(img, path, sizes=(16, 20, 24, 32, 40, 48, 64, 128, 256)):
    img.save(path, format="ICO", sizes=[(s, s) for s in sizes])

def main():
    os.makedirs(RES, exist_ok=True)
    os.makedirs(ASSETS, exist_ok=True)
    app = build()
    save_ico(app, os.path.join(RES, "app.ico"))
    app.resize((512, 512), Image.LANCZOS).save(os.path.join(ASSETS, "logo.png"))
    variants = {
        "tray_active": (34, 197, 94),
        "tray_scan": (250, 176, 5),
        "tray_paused": (148, 163, 184),
        "tray_error": (30, 41, 59),
    }
    for name, col in variants.items():
        save_ico(build(col), os.path.join(RES, name + ".ico"), sizes=(16, 20, 24, 32, 40, 48, 64))
    print("icons written to", RES)

if __name__ == "__main__":
    main()
