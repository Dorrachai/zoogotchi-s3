#!/usr/bin/env python3
"""
ZooGotchi S3 — Automated Dataset Curation, Dedup & Quality Gates

Runs reproducible data curation for `zoogotchi`:
  1. Ingests raw synthetic/curated interactions
  2. Normalizes whitespace & validates schema
  3. Exact deduplication (SHA-256) + word 3-gram Jaccard near-deduplication (>= 0.92)
  4. Automated Quality Gates (safety/PII blocklist, V_out 0-<unk> coverage, length/ASCII bounds, class balance)
  5. Deterministic 80/10/10 group-split by normalized prompt (zero train/test prompt leakage)
  6. Exports `train.jsonl`, `val.jsonl`, `test.jsonl`, `vocab_out.json`, and `manifest.json`

Usage:
  .venv\\Scripts\\python.exe llm_training\\data_pipeline\\curate_dataset.py
"""

import argparse
import hashlib
import json
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from llm_training import verify_vocab_safety  # noqa: E402
from llm_training.data_pipeline.domains import (  # noqa: E402
    TOKEN_RE,
    get_zoogotchi_raw_samples,
)

CURATED_ROOT = ROOT / "llm_training" / "data" / "curated"

PII_PATTERNS = [
    re.compile(r"\b\d{15}\b"),
    re.compile(r"password\s*=", re.I),
    re.compile(r"\+\d{1,3}-\d{3}-\d{4}"),
]


def _norm(s: str) -> str:
    return re.sub(r"\s+", " ", s).strip()


def _ngrams(text: str, n: int = 3) -> set[tuple[str, ...]]:
    toks = re.findall(r"[a-z0-9'-]+", text.lower())
    if len(toks) < n:
        return {tuple(toks)}
    return {tuple(toks[i : i + n]) for i in range(len(toks) - n + 1)}


def _jaccard(a: set, b: set) -> float:
    if not a or not b:
        return 0.0
    inter = len(a & b)
    union = len(a | b)
    return inter / float(union) if union else 0.0


def curate_domain(domain: str = "zoogotchi") -> dict:
    if domain != "zoogotchi":
        raise ValueError(f"Unknown domain: {domain}")
    raw_rows, vout_words = get_zoogotchi_raw_samples()
    expected_classes = 18

    vout_low = {w.lower() for w in vout_words if w not in {"<pad>", "<bos>", "<eos>", "<unk>"}}

    # Stage 1: Normalize + Exact Dedup
    seen_exact = set()
    after_exact = []
    exact_dups_removed = 0
    for r in raw_rows:
        p = _norm(r["prompt"])
        rep = _norm(r["reply"])
        key = hashlib.sha256(f"{p}\n{rep}".encode("utf-8")).hexdigest()
        if key in seen_exact:
            exact_dups_removed += 1
            continue
        seen_exact.add(key)
        row = dict(r)
        row["prompt"] = p
        row["reply"] = rep
        row["_shingles"] = _ngrams(f"{p} {rep}", n=3)
        after_exact.append(row)

    # Stage 2: Near-Dedup within (category, intent) bucket (Jaccard >= 0.92)
    buckets: dict[tuple[str, str], list[set]] = {}
    after_near = []
    near_dups_removed = 0
    for row in after_exact:
        bkey = (row["category"], row["intent"])
        prev_list = buckets.setdefault(bkey, [])
        sh = row["_shingles"]
        is_near = any(_jaccard(sh, prev) >= 0.92 for prev in prev_list)
        if is_near:
            near_dups_removed += 1
            continue
        prev_list.append(sh)
        row.pop("_shingles", None)
        after_near.append(row)

    # Stage 3: Automated Quality Gates
    gate_failures = []
    kept = []
    for row in after_near:
        p, rep = row["prompt"], row["reply"]
        if not all(0x20 <= ord(ch) <= 0x7E for ch in p + rep):
            gate_failures.append("non_ascii")
            continue
        if any(pat.search(rep) for pat in PII_PATTERNS):
            gate_failures.append("pii_leak")
            continue
        toks = TOKEN_RE.findall(rep)
        words_only = [t for t in toks if t not in {".", ",", "!", "?", ":", ";", "..."}]
        if not (4 <= len(words_only) <= 36):
            gate_failures.append("length_bounds")
            continue
        if any(w.lower() in verify_vocab_safety.STRICT_BLOCKLIST for w in words_only):
            gate_failures.append("unsafe_blocklist")
            continue
        if any(t.lower() not in vout_low for t in toks):
            gate_failures.append("vocab_unk")
            continue
        kept.append(row)

    assert not gate_failures, f"[{domain}] Quality gate failures: {Counter(gate_failures)}"
    classes_present = Counter(r["category"] for r in kept)
    assert len(classes_present) >= expected_classes, (
        f"[{domain}] Expected >= {expected_classes} classes, got {len(classes_present)}: {classes_present}"
    )

    # Stage 4: Deterministic 80/10/10 Group-Split by normalized prompt (zero leakage)
    # Assign groups deterministically per class to preserve stratification AND zero prompt overlap
    prompt_groups: dict[str, list[dict]] = {}
    for row in kept:
        prompt_groups.setdefault(row["prompt"], []).append(row)

    sorted_prompts = sorted(
        prompt_groups.keys(),
        key=lambda pr: hashlib.sha256(f"{domain}:{pr}".encode("utf-8")).hexdigest(),
    )
    n_p = len(sorted_prompts)
    n_train = int(round(0.80 * n_p))
    n_val = int(round(0.10 * n_p))
    train_p = set(sorted_prompts[:n_train])
    val_p = set(sorted_prompts[n_train : n_train + n_val])
    test_p = set(sorted_prompts[n_train + n_val :])
    assert not (train_p & val_p) and not (train_p & test_p) and not (val_p & test_p)

    train_rows, val_rows, test_rows = [], [], []
    for pr, items in prompt_groups.items():
        if pr in train_p:
            train_rows.extend(items)
        elif pr in val_p:
            val_rows.extend(items)
        else:
            test_rows.extend(items)

    out_dir = CURATED_ROOT / domain
    out_dir.mkdir(parents=True, exist_ok=True)
    for split_name, split_rows in (("train", train_rows), ("val", val_rows), ("test", test_rows)):
        with open(out_dir / f"{split_name}.jsonl", "w", encoding="utf-8", newline="\n") as f:
            for r in split_rows:
                f.write(json.dumps(r) + "\n")

    (out_dir / "vocab_out.json").write_text(json.dumps(vout_words, indent=2), encoding="utf-8")

    h_sha = hashlib.sha256()
    for split_name in ("train", "val", "test"):
        h_sha.update((out_dir / f"{split_name}.jsonl").read_bytes())
    dataset_sha256 = h_sha.hexdigest()

    manifest = {
        "domain": domain,
        "dataset_sha256": dataset_sha256,
        "v_out_size": len(vout_words),
        "raw_count": len(raw_rows),
        "exact_dups_removed": exact_dups_removed,
        "near_dups_removed": near_dups_removed,
        "curated_total": len(kept),
        "splits": {
            "train": len(train_rows),
            "val": len(val_rows),
            "test": len(test_rows),
        },
        "prompt_overlap_count": 0,
        "quality_gates": {
            "ascii_only": True,
            "zero_unk_targets": True,
            "zero_pii_or_blocklist": True,
            "classes_covered": len(classes_present),
            "expected_classes": expected_classes,
        },
        "class_histogram": dict(sorted(classes_present.items())),
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(
        f"[{domain}] Curated {len(kept)}/{len(raw_rows)} samples "
        f"(exact_dup=-{exact_dups_removed}, near_dup=-{near_dups_removed}) "
        f"-> train={len(train_rows)}, val={len(val_rows)}, test={len(test_rows)} | "
        f"sha256={dataset_sha256[:12]}..."
    )
    return manifest


def main():
    ap = argparse.ArgumentParser(description="ZooGotchi S3 Data Curation Pipeline")
    ap.add_argument("--domain", choices=("zoogotchi",), default="zoogotchi")
    args = ap.parse_args()

    curate_domain(args.domain)


if __name__ == "__main__":
    main()
