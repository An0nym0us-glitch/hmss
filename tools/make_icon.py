#!/usr/bin/env python3
"""Generates the AudioBridge application icon (PNG + multi-resolution ICO).

Design: rounded square with a blue-to-violet gradient and a white "bridge" made of
audio level bars, with two endpoint dots (input -> output).

Usage: python3 tools/make_icon.py  (writes into src/app/resources/)
Requires Pillow.
"""
import os
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(__file__), "..", "src", "app", "resources")
S = 1024  # supersampled master


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def master():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # Diagonal gradient background.
    grad = Image.new("RGBA", (S, S))
    c0, c1 = (44, 120, 255, 255), (139, 92, 255, 255)
    px = grad.load()
    for y in range(S):
        for x in range(S):
            t = (x + y) / (2 * S)
            px[x, y] = lerp(c0, c1, t)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([40, 40, S - 40, S - 40], radius=230, fill=255)
    img.paste(grad, (0, 0), mask)

    # Soft top highlight.
    hl = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(hl).ellipse([-200, -620, S + 200, 430], fill=(255, 255, 255, 38))
    hl_mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(hl_mask).rounded_rectangle([40, 40, S - 40, S - 40], radius=230, fill=255)
    img = Image.alpha_composite(img, Image.composite(hl, Image.new("RGBA", (S, S)), hl_mask))

    d = ImageDraw.Draw(img)
    white = (255, 255, 255, 255)
    # Level bars forming an arch (the "bridge").
    heights = [150, 250, 340, 400, 340, 250, 150]
    bar_w, gap = 62, 34
    total = len(heights) * bar_w + (len(heights) - 1) * gap
    x0 = (S - total) // 2
    base = 640
    for i, h in enumerate(heights):
        x = x0 + i * (bar_w + gap)
        d.rounded_rectangle([x, base - h, x + bar_w, base], radius=bar_w // 2, fill=white)
    # Deck line and endpoints (input -> output).
    d.rounded_rectangle([190, 700, S - 190, 744], radius=22, fill=white)
    for cx in (190, S - 190):
        d.ellipse([cx - 62, 722 - 62, cx + 62, 722 + 62], fill=white)
        d.ellipse([cx - 30, 722 - 30, cx + 30, 722 + 30], fill=(92, 106, 255, 255))
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    m = master()
    png = m.resize((256, 256), Image.LANCZOS)
    png.save(os.path.join(OUT, "AudioBridge.png"))
    m.resize((512, 512), Image.LANCZOS).save(os.path.join(OUT, "AudioBridge-512.png"))
    sizes = [(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (128, 128), (256, 256)]
    m.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT, "AudioBridge.ico"), sizes=sizes)
    print("icons written to", os.path.abspath(OUT))


if __name__ == "__main__":
    main()
