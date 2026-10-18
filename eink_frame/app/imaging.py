"""Image conversion for the Waveshare 13.3" E Ink Spectra 6 (E6) panel.

The panel is natively 1200 x 1600 (portrait), 4 bits per pixel, two pixels per
byte with the left pixel in the high nibble.  Rows are 600 bytes; the firmware
sends the left 300 bytes of every row to the master controller and the right
300 bytes to the slave controller.
"""

from io import BytesIO

from PIL import Image, ImageChops, ImageEnhance, ImageOps

NATIVE_W = 1200
NATIVE_H = 1600
FRAME_BYTES = NATIVE_W * NATIVE_H // 2  # 960,000

# Order used everywhere below: black, white, yellow, red, blue, green.
PANEL_CODES = [0x0, 0x1, 0x2, 0x3, 0x5, 0x6]

# What the inks are driven towards ...
IDEAL_PALETTE = [
    (0, 0, 0),
    (255, 255, 255),
    (255, 230, 0),
    (200, 0, 0),
    (0, 60, 200),
    (0, 140, 60),
]
# ... and roughly what they look like on a real Spectra 6 panel.  Dithering
# against the muted colours gives far more natural photos; tune these after
# photographing your own panel if needed.
MEASURED_PALETTE = [
    (25, 25, 30),
    (200, 200, 195),
    (215, 195, 40),
    (160, 40, 40),
    (45, 65, 140),
    (50, 110, 70),
]

MODES = ("photo", "drawing")
FITS = ("crop", "contain")


def blended_palette(saturation):
    """0.0 = measured (muted) colours, 1.0 = ideal colours."""
    s = max(0.0, min(1.0, float(saturation)))
    return [
        tuple(round(m + (i - m) * s) for m, i in zip(measured, ideal))
        for measured, ideal in zip(MEASURED_PALETTE, IDEAL_PALETTE)
    ]


def _palette_image(colours):
    flat = []
    for c in colours:
        flat.extend(c)
    # Pad the 256-entry palette with black so stray indices still map safely.
    flat.extend(colours[0] * (256 - len(colours)))
    pal = Image.new("P", (1, 1))
    pal.putpalette(flat)
    return pal


def logical_size(orientation):
    return (NATIVE_H, NATIVE_W) if orientation == "landscape" else (NATIVE_W, NATIVE_H)


def open_upload(data):
    img = Image.open(BytesIO(data))
    img = ImageOps.exif_transpose(img)
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        background = Image.new("RGBA", img.size, (255, 255, 255, 255))
        img = Image.alpha_composite(background, img)
    return img.convert("RGB")


def shrink_original(img, longest=2400):
    """Keep a moderately sized copy of the upload so it can be reprocessed."""
    img = img.copy()
    img.thumbnail((longest, longest), Image.LANCZOS)
    return img


def convert(img, *, mode="photo", fit="crop", orientation="landscape", flip=False, saturation=0.5):
    """Return (frame_bytes, preview_image) for an RGB PIL image."""
    size = logical_size(orientation)
    if fit == "contain":
        canvas = ImageOps.pad(img, size, method=Image.LANCZOS, color=(255, 255, 255))
    else:
        canvas = ImageOps.fit(img, size, method=Image.LANCZOS)

    if mode == "photo":
        canvas = ImageOps.autocontrast(canvas, cutoff=0.5)
        canvas = ImageEnhance.Color(canvas).enhance(1.35)
        canvas = ImageEnhance.Contrast(canvas).enhance(1.1)
        dither = Image.Dither.FLOYDSTEINBERG
    else:
        dither = Image.Dither.NONE

    colours = blended_palette(saturation)
    indexed = canvas.quantize(palette=_palette_image(colours), dither=dither)

    preview = indexed.copy()
    preview.putpalette(_palette_image(MEASURED_PALETTE).getpalette())
    preview = preview.convert("RGB")

    native = indexed
    if orientation == "landscape":
        native = native.transpose(Image.Transpose.ROTATE_90)
    if flip:
        native = native.transpose(Image.Transpose.ROTATE_180)
    assert native.size == (NATIVE_W, NATIVE_H), native.size

    return pack(native), preview


def pack(indexed):
    """Pack a 1200x1600 palette-index image into the panel's 4bpp layout."""
    raw = indexed.tobytes()
    to_code = bytes(PANEL_CODES[i] if i < len(PANEL_CODES) else 0 for i in range(256))
    codes = raw.translate(to_code)
    half_w = NATIVE_W // 2
    hi = Image.frombytes("L", (half_w, NATIVE_H), codes[0::2])
    lo = Image.frombytes("L", (half_w, NATIVE_H), codes[1::2])
    hi = hi.point(lambda v: v << 4)
    packed = ImageChops.add(hi, lo).tobytes()  # codes <= 6, so add == OR
    assert len(packed) == FRAME_BYTES
    return packed


def thumbnail(preview, longest=480):
    thumb = preview.copy()
    thumb.thumbnail((longest, longest), Image.LANCZOS)
    return thumb
