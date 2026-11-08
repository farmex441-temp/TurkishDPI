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

def mark(kind):
    """Bold white sign used instead of the emblem on warn/error tray icons,
    so those states differ by shape too, not only by colour."""
    e = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(e)
    w = int(S * 0.11)
    cx, cy = S * 0.5, S * 0.47
    if kind == "cross":
        h = S * 0.17
        d.line([(cx - h, cy - h), (cx + h, cy + h)], fill=255, width=w)
        d.line([(cx + h, cy - h), (cx - h, cy + h)], fill=255, width=w)
        for (x, y) in [(cx - h, cy - h), (cx + h, cy + h), (cx + h, cy - h), (cx - h, cy + h)]:
            d.ellipse((x - w / 2, y - w / 2, x + w / 2, y + w / 2), fill=255)
    else:  # "bang"
        d.line([(cx, cy - S * 0.22), (cx, cy + S * 0.06)], fill=255, width=w)
        for y in (cy - S * 0.22, cy + S * 0.06):
            d.ellipse((cx - w / 2, y - w / 2, cx + w / 2, y + w / 2), fill=255)
        r = w * 0.62
        y = cy + S * 0.22
        d.ellipse((cx - r, y - r, cx + r, y + r), fill=255)
    return e

def build(color=None, sign=None):
    """Flat shield (no gloss or shadow: crisper at 16 px). color=None is the
    brand red with a gradient (app icon); tray icons fill the whole shield
    with the state colour."""
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    mask = shield_mask()
    if color is None:
        body = gradient((236, 40, 56), (176, 18, 36))
    else:
        body = Image.new("RGBA", (S, S), color + (255,))
    body.putalpha(mask)
    base.alpha_composite(body)
    white = Image.new("RGBA", (S, S), (255, 255, 255, 255))
    white.putalpha(mark(sign) if sign else emblem())
    base.alpha_composite(white)
    return base

def save_ico(img, path, sizes=(16, 20, 24, 32, 40, 48, 64, 96, 128)):
    # Plain (uncompressed) bitmaps: compressed PNG frames make the resource
    # section look like packed/encrypted data to antivirus heuristics.
    img.save(path, format="ICO", sizes=[(s, s) for s in sizes], bitmap_format="bmp")

def main():
    os.makedirs(RES, exist_ok=True)
    os.makedirs(ASSETS, exist_ok=True)
    app = build()
    save_ico(app, os.path.join(RES, "app.ico"))
    app.resize((512, 512), Image.LANCZOS).save(os.path.join(ASSETS, "logo.png"))
    # Tray: the shield itself carries the state (docs/design/DESIGN-SYSTEM.md
    # §10, T1). Same hues as the window's state colours, one step deeper so
    # the white mark stays readable on light and dark taskbars.
    variants = {
        "tray_active": ((22, 163, 74), None),     # ok
        "tray_scan": ((37, 99, 235), None),       # busy
        "tray_paused": ((100, 116, 139), None),   # idle: standby, paused, offline
        "tray_error": ((220, 38, 38), "cross"),   # error
        "tray_warn": ((217, 119, 6), "bang"),     # partial
    }
    for name, (col, sign) in variants.items():
        save_ico(build(col, sign), os.path.join(RES, name + ".ico"), sizes=(16, 20, 24, 32, 40, 48, 64))
    print("icons written to", RES)

if __name__ == "__main__":
    main()
