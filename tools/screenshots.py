"""Renders the documentation screenshots and writes them to docs/images as PNG.

Runs the `screenshots` example (it draws the gallery in each theme without a screen), then
converts its PPM output to compressed PNG with the standard library's zlib. Screenshots are
never captured from a desktop, so nothing else on the machine can appear in them.

    python tools/screenshots.py build/windows/Release/screenshots.exe
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "images"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
RENDER_TIMEOUT_S = 120


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    parts = data.split(maxsplit=4)  # P6, width, height, maxval, pixels
    if parts[0] != b"P6" or parts[3] != b"255":
        raise ValueError(f"{path} is not an 8-bit binary PPM")
    width, height = int(parts[1]), int(parts[2])
    pixels = parts[4]
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path} is truncated")
    return width, height, pixels


def png_bytes(width: int, height: int, rgb: bytes) -> bytes:
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    stride = width * 3
    rows = b"".join(b"\0" + rgb[y * stride : (y + 1) * stride] for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # 8-bit RGB, no interlace
    return (
        PNG_SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("renderer", type=Path, help="the built `screenshots` example")
    opts = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        result = subprocess.run(
            [str(opts.renderer), tmp], capture_output=True, text=True, timeout=RENDER_TIMEOUT_S
        )
        if result.returncode != 0:
            print(result.stdout + result.stderr)
            return 1
        for ppm in sorted(Path(tmp).glob("*.ppm")):
            width, height, rgb = read_ppm(ppm)
            target = OUT / (ppm.stem + ".png")
            target.write_bytes(png_bytes(width, height, rgb))
            print(f"{target.relative_to(ROOT)}: {width}x{height}, {target.stat().st_size // 1024} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
