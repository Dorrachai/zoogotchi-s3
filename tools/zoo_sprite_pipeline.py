#!/usr/bin/env python3
"""
ZooGotchi S3 — PNG to LittleFS 64x64 RGB565+A8 Sprite Binary Packer
Matches try_load_littlefs_sprite() in src/sprites/zoo_renderer.cpp:
  [uint16 w=64][uint16 h=64][64*64*2 bytes RGB565 LE][64*64 bytes Alpha]

Usage:
  python tools/zoo_sprite_pipeline.py tools/raw_sprites/manatee_3.png data/sprites/manatee_3.bin
  python tools/zoo_sprite_pipeline.py --batch tools/raw_sprites data/sprites
"""

import argparse
from collections import deque
import struct
import sys
from pathlib import Path
from PIL import Image


def rgb888_to_565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def _pixels(img: Image.Image) -> list:
    if hasattr(img, "get_flattened_data"):
        return list(img.get_flattened_data())
    return list(img.getdata())


def _border_flood_key_rgba(img: Image.Image) -> Image.Image:
    w, h = img.size
    px = _pixels(img)

    corners = [px[0], px[w - 1], px[(h - 1) * w], px[(h - 1) * w + (w - 1)]]
    opaque_corners = [c[:3] for c in corners if c[3] > 128]
    if opaque_corners:
        bg_r = sorted(c[0] for c in opaque_corners)[len(opaque_corners) // 2]
        bg_g = sorted(c[1] for c in opaque_corners)[len(opaque_corners) // 2]
        bg_b = sorted(c[2] for c in opaque_corners)[len(opaque_corners) // 2]
    else:
        bg_r, bg_g, bg_b = 255, 255, 255

    def is_bg_pixel(idx: int) -> bool:
        r, g, b, a = px[idx]
        if a <= 128:
            return True
        if r >= 232 and g >= 232 and b >= 232:
            return True
        dist = abs(r - bg_r) + abs(g - bg_g) + abs(b - bg_b)
        return dist <= 42

    visited = [False] * (w * h)
    q = deque()
    for x in range(w):
        for y in (0, h - 1):
            idx = y * w + x
            if not visited[idx] and is_bg_pixel(idx):
                visited[idx] = True
                q.append((x, y))
    for y in range(1, h - 1):
        for x in (0, w - 1):
            idx = y * w + x
            if not visited[idx] and is_bg_pixel(idx):
                visited[idx] = True
                q.append((x, y))

    while q:
        cx, cy = q.popleft()
        cidx = cy * w + cx
        r, g, b, _ = px[cidx]
        px[cidx] = (r, g, b, 0)
        for nx, ny in ((cx - 1, cy), (cx + 1, cy), (cx, cy - 1), (cx, cy + 1)):
            if 0 <= nx < w and 0 <= ny < h:
                nidx = ny * w + nx
                if not visited[nidx] and is_bg_pixel(nidx):
                    visited[nidx] = True
                    q.append((nx, ny))

    out = Image.new("RGBA", (w, h))
    out.putdata(px)
    return out


def convert_png_to_bin(src_png: Path, dst_bin: Path, colorkey_white: bool = True) -> float:
    raw = Image.open(src_png).convert("RGBA")
    resample = Image.Resampling.BOX if (raw.width > 64 or raw.height > 64) else Image.Resampling.NEAREST
    img = raw.resize((64, 64), resample)
    if colorkey_white:
        img = _border_flood_key_rgba(img)

    rgb_bytes = bytearray()
    alpha_bytes = bytearray()
    opaque_cnt = 0

    for r, g, b, a in _pixels(img):
        a_out = 255 if a > 128 else 0
        if a_out == 255:
            opaque_cnt += 1
        v = rgb888_to_565(r, g, b)
        rgb_bytes.append(v & 0xFF)
        rgb_bytes.append((v >> 8) & 0xFF)
        alpha_bytes.append(a_out)

    dst_bin.parent.mkdir(parents=True, exist_ok=True)
    with open(dst_bin, "wb") as f:
        f.write(struct.pack("<HH", 64, 64))
        f.write(rgb_bytes)
        f.write(alpha_bytes)

    ratio = opaque_cnt / float(64 * 64)
    print(f"Packed {src_png} -> {dst_bin} ({dst_bin.stat().st_size} bytes, opaque={ratio:.2%})")
    return ratio


def convert_batch(src_dir: Path, dst_dir: Path) -> list[tuple[Path, float]]:
    pngs = sorted(src_dir.glob("*.png"))
    if not pngs:
        raise SystemExit(f"No PNG files found in {src_dir}")
    results = []
    for p in pngs:
        out = dst_dir / f"{p.stem}.bin"
        ratio = convert_png_to_bin(p, out, colorkey_white=True)
        results.append((out, ratio))
    return results


def main():
    ap = argparse.ArgumentParser(description="ZooGotchi S3 64x64 RGB565+A8 Sprite Packer")
    ap.add_argument("--batch", action="store_true", help="Pack all *.png from <input_dir> into <output_dir>")
    ap.add_argument("src", help="Input PNG file or source directory (with --batch)")
    ap.add_argument("dst", help="Output .bin file or destination directory (with --batch)")
    args = ap.parse_args()

    if args.batch:
        convert_batch(Path(args.src), Path(args.dst))
    else:
        convert_png_to_bin(Path(args.src), Path(args.dst))


if __name__ == "__main__":
    main()
