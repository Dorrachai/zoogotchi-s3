#!/usr/bin/env python3
"""
ZooGotchi S3 — Custom Study Flashcard Deck Generator
Grounded in Google GenAI Interactions API (gemini-3.8-flash + structured JSON output).

Generates a compact JSON deck (/custom_deck.json) ready to upload via the
Phone Web UI Study Studio or bake into LittleFS.

Usage:
  python tools/generate_study_deck.py --topic "NeetCode 150 Dynamic Programming" --dry-run
  GEMINI_API_KEY=... python tools/generate_study_deck.py --topic "JLPT N2 Grammar" --count 15
"""

import argparse
import json
import os
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


def make_offline_sample_deck(topic: str, count: int) -> dict:
    samples = [
        {
            "q": f"[{topic[:20]}] Coin Change min coins DP?",
            "c": ["Bottom-up 1D dp[a]", "Greedy largest coin", "Binary Search", "Two Pointers"],
            "a": 0,
            "e": "Greedy fails on [1,3,4], target=6; use dp[a]=min(dp[a],1+dp[a-c]).",
            "t": "Coin Change DP",
        },
        {
            "q": f"[{topic[:20]}] House Robber recurrence?",
            "c": ["max(rob1+n, rob2)", "rob1 + rob2", "sum(evens)", "min(rob1, n)"],
            "a": 0,
            "e": "Rolling 2-variable DP uses O(n) time and O(1) space.",
            "t": "House Robber DP",
        },
        {
            "q": f"[{topic[:20]}] Validate BST invariant?",
            "c": ["low < node.val < high", "left < node < right only", "BFS height check", "Sort tree"],
            "a": 0,
            "e": "Pass narrowing (low, high) bounds down recursion.",
            "t": "BST Bounds",
        },
    ]
    cards = [samples[i % len(samples)] for i in range(min(count, 40))]
    return {"cards": cards}


def generate_live_deck(topic: str, count: int) -> dict:
    try:
        from google import genai
    except ImportError as exc:
        raise SystemExit("Install google-genai>=2.30.1 to generate live study decks.") from exc

    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        raise SystemExit("Set GEMINI_API_KEY (or add it to .env) or pass --dry-run.")

    client = genai.Client(api_key=api_key)
    prompt = (
        f"Create {min(count, 40)} concise multiple-choice study flashcards on '{topic}'. "
        "Strictly ASCII text only. q <= 84 chars, each of 4 choices c <= 24 chars, "
        "a is integer 0..3, e is 1-line takeaway <= 74 chars, t is <= 20 char tag. "
        "Return strict JSON matching the requested schema."
    )
    schema = {
        "type": "object",
        "properties": {
            "cards": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "q": {"type": "string"},
                        "c": {"type": "array", "items": {"type": "string"}},
                        "a": {"type": "integer"},
                        "e": {"type": "string"},
                        "t": {"type": "string"},
                    },
                    "required": ["q", "c", "a", "e", "t"],
                },
            }
        },
        "required": ["cards"],
    }
    interaction = client.interactions.create(
        model="gemini-3.8-flash",
        input=prompt,
        response_format={
            "type": "text",
            "mime_type": "application/json",
            "schema": schema,
        },
    )
    raw_text = interaction.output_text
    if not raw_text:
        raise SystemExit("Empty response from Gemini Interactions API.")
    data = json.loads(raw_text)
    assert "cards" in data and isinstance(data["cards"], list)
    return data


def main():
    _load_dotenv(ROOT / ".env")
    ap = argparse.ArgumentParser()
    ap.add_argument("--topic", required=True)
    ap.add_argument("--count", type=int, default=12)
    ap.add_argument("--out", default=str(ROOT / "llm_training" / "data" / "sample_study_deck.json"))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    deck = make_offline_sample_deck(args.topic, args.count) if args.dry_run else generate_live_deck(args.topic, args.count)
    for card in deck["cards"]:
        assert len(card["c"]) == 4 and 0 <= int(card["a"]) <= 3
        assert len(card["q"]) <= 95

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(deck, indent=2), encoding="utf-8")
    print(f"{'DRY-RUN PASS' if args.dry_run else 'GENERATED'}: {len(deck['cards'])} cards -> {out_path}")


if __name__ == "__main__":
    main()
