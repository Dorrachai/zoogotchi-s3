#!/usr/bin/env python3
"""Gzip-compress web_ui/index.html into data/web/index.html.gz for LittleFS."""
import gzip
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "web_ui" / "index.html"
DST = ROOT / "data" / "web" / "index.html.gz"


def main():
    DST.parent.mkdir(parents=True, exist_ok=True)
    raw = SRC.read_bytes()
    with gzip.open(DST, "wb", compresslevel=9) as f:
        f.write(raw)
    sz = DST.stat().st_size
    print(f"Built {DST} ({sz} bytes gzipped from {len(raw)} bytes raw)")
    if sz >= 35 * 1024:
        raise SystemExit(f"ERROR: web bundle {sz} bytes exceeds 35 KB target")


if __name__ == "__main__":
    main()
