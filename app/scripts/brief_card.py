"""The picture a post of the weekly brief's link shows (1200 x 630): the stance,
its state, and the week's rates and economy. Nothing of it is a screen's pick
or an insider's name: a correction of those never leaves a wrong picture.
Needs Pillow and a DejaVu font; without them there is no picture."""

from pathlib import Path

W, H = 1200, 630
BG, FG, DIM, LINE = "#0d1117", "#e6edf3", "#8b949e", "#30363d"
TONE = {"bullish": "#3fb950", "bullish_nervous": "#d29922", "bearish": "#f85149", "fear": "#ff7b72"}
FONT_DIRS = ("/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/dejavu", "/usr/share/fonts/TTF",
             "/Library/Fonts", "/opt/homebrew/share/fonts")


def _font(size, bold=False):
    from PIL import ImageFont

    name = "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"
    for d in FONT_DIRS:
        if (Path(d) / name).exists():
            return ImageFont.truetype(str(Path(d) / name), size)
    return ImageFont.truetype(name, size)  # raises OSError when there is none


def make(path, title, state, headline, words, cells):
    """Write the card, or return False when it cannot be drawn. @cells:
    [(label, value, note)], up to six."""
    try:
        from PIL import Image, ImageDraw

        big, mid, small, tiny = _font(76, True), _font(38, True), _font(30), _font(24)
    except (ImportError, OSError):
        return False
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    tone = TONE.get(state, FG)
    d.rectangle([0, 0, 14, H], fill=tone)
    d.text((64, 48), title, font=small, fill=DIM)
    d.text((W - 64, 48), "finterm", font=_font(34, True), fill="#f0a020", anchor="ra")
    size = 76
    while size > 40 and d.textlength(headline, font=big) > W - 128:
        size -= 4
        big = _font(size, True)
    d.text((64, 118), headline, font=big, fill=tone)
    d.text((64, 222), words, font=small, fill=FG)
    d.line([64, 296, W - 64, 296], fill=LINE, width=2)
    for i, (label, value, note) in enumerate(cells[:6]):
        x, y = 64 + (i % 3) * 365, 326 + (i // 3) * 132
        d.text((x, y), label, font=tiny, fill=DIM)
        d.text((x, y + 32), value, font=mid, fill=FG)
        d.text((x + d.textlength(value, font=mid) + 14, y + 44), note, font=tiny, fill=DIM)
    d.text((64, H - 46), "yumima.github.io/finterm · fixed rules, not personal advice", font=tiny, fill=DIM)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    img.save(path, optimize=True)
    return True
