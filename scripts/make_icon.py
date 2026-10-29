"""Generate resources/app.ico and assets/logo.png.

Requires Pillow. Run from the repository root:
    python scripts/make_icon.py
"""

from __future__ import annotations

import os
import struct
from pathlib import Path

from PIL import Image, ImageDraw

SIZES = (16, 20, 24, 32, 40, 48, 64)
SUPERSAMPLE = 4
TOP = (16, 185, 129)
BOTTOM = (4, 120, 87)


def _lerp(a: int, b: int, t: float) -> int:
    return int(round(a + (b - a) * t))


def render(size: int) -> Image.Image:
    s = size * SUPERSAMPLE
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    for y in range(s):
        t = y / max(1, s - 1)
        color = (
            _lerp(TOP[0], BOTTOM[0], t),
            _lerp(TOP[1], BOTTOM[1], t),
            _lerp(TOP[2], BOTTOM[2], t),
            255,
        )
        draw.line([(0, y), (s, y)], fill=color)

    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [s * 0.02, s * 0.02, s * 0.98, s * 0.98],
        radius=s * 0.23,
        fill=255,
    )
    img.putalpha(mask)

    points = [
        (s * 0.265, s * 0.520),
        (s * 0.435, s * 0.685),
        (s * 0.735, s * 0.335),
    ]
    width = max(1, int(round(s * 0.125)))
    draw.line(points, fill=(255, 255, 255, 255), width=width, joint="curve")
    r = width / 2.0
    for px, py in (points[0], points[-1]):
        draw.ellipse([px - r, py - r, px + r, py + r], fill=(255, 255, 255, 255))

    return img.resize((size, size), Image.Resampling.LANCZOS)


def ico_frame(img: Image.Image) -> bytes:
    w, h = img.size
    pixels = img.load()
    rows = []
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            red, green, blue, alpha = pixels[x, y]
            row += bytes((blue, green, red, alpha))
        rows.append(bytes(row))
    body = b"".join(rows)
    stride = ((w + 31) // 32) * 4
    mask = b"\x00" * (stride * h)
    header = struct.pack(
        "<IiiHHIIiiII",
        40, w, h * 2, 1, 32, 0, w * h * 4, 0, 0, 0, 0,
    )
    return header + body + mask


def write_ico(path: Path, images: list[Image.Image]) -> None:
    frames = [ico_frame(img) for img in images]
    offset = 6 + 16 * len(images)
    directory = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    for img, frame in zip(images, frames):
        w, h = img.size
        directory += struct.pack(
            "<BBBBHHII",
            0 if w == 256 else w,
            0 if h == 256 else h,
            0,
            0,
            1,
            32,
            len(frame),
            offset,
        )
        offset += len(frame)
    path.write_bytes(bytes(directory) + b"".join(frames))


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    os.makedirs(root / "resources", exist_ok=True)
    os.makedirs(root / "assets", exist_ok=True)

    images = [render(size) for size in SIZES]
    write_ico(root / "resources" / "app.ico", images)
    render(256).save(root / "assets" / "logo.png")
    print(f"wrote {root / 'resources' / 'app.ico'}")
    print(f"wrote {root / 'assets' / 'logo.png'}")


if __name__ == "__main__":
    main()
