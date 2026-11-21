#!/usr/bin/env python3
"""
ZooGotchi S3 — Sprite Generator using Google GenAI Interactions API or gemini-api CLI
Follows .agents/skills/gemini-api-cli/SKILL.md and
C:/Users/Theo/.gemini/config/plugins/gemini-api/skills/gemini-api-dev/SKILL.md.

Usage:
  python tools/generate_zoo_sprites.py --dry-run
  python tools/generate_zoo_sprites.py --backend sdk --pack
  python tools/generate_zoo_sprites.py --backend cli --out-dir tools/raw_sprites
"""

import argparse
import base64
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def _load_dotenv(env_path: Path):
    if not env_path.is_file():
        return
    for raw in env_path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        k = k.strip()
        v = v.strip().strip('"').strip("'")
        if k and v:
            os.environ.setdefault(k, v)

SPECIES_PROMPTS = [
    ("red_panda", 3, "cute 16-bit pixel art red panda with ringed tail, centered, crisp silhouette, solid #FFFFFF background, no text"),
    ("fennec_fox", 3, "cute 16-bit pixel art fennec fox with big ears, centered, solid #FFFFFF background, no text"),
    ("capybara", 3, "cute 16-bit pixel art calm capybara, centered, solid #FFFFFF background, no text"),
    ("barn_owl", 3, "cute 16-bit pixel art white heart-faced barn owl, centered, solid #FFFFFF background, no text"),
    ("scarlet_macaw", 3, "cute 16-bit pixel art scarlet macaw parrot, centered, solid #FFFFFF background, no text"),
    ("hummingbird", 3, "cute 16-bit pixel art ruby-throated hummingbird, centered, solid #FFFFFF background, no text"),
    ("monarch", 3, "cute 16-bit pixel art orange monarch butterfly, centered, solid #FFFFFF background, no text"),
    ("hercules_beetle", 3, "cute 16-bit pixel art olive hercules beetle, centered, solid #FFFFFF background, no text"),
    ("orchid_mantis", 3, "cute 16-bit pixel art pink orchid mantis, centered, solid #FFFFFF background, no text"),
    ("manatee", 3, "cute 16-bit pixel art plump West Indian manatee swimming calmly, centered, solid #FFFFFF background, no text"),
    ("axolotl", 3, "cute 16-bit pixel art pink smiling axolotl with frilly gills, centered, solid #FFFFFF background, no text"),
    ("octopus", 3, "cute 16-bit pixel art friendly coral pacific octopus, centered, solid #FFFFFF background, no text"),
    ("tortoise", 3, "cute 16-bit pixel art gentle galapagos tortoise, centered, solid #FFFFFF background, no text"),
    ("chameleon", 3, "cute 16-bit pixel art emerald veiled chameleon, centered, solid #FFFFFF background, no text"),
    ("leopard_gecko", 3, "cute 16-bit pixel art golden spotted leopard gecko, centered, solid #FFFFFF background, no text"),
    ("hoshi_karimono", 3, "cute 16-bit pixel art cosmic star tamagotchi spirit creature, centered, solid #FFFFFF background, no text"),
    ("kumo_karimono", 3, "cute 16-bit pixel art pastel cloud puff tamagotchi creature, centered, solid #FFFFFF background, no text"),
    ("kuro_karimono", 3, "cute 16-bit pixel art playful horned ember tamagotchi creature, centered, solid #FFFFFF background, no text"),
]


def _pack_sprite(png_path: Path, slug: str, stage: int):
    script_dir = Path(__file__).resolve().parent
    if str(script_dir) not in sys.path:
        sys.path.insert(0, str(script_dir))
    import zoo_sprite_pipeline

    dst = ROOT / "data" / "sprites" / f"{slug}_{stage}.bin"
    zoo_sprite_pipeline.convert_png_to_bin(png_path, dst, colorkey_white=True)


def main():
    _load_dotenv(ROOT / ".env")
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true", help="Preview generation requests without calling API")
    ap.add_argument("--backend", choices=("sdk", "cli"), default="sdk", help="Use Google GenAI SDK (default) or gemini-api CLI")
    ap.add_argument("--out-dir", default=str(ROOT / "tools" / "raw_sprites"))
    ap.add_argument("--pack", action="store_true", help="Also pack generated PNGs into data/sprites/*.bin")
    ap.add_argument("--missing-only", action="store_true", help="Skip species whose data/sprites/<slug>_<stage>.bin already exists")
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    selected = [
        (slug, stage, prompt)
        for (slug, stage, prompt) in SPECIES_PROMPTS
        if not (args.missing_only and (ROOT / "data" / "sprites" / f"{slug}_{stage}.bin").is_file())
    ]

    if args.backend == "cli":
        cli = shutil.which("gemini-api")
        for slug, stage, prompt in selected:
            out_path = out_dir / f"{slug}_{stage}.png"
            cmd = [
                cli or "gemini-api",
                "image", prompt,
                "-m", "gemini-nano-banana-2.1",
                "--aspect-ratio", "1:1",
                "--out", str(out_path),
            ]
            if args.dry_run:
                cmd.append("--dry-run")
            print("CMD:", " ".join(f'"{c}"' if " " in c else c for c in cmd))
            if not args.dry_run:
                if not cli:
                    raise SystemExit("gemini-api CLI not found on PATH; use --backend sdk or install gemini-api.")
                out_dir.mkdir(parents=True, exist_ok=True)
                subprocess.run(cmd, check=True)
                if args.pack:
                    _pack_sprite(out_path, slug, stage)
    else:
        client = None
        if not args.dry_run and selected:
            try:
                from google import genai
            except ImportError as exc:
                raise SystemExit("Install google-genai>=2.30.1 to generate sprites with --backend sdk.") from exc
            if not os.environ.get("GEMINI_API_KEY"):
                raise SystemExit("Set GEMINI_API_KEY (or add it to .env) or pass --dry-run.")
            client = genai.Client()

        for slug, stage, prompt in selected:
            out_path = out_dir / f"{slug}_{stage}.png"
            if args.dry_run:
                print(
                    f"[DRY-RUN SDK] client.interactions.create(model='gemini-nano-banana-2.1', "
                    f"response_format={{'type':'image','aspect_ratio':'1:1','image_size':'1K'}}) -> {out_path}"
                )
                if args.pack:
                    print(f"  -> pack {out_path} -> {ROOT / 'data' / 'sprites' / f'{slug}_{stage}.bin'}")
                continue
            interaction = client.interactions.create(
                model="gemini-nano-banana-2.1",
                input=prompt,
                response_format={
                    "type": "image",
                    "aspect_ratio": "1:1",
                    "image_size": "1K",
                },
            )
            img_obj = interaction.output_image
            if img_obj is None or not getattr(img_obj, "data", None):
                raise SystemExit(f"No image returned for {slug}")
            out_dir.mkdir(parents=True, exist_ok=True)
            out_path.write_bytes(base64.b64decode(img_obj.data))
            print(f"Saved {out_path}")
            if args.pack:
                _pack_sprite(out_path, slug, stage)

    print(f"DRY-RUN PASS: Prepared {len(selected)} sprite requests ({args.backend})." if args.dry_run
          else f"Generated {len(selected)} sprite PNGs in {out_dir}")


if __name__ == "__main__":
    main()
