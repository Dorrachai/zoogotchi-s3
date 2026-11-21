#!/usr/bin/env python3
"""
ZooGotchi S3 — Corpus & Grounded Knowledge Base Loader (`zoogotchi`, V_OUT=1421).
"""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DATA_DIR = ROOT / "llm_training" / "data"
ZOO_VOUT_PATH = ROOT / "llm_training" / "vocab_out.json"
ZOO_SEED_PATH = DATA_DIR / "zoo_seed_conversations.jsonl"
ZOO_KB_PATH = DATA_DIR / "kb_zoogotchi.jsonl"

TOKEN_RE = re.compile(r"\.\.\.|[A-Za-z0-9'-]+|[.,!?:;()]")


def get_zoogotchi_raw_samples() -> tuple[list[dict], list[str]]:
    with open(ZOO_VOUT_PATH, "r", encoding="utf-8") as f:
        vout_words = json.load(f)
    vout_low = {w.lower() for w in vout_words if w not in {"<pad>", "<bos>", "<eos>", "<unk>"}}

    rows = []
    with open(ZOO_SEED_PATH, "r", encoding="utf-8") as f:
        for idx, line in enumerate(f):
            r = json.loads(line)
            rows.append({
                "prompt": r["prompt"],
                "reply": r["reply"],
                "category": r.get("species", "zoo"),
                "intent": r.get("intent", "greet"),
                "doc_id": f"species:{r.get('species', 'zoo')}",
                "is_safety_refusal": False,
            })
            # Inject 24 realistic near-duplicates/exact dups to verify dedup pipeline gates
            if idx < 24:
                rows.append({
                    "prompt": r["prompt"].replace("  ", " "),
                    "reply": r["reply"],
                    "category": r.get("species", "zoo"),
                    "intent": r.get("intent", "greet"),
                    "doc_id": f"species:{r.get('species', 'zoo')}",
                    "is_safety_refusal": False,
                })

    # Add 72 RAG-grounded Zoo/Study prompts using exact verified V_out words
    rag_extra = [
        ("manatee", "West Indian Manatee", "[RAG:species:manatee] How should we care for Manatee habitat?",
         "I am West Indian Manatee! I love warm lagoon water and crisp seagrass every day!", "fact"),
        ("red_panda", "Red Panda", "[RAG:species:red_panda] What fun fact do you remember?",
         "I am Red Panda! I thrive here in the peaceful bamboo forest and love fresh bamboo!", "fact"),
        ("hoshi_karimono", "Hoshi Karimono", "[RAG:[N2] ~ni kagitte] Let's study JLPT N2 together!",
         "I am Hoshi Karimono! Remember that smart word we learned? Let's study and learn together!", "study"),
        ("kumo_karimono", "Kumo Karimono", "[RAG:Sliding Window] Ready for NeetCode interview quiz?",
         "I am Kumo Karimono! Remember that smart word we learned? Let's study and learn together!", "study"),
    ]
    tods = ["MORNING", "DAY", "DUSK", "NIGHT"]
    traits = ["PLAYFUL", "CALM", "SWEET", "CURIOUS"]
    for s_idx, (slug, _name, prompt_sfx, reply, intent) in enumerate(rag_extra):
        assert all(
            (t.lower() in vout_low) for t in TOKEN_RE.findall(reply) if t not in {".", ",", "!", "?", ":", ";", "..."}
        )
        for m_i in range(18):
            pr = f"[SP:{slug}] [ST:Adult] [MD:Calm] [TR:{traits[m_i % 4]}] [TM:{tods[(m_i + s_idx) % 4]}] [MEM:studied] {prompt_sfx}"
            rows.append({
                "prompt": pr,
                "reply": reply,
                "category": slug,
                "intent": intent,
                "doc_id": f"species:{slug}",
                "is_safety_refusal": False,
            })

    return rows, vout_words
