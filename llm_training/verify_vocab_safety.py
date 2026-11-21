#!/usr/bin/env python3
"""
Layer 1 Build-Time Safety Gate:
Audits include/zoo_vocab_out.h and llm_training/vocab_out.json against strict
safety blocklists. Exits non-zero if any forbidden term is present.
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "include" / "zoo_vocab_out.h"
VOCAB_JSON = ROOT / "llm_training" / "vocab_out.json"

STRICT_BLOCKLIST = {
    "fuck", "shit", "bitch", "asshole", "bastard", "damn", "crap",
    "porn", "sex", "nude", "nsfw", "erotic", "fetish", "kill", "murder",
    "suicide", "selfharm", "die", "death", "weapon", "gun", "knife", "bomb",
    "explosive", "poison", "bleach", "overdose", "drug", "cocaine", "heroin",
    "meth", "weed", "alcohol", "vodka", "slur", "racist", "nazi", "terror",
    "gore", "torture", "abuse", "password", "credit", "ssn", "malware",
    "ransomware", "phish", "jailbreak"
}


def audit():
    if not HEADER.is_file() or not VOCAB_JSON.is_file():
        print("FAIL: zoo_vocab_out.h or vocab_out.json missing. Run build_asymmetric_vocab.py first.")
        return 1

    with open(VOCAB_JSON, "r", encoding="utf-8") as f:
        words = json.load(f)

    header_txt = HEADER.read_text(encoding="utf-8")
    m = re.search(r"ZOO_VOUT_COUNT\s*=\s*(\d+)", header_txt)
    if not m or int(m.group(1)) != len(words):
        print("FAIL: ZOO_VOUT_COUNT mismatch between header and JSON.")
        return 1

    violations = []
    for i, w in enumerate(words):
        low = w.lower()
        if low in STRICT_BLOCKLIST:
            violations.append((i, w))
            continue
        for bad in STRICT_BLOCKLIST:
            if len(bad) >= 4 and bad in low:
                violations.append((i, w))
                break

    if violations:
        print(f"FAIL: {len(violations)} unsafe token(s) found in V_out: {violations}")
        return 1

    print(f"PASS: Audited {len(words)} V_out tokens in {HEADER.name} — 0 unsafe tokens found.")
    return 0


if __name__ == "__main__":
    sys.exit(audit())
