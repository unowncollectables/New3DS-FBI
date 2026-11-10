#!/usr/bin/env python3
"""Generate New3DS FBI icon, banner and in-app logo.

Original art drawn with PIL shapes + an OFL font (Be Vietnam Pro).
No Nintendo or other third-party artwork is used.
Run:  python3 tools/make_art.py   (from the repo root)
"""
import os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

FONT_DIR = "/usr/share/fonts/truetype/sand-box/google/Be Vietnam Pro"
def font(weight, size):
    for name in (f"BeVietnamPro-{weight}.ttf", "BeVietnamPro-Bold.ttf"):
        p = os.path.join(FONT_DIR, name)
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", size)

TOP = (33, 150, 243)      # bright blue
BOT = (13, 71, 161)       # deep blue
GOLD = (255, 193, 7)
WHITE = (255, 255, 255)
NAVY = (10, 25, 60)

def vgrad(w, h, c1, c2):
    g = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(g)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=tuple(int(c1[i] + (c2[i] - c1[i]) * t) for i in range(3)))
    return g

def tile(size, with_text=True):
    """Rounded app tile: gradient, install arrow into a tray, 'FBI' label."""
    S = 8  # supersample
    W = size * S
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    r = int(W * 0.22)
    mask = Image.new("L", (W, W), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, W - 1, W - 1], r, fill=255)
    img.paste(vgrad(W, W, TOP, BOT), (0, 0), mask)
    d = ImageDraw.Draw(img)
    # soft top gloss
    gloss = Image.new("L", (W, W), 0)
    ImageDraw.Draw(gloss).rounded_rectangle([int(W*0.04), int(W*0.03), int(W*0.96), int(W*0.45)], int(r*0.9), fill=40)
    img.paste(Image.new("RGBA", (W, W), WHITE + (255,)), (0, 0), Image.composite(gloss, Image.new("L", (W, W), 0), mask))
    # inner border
    d.rounded_rectangle([int(W*0.03), int(W*0.03), W - 1 - int(W*0.03), W - 1 - int(W*0.03)], int(r*0.85), outline=(255, 255, 255, 70), width=max(1, int(W*0.025)))

    cx = W / 2
    top_area = 0.64 if with_text else 0.86
    # download arrow
    aw = W * 0.16          # shaft half-width/2
    ay0 = W * 0.13
    ay1 = W * (0.40 if with_text else 0.52)
    head = W * (0.17 if with_text else 0.22)
    d.rounded_rectangle([cx - aw/2, ay0, cx + aw/2, ay1], int(aw*0.35), fill=WHITE)
    d.polygon([(cx - head*1.15, ay1 - head*0.35), (cx + head*1.15, ay1 - head*0.35), (cx, ay1 + head*0.8)], fill=WHITE)
    # tray
    ty = W * (top_area - 0.06)
    tw = W * 0.30
    th = W * 0.065
    d.rounded_rectangle([cx - tw, ty, cx + tw, ty + th], int(th/2), fill=GOLD)
    d.rounded_rectangle([cx - tw, ty - W*0.12, cx - tw + th, ty + th], int(th/2), fill=GOLD)
    d.rounded_rectangle([cx + tw - th, ty - W*0.12, cx + tw, ty + th], int(th/2), fill=GOLD)

    if with_text:
        f = font("ExtraBold", int(W * 0.25))
        txt = "FBI"
        b = d.textbbox((0, 0), txt, font=f)
        tx = cx - (b[2] - b[0]) / 2 - b[0]
        tyy = W * 0.66 - b[1]
        d.text((tx + W*0.01, tyy + W*0.012), txt, font=f, fill=(0, 0, 0, 90))
        d.text((tx, tyy), txt, font=f, fill=WHITE)
    return img.resize((size, size), Image.LANCZOS)

def banner():
    S = 4
    W, H = 256 * S, 128 * S
    img = vgrad(W, H, (21, 101, 192), (8, 30, 72)).convert("RGBA")
    d = ImageDraw.Draw(img)
    # diagonal light streaks (subtle)
    streak = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(streak)
    for i, a in ((0, 22), (1, 14)):
        x = W * (0.55 + i * 0.18)
        sd.polygon([(x, 0), (x + W*0.10, 0), (x - W*0.25, H), (x - W*0.35, H)], fill=(255, 255, 255, a))
    img = Image.alpha_composite(img, streak)
    d = ImageDraw.Draw(img)
    # gold accent line at bottom
    d.rectangle([0, H - 5*S, W, H - 3*S], fill=GOLD + (255,))
    # tile on the left with drop shadow
    t = tile(80).resize((80 * S, 80 * S), Image.LANCZOS)
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([14*S, 26*S, 94*S, 106*S], 18*S, fill=(0, 0, 0, 120))
    sh = sh.filter(ImageFilter.GaussianBlur(5 * S))
    img = Image.alpha_composite(img, sh)
    img.alpha_composite(t, (12 * S, 22 * S))
    d = ImageDraw.Draw(img)
    # title text
    x0 = 104 * S
    f1 = font("ExtraBold", 30 * S)
    f1b = font("ExtraBold", 30 * S)
    d.text((x0, 30 * S), "New3DS", font=f1, fill=WHITE)
    b = d.textbbox((x0, 30 * S), "New3DS", font=f1)
    d.text((x0, 30 * S + (b[3] - b[1]) + 4 * S), "FBI", font=f1b, fill=GOLD)
    f2 = font("SemiBold", 10 * S)
    d.text((x0 + 1*S, 98 * S), "Based on FBI by Steveice10", font=f2, fill=(220, 232, 255))
    out = img.resize((256, 128), Image.LANCZOS).convert("RGB")
    return out

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    icon = tile(48)
    bg = Image.new("RGB", (48, 48), BOT)
    # Home Menu shows the icon square; fill the rounded corners with deep blue.
    bg.paste(icon, (0, 0), icon)
    bg.save(os.path.join(root, "meta", "icon_3ds.png"))
    banner().save(os.path.join(root, "meta", "banner_3ds.png"))
    # In-app logo (top screen of main menu), transparent corners.
    tile(64).save(os.path.join(root, "romfs", "logo.png"))

if __name__ == "__main__":
    main()
