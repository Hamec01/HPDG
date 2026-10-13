"""Item images for Kickstarter's reward builder (3:2; Kickstarter asks for 348x232, made at 2x).

usage: python tools/kickstarter/make_items.py   -> docs/kickstarter/images/items/item_XX.png
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "demo_reel"))
import effects  # noqa: E402

OUT = ROOT / "docs" / "kickstarter" / "images" / "items"
W, H = 696, 464
BG = (14, 15, 18)

ITEMS = [
    ("01", "CLOSED BETA", "Windows · VST3", "screen"),
    ("02", "BACKER LIST", "Your name with us", "list"),
    ("03", "HPDG 1.0", "Full license by email", "key"),
    ("04", "ALL 1.x UPDATES", "Free, as they ship", "update"),
    ("05", "INSIDER", "Early builds + private channel", "chat"),
    ("06", "VOTE", "Choose the next genre", "vote"),
    ("07", "CREDITS", "Your name in the plugin", "list"),
    ("08", "FRIEND LICENSE", "HPDG 1.0 for a friend", "key"),
    ("09", "FOUNDING SUPPORTER", "A credit line of your own", "star"),
    ("10", "CUSTOM KIT", "Sounds made with you", "wave"),
]


def icon(d, kind, cx, cy, s, color):
    w = max(4, s // 10)
    if kind == "screen":
        d.rounded_rectangle([cx - s, cy - s * .65, cx + s, cy + s * .55], radius=10, outline=color, width=w)
        for i in range(8):
            x = cx - s * .78 + i * s * .22
            h = s * (.15 + .35 * ((i * 5) % 7) / 6)
            d.rectangle([x, cy + s * .3 - h, x + s * .12, cy + s * .3], fill=color)
        d.line([cx - s * .4, cy + s * .8, cx + s * .4, cy + s * .8], fill=color, width=w)
    elif kind == "list":
        for i in range(4):
            y = cy - s * .6 + i * s * .4
            d.ellipse([cx - s, y - s * .1, cx - s * .8, y + s * .1], fill=color)
            d.line([cx - s * .6, y, cx + s * (1 if i % 2 == 0 else .6), y], fill=color, width=w)
    elif kind == "key":
        r = s * .45
        d.ellipse([cx - s, cy - r, cx - s + 2 * r, cy + r], outline=color, width=w)
        d.line([cx - s + 2 * r, cy, cx + s, cy], fill=color, width=w)
        d.line([cx + s * .55, cy, cx + s * .55, cy + s * .35], fill=color, width=w)
        d.line([cx + s * .85, cy, cx + s * .85, cy + s * .45], fill=color, width=w)
    elif kind == "update":
        d.arc([cx - s, cy - s, cx + s, cy + s], 30, 320, fill=color, width=w)
        d.polygon([(cx + s * .95, cy - s * .55), (cx + s * .95, cy + s * .05), (cx + s * .45, cy - s * .25)], fill=color)
        d.line([cx, cy - s * .45, cx, cy + s * .1], fill=color, width=w)
        d.line([cx, cy + s * .1, cx + s * .35, cy + s * .3], fill=color, width=w)
    elif kind == "chat":
        d.rounded_rectangle([cx - s, cy - s * .7, cx + s * .5, cy + s * .2], radius=12, outline=color, width=w)
        d.rounded_rectangle([cx - s * .4, cy - s * .1, cx + s, cy + s * .8], radius=12, fill=BG, outline=color, width=w)
        for i in range(3):
            d.ellipse([cx - s * .1 + i * s * .3 - 6, cy + s * .35 - 6, cx - s * .1 + i * s * .3 + 6, cy + s * .35 + 6], fill=color)
    elif kind == "vote":
        d.rounded_rectangle([cx - s, cy - s * .2, cx + s, cy + s * .8], radius=8, outline=color, width=w)
        d.line([cx - s * .5, cy - s * .2, cx + s * .5, cy - s * .2], fill=BG, width=w + 2)
        d.line([cx - s * .35, cy - s * .55, cx - s * .05, cy - s * .2], fill=color, width=w)
        d.line([cx - s * .05, cy - s * .2, cx + s * .55, cy - s * .95], fill=color, width=w)
    elif kind == "star":
        import math
        pts = []
        for i in range(10):
            a = -math.pi / 2 + i * math.pi / 5
            r = s if i % 2 == 0 else s * .42
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
        d.line(pts + [pts[0], pts[1]], fill=color, width=w, joint="curve")
    elif kind == "wave":
        for i in range(13):
            x = cx - s + i * s / 6
            h = s * (.2 + .8 * abs(((i * 7) % 11) - 5) / 5)
            d.line([x, cy - h, x, cy + h], fill=color, width=w)


def make(num, title, sub, kind):
    img = Image.new("RGBA", (W, H), BG + (255,))
    effects.paste_center(img, effects.logo(560, (255, 255, 255)), W * .82, H * .55, 0.035)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W, 8], fill=effects.ACCENT)
    # icon with a soft red glow
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    icon(ImageDraw.Draw(glow), kind, 180, 220, 92, effects.ACCENT + (255,))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(12)))
    icon(d, kind, 180, 220, 92, effects.INK)
    size = 52
    font_title = effects.font(size, bold=True)
    while d.textlength(title, font=font_title) > W - 322 - 30 and size > 24:  # fit the card width
        size -= 2
        font_title = effects.font(size, bold=True)
    d.text((320, 205), title, font=font_title, fill=effects.INK, anchor="ls")
    d.rectangle([322, 222, 420, 228], fill=effects.ACCENT)
    d.text((322, 272), sub, font=effects.font(26), fill=effects.INK_SOFT, anchor="ls")
    mark = effects.logo(46, effects.INK)
    img.alpha_composite(mark, (34, H - 34 - mark.height))
    d.text((34 + mark.width + 14, H - 34 - mark.height / 2), "HPDG", font=effects.font(28, bold=True),
           fill=effects.INK, anchor="lm")
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f"item_{num}.png"
    img.convert("RGB").save(path, optimize=True)
    print(path)


if __name__ == "__main__":
    for item in ITEMS:
        make(*item)
