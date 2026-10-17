"""Draws the geow2 icon (original artwork) and writes it in every format.

    python tools/make_icon.py [out-dir]      (default: res/)

A glowing grid, pulled in by a gravity well at the centre, behind a "G2"
monogram in glowing vector strokes, on a dark rounded square. Everything
is drawn here, deterministically; nothing comes from the game.

Writes icon-1024.png, geow2.ico (16-256), geow2.icns and
linux/geow2-<n>.png (16-512).
"""

import io
import math
import os
import struct
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

GRID = (40, 110, 255)
GLOW = (255, 40, 200)
CORE = (255, 235, 250)


def warp(x, y, size, strength):
    # A well at the centre pulls the grid in, most strongly near it.
    cx = cy = size / 2
    dx, dy = x - cx, y - cy
    r = math.hypot(dx, dy)
    if r < 1e-6:
        return x, y
    s = size * 0.28
    pull = strength * size * math.exp(-(r * r) / (s * s)) * min(1.0, r / (size * 0.08))
    return x - dx / r * pull, y - dy / r * pull


def glow(layer, colour, radii, gains):
    # Bloom: the layer blurred at several radii, tinted and added up.
    out = Image.new("RGB", layer.size, (0, 0, 0))
    tint = Image.new("RGB", layer.size, colour)
    for r, g in zip(radii, gains):
        b = layer.filter(ImageFilter.GaussianBlur(r)).point(lambda v, g=g: min(255, int(v * g)))
        out = ImageChops.add(out, ImageChops.multiply(tint, Image.merge("RGB", (b, b, b))))
    return out


def arc(cx, cy, r, a0, a1, n=96):
    # Points along a circle from angle a0 to a1 (degrees, y down).
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def monogram(size, width):
    # "G2" as strokes on a mask the size of the canvas.
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    u = size / 1024
    # G: from the upper right round the left to the right middle, then the
    # bar in towards the centre.
    gx, gy, gr = 350 * u, 512 * u, 190 * u
    g = arc(gx, gy, gr, -40, -360)
    g += [(gx + 30 * u, gy)]
    d.line(g, fill=255, width=width, joint="curve")
    # 2: a hook, a diagonal and the base, as tall as the G.
    tx, ty, tr = 695 * u, 447 * u, 125 * u
    two = arc(tx, ty, tr, 200, 360 + 25)
    two += [(570 * u, 702 * u), (845 * u, 702 * u)]
    d.line(two, fill=255, width=width, joint="curve")
    for p in (g[0], g[-1], two[0], two[-1]):
        d.ellipse([p[0] - width / 2, p[1] - width / 2, p[0] + width / 2, p[1] + width / 2], fill=255)
    return m


def render(size, detail):
    # Drawn at 4x and reduced, for smooth lines at every size.
    S = size * 4
    u = S / 1024
    grid = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(grid)
    lines = 14 if detail else 7
    lw = max(1, int((5 if detail else 11) * u))
    for i in range(lines + 1):
        t = i / lines * S
        for horizontal in (True, False):
            pts = []
            for k in range(161):
                s = k / 160 * S
                x, y = (s, t) if horizontal else (t, s)
                pts.append(warp(x, y, S, 0.13))
            d.line(pts, fill=255, width=lw, joint="curve")
    grid_rgb = glow(grid, GRID, [2 * u, 10 * u, 30 * u], [1.0, 0.9, 0.7])
    grid_rgb = ImageChops.add(grid_rgb, Image.merge("RGB", (grid.point(lambda v: v // 6),) * 3))
    stroke = int((44 if detail else 70) * u)
    mono = monogram(S, stroke)
    core = monogram(S, max(1, stroke // 3))
    mono_rgb = glow(mono, GLOW, [4 * u, 18 * u, 48 * u, 110 * u], [1.0, 1.1, 1.0, 0.8])
    mono_rgb = ImageChops.add(mono_rgb, ImageChops.multiply(Image.new("RGB", (S, S), CORE), Image.merge("RGB", (core,) * 3)))
    # The grid dims behind the letters so they read at small sizes.
    shade = mono.filter(ImageFilter.GaussianBlur(30 * u)).point(lambda v: 255 - min(255, v * 2))
    grid_rgb = ImageChops.multiply(grid_rgb, Image.merge("RGB", (shade,) * 3))
    art = ImageChops.add(ImageChops.add(Image.new("RGB", (S, S), (4, 4, 18)), grid_rgb), mono_rgb)
    # A rounded square with a margin, as desktop icons are drawn.
    margin, radius = 0.06 * S, 0.2 * S
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([margin, margin, S - margin, S - margin], radius, fill=255)
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(art, (0, 0), mask)
    return out.resize((size, size), Image.LANCZOS)


def png_bytes(img):
    b = io.BytesIO()
    img.save(b, "PNG", optimize=True)
    return b.getvalue()


def write_icns(path, images):
    # ICNS with PNG entries: ic07 128, ic08 256, ic09 512, ic11 32 (16@2x),
    # ic12 64 (32@2x), ic13 256 (128@2x), ic14 512 (256@2x). (No ic10, the
    # 1024 one, which would double the file.)
    types = [("ic11", 32), ("ic12", 64), ("ic07", 128), ("ic13", 256), ("ic08", 256),
             ("ic14", 512), ("ic09", 512)]
    body = b""
    for t, n in types:
        data = png_bytes(images[n])
        body += t.encode() + struct.pack(">I", 8 + len(data)) + data
    with open(path, "wb") as f:
        f.write(b"icns" + struct.pack(">I", 8 + len(body)) + body)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "res")
    os.makedirs(os.path.join(out, "linux"), exist_ok=True)
    sizes = [16, 24, 32, 48, 64, 128, 256, 512, 1024]
    images = {n: render(n, detail=n >= 64) for n in sizes}
    with open(os.path.join(out, "icon-1024.png"), "wb") as f:
        f.write(png_bytes(images[1024]))
    ico = [images[n] for n in (256, 128, 64, 48, 32, 24, 16)]
    ico[0].save(os.path.join(out, "geow2.ico"), format="ICO", sizes=[(i.width, i.height) for i in ico],
                append_images=ico[1:])
    write_icns(os.path.join(out, "geow2.icns"), images)
    for n in (16, 32, 48, 64, 128, 256, 512):
        with open(os.path.join(out, "linux", "geow2-%d.png" % n), "wb") as f:
            f.write(png_bytes(images[n]))
    print("icons written to", os.path.normpath(out))


if __name__ == "__main__":
    main()
