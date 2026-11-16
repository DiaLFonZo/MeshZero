#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Draws the MeshZero icon: four mesh nodes whose strongest links form a Z.

    pip install pillow
    tools/gen_icon.py assets
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

BG = (14, 22, 32, 255)
LINK_DIM = (52, 78, 104, 255)
GREEN = (103, 234, 148, 255)
NODE_FILL = (11, 15, 20, 255)


def draw(size):
    scale = 4  # supersample for smooth edges
    s = size * scale
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=int(s * 0.22), fill=BG)

    def p(x, y):
        return (int(x * s), int(y * s))

    tl, tr, bl, br = p(0.27, 0.29), p(0.73, 0.29), p(0.27, 0.71), p(0.73, 0.71)
    thin, thick = max(1, int(s * 0.022)), int(s * 0.075)
    for a, b in ((tl, bl), (tr, br), (tl, br)):
        d.line([a, b], fill=LINK_DIM, width=thin)
    for a, b in ((tl, tr), (tr, bl), (bl, br)):
        d.line([a, b], fill=GREEN, width=thick)
    r_out, r_in = int(s * 0.105), int(s * 0.055)
    for c in (tl, tr, bl, br):
        d.ellipse([c[0] - r_out, c[1] - r_out, c[0] + r_out, c[1] + r_out], fill=GREEN)
        d.ellipse([c[0] - r_in, c[1] - r_in, c[0] + r_in, c[1] + r_in], fill=NODE_FILL)
    return img.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "assets")
    out.mkdir(parents=True, exist_ok=True)
    draw(256).save(out / "icon.png")
    draw(80).save(out / "meshzero.png")
    print("wrote", out / "icon.png", "and", out / "meshzero.png")
