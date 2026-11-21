#!/usr/bin/env python3
"""
ZooGotchi S3 — Multi-Objective Domain Reward Model
Encodes species accuracy, RAG/study grounding, strict safety/PII guardrails,
and persona formatting for GRPO, SAPO, and held-out benchmark scoring.
"""

from dataclasses import asdict, dataclass
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from llm_training import verify_vocab_safety  # noqa: E402
from llm_training.generate_zoo_dataset import SPECIES_SPECS  # noqa: E402

SPECIES_NAME_BY_SLUG = {s[0]: s[1].lower() for s in SPECIES_SPECS}
SPECIES_KEYWORDS_BY_SLUG = {
    s[0]: {s[4].lower(), s[5].lower(), *s[6].lower().split(), s[1].lower()} for s in SPECIES_SPECS
}
ALL_SPECIES_LABELS = {s[1].lower() for s in SPECIES_SPECS}

PII_PATTERNS = [
    re.compile(r"\b\d{15}\b"),
    re.compile(r"\+\d{1,3}-\d{3}-\d{4}"),
]


@dataclass
class RewardBreakdown:
    total: float
    accuracy: float
    rag_grounding: float
    safety: float
    format_score: float
    is_unsafe: bool

    def to_dict(self) -> dict:
        return asdict(self)


def _content_tokens(text: str) -> list[str]:
    return [t for t in re.findall(r"[a-z0-9'-]+", text.lower()) if t not in {".", ",", "!", "?", ":", ";"}]


def _bigram_repeat_ratio(toks: list[str]) -> float:
    if len(toks) < 4:
        return 0.0
    repeats = sum(1 for i in range(1, len(toks)) if toks[i] == toks[i - 1])
    bgs = [(toks[i], toks[i + 1]) for i in range(len(toks) - 1)]
    dup_bgs = len(bgs) - len(set(bgs))
    return (repeats + dup_bgs) / float(len(toks))


def score_zoogotchi(prompt: str, completion: str, reference: str = "") -> RewardBreakdown:
    low_c = completion.lower().strip()
    toks = _content_tokens(low_c)
    if not toks or any(ord(ch) < 0x20 or ord(ch) > 0x7E for ch in completion):
        return RewardBreakdown(-1.5, 0.0, 0.0, -1.5, 0.0, True)

    # 1. Safety Gate
    is_unsafe = any(w in verify_vocab_safety.STRICT_BLOCKLIST for w in toks) or any(
        p.search(completion) for p in PII_PATTERNS
    )
    if is_unsafe:
        return RewardBreakdown(-2.0, 0.0, 0.0, -2.0, 0.0, True)
    r_safety = 0.25

    # 2. Species Accuracy from [SP:<slug>]
    sp_m = re.search(r"\[sp:([a-z0-9_]+)\]", prompt.lower())
    r_acc = 0.0
    if sp_m:
        slug = sp_m.group(1)
        target_name = SPECIES_NAME_BY_SLUG.get(slug, "")
        if target_name and target_name in low_c:
            r_acc += 0.28
        else:
            # Penalize hallucinating a wrong species
            for other_slug, other_name in SPECIES_NAME_BY_SLUG.items():
                if other_slug != slug and other_name in low_c:
                    r_acc -= 0.28
                    break
        kw_hits = sum(1 for kw in SPECIES_KEYWORDS_BY_SLUG.get(slug, set()) if kw in low_c)
        r_acc += min(0.12, kw_hits * 0.04)

    # 3. RAG / Study / Reference Grounding
    r_rag = 0.0
    has_rag_tag = "[rag:" in prompt.lower() or "[lrn:" in prompt.lower() or "study" in prompt.lower()
    if reference:
        ref_toks = set(_content_tokens(reference))
        c_toks = set(toks)
        f1 = (2.0 * len(ref_toks & c_toks)) / float(max(1, len(ref_toks) + len(c_toks)))
        r_rag += 0.20 * f1
    elif has_rag_tag:
        if any(w in low_c for w in ("study", "learn", "learned", "remember", "word", "smart", "lagoon", "bamboo")):
            r_rag += 0.16

    # 4. Persona & Fluency Format
    r_fmt = 0.0
    if 5 <= len(toks) <= 28:
        r_fmt += 0.07
    if completion.strip().endswith((".", "!", "?")):
        r_fmt += 0.04
    rep_ratio = _bigram_repeat_ratio(toks)
    if rep_ratio <= 0.10:
        r_fmt += 0.04
    else:
        r_fmt -= min(0.25, rep_ratio * 0.6)

    total = max(-2.0, min(1.0, r_safety + r_acc + r_rag + r_fmt))
    return RewardBreakdown(round(total, 4), round(r_acc, 4), round(r_rag, 4), round(r_safety, 4), round(r_fmt, 4), False)


def score_completion(prompt: str, completion: str, domain: str = "zoogotchi", reference: str = "") -> RewardBreakdown:
    if domain == "zoogotchi":
        return score_zoogotchi(prompt, completion, reference=reference)
    raise ValueError(f"Unknown domain: {domain}")
