#!/usr/bin/env python3
"""
ZooGotchi S3 — On-Device Compact BM25 RAG Index Builder
Extracts factual grounding chunks directly from firmware registries/study decks
and builds:
  1. data/rag/kb.bin  (LittleFS binary index < 64 KB for ESP32-S3 C++ retriever)
  2. llm_training/data/kb_zoogotchi.jsonl (Host Hybrid RAG corpus & eval source)

Binary format ("ZRAG" v1):
  Header (36B, <4sI4H5I):
    magic="ZRAG", version=1, num_chunks, num_terms, avg_dl_x100, reserved=0,
    chunk_off, term_off, post_off, total_bytes, crc32(data[36:])
  Chunks (num_chunks * 240B, <HBB40s36s160s):
    id, cat (0=species,1=dsa,2=jlpt,3=genetics,4=care), doc_len, title, tag, snippet
  Terms (num_terms * 12B sorted asc by hash, <IHHI):
    fnv1a_hash, idf_x100, post_cnt, post_rel_off (byte offset inside postings)
  Postings (sum(post_cnt) * 4B, <HBB):
    chunk_id, tf, pad

Usage:
  .venv\\Scripts\\python.exe tools\\build_rag_index.py
"""

import bisect
import json
import math
import re
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPECIES_CAT_FILES = (
    ROOT / "src" / "zoo" / "categories" / "land.h",
    ROOT / "src" / "zoo" / "categories" / "sky.h",
    ROOT / "src" / "zoo" / "categories" / "insect.h",
    ROOT / "src" / "zoo" / "categories" / "water.h",
    ROOT / "src" / "zoo" / "categories" / "reptile.h",
    ROOT / "src" / "zoo" / "categories" / "karimono.h",
)
STUDY_DECK_DIR = ROOT / "src" / "learn" / "decks"
OUT_BIN = ROOT / "data" / "rag" / "kb.bin"
OUT_JSONL = ROOT / "llm_training" / "data" / "kb_zoogotchi.jsonl"

CAT_SPECIES = 0
CAT_DSA = 1
CAT_JLPT = 2
CAT_GENETICS = 3
CAT_CARE = 4

CHUNK_FMT = "<HBB40s36s160s"
CHUNK_SIZE = struct.calcsize(CHUNK_FMT)  # 240 bytes

STOPWORDS = {
    "a", "an", "and", "are", "as", "at", "be", "by", "do", "for", "from", "has",
    "have", "in", "is", "it", "its", "of", "on", "or", "that", "the", "this",
    "to", "was", "were", "with", "you", "your",
}

_FURIGANA_PAREN_RE = re.compile(r"\(([ぁ-んー]+)\)")
_JP_SEQ_RE = re.compile(r"[\u3041-\u3096\u30a1-\u30fa\u30fc\u4e00-\u9fff]+")


def fnv1a_32(tok: str) -> int:
    h = 2166136261
    for b in tok.lower().encode("utf-8"):
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def _is_kanji(ch: str) -> bool:
    return 0x4E00 <= ord(ch) <= 0x9FFF


def extract_jp_tokens(text: str) -> list[str]:
    out = []
    stripped = _FURIGANA_PAREN_RE.sub("", text)
    for seq in _JP_SEQ_RE.findall(stripped):
        for idx, ch in enumerate(seq):
            if _is_kanji(ch):
                out.append(ch)
            if idx + 1 < len(seq):
                out.append(seq[idx : idx + 2])
    for furi in _FURIGANA_PAREN_RE.findall(text):
        for idx in range(len(furi) - 1):
            out.append(furi[idx : idx + 2])
    return out


def tokenize_bm25(text: str) -> list[str]:
    toks = [
        m.lower()
        for m in re.findall(r"[A-Za-z0-9]+", text)
        if len(m) >= 2 and m.lower() not in STOPWORDS
    ]
    toks.extend(extract_jp_tokens(text))
    return toks


def _clean_utf8(s: str, max_bytes: int) -> str:
    s = re.sub(r"[\x00-\x1f\x7f]+", " ", s)
    s = re.sub(r"\s+", " ", s).strip()
    b = s.encode("utf-8")
    if len(b) <= max_bytes:
        return s
    lim = max(0, max_bytes - 3)
    cut = b[:lim]
    while cut:
        try:
            return cut.decode("utf-8").rstrip() + "..."
        except UnicodeDecodeError:
            cut = cut[:-1]
    return ""


def extract_species_chunks() -> list[dict]:
    src = "\n".join(p.read_text(encoding="utf-8") for p in SPECIES_CAT_FILES)
    pat = re.compile(
        r'\{\s*(\d+),\s*AnimalCategory::(\w+),\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*'
        r'"([^"]+)",\s*CircadianType::(\w+),\s*\{([^}]+)\},\s*\{[^}]+\},\s*(\d+),\s*'
        r'"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*0x[0-9A-Fa-f]+,\s*0x[0-9A-Fa-f]+,\s*0x[0-9A-Fa-f]+,\s*'
        r'\{\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)"\s*\},\s*(true|false)',
        re.DOTALL,
    )
    chunks = []
    for m in pat.finditer(src):
        (_sid, cat_enum, slug, name, sci, biome, circ, _stages, lifespan,
         food, treat, care, fact0, fact1, fact2, is_kari) = m.groups()
        is_kari_b = is_kari == "true"
        cat_label = cat_enum.title()
        life_str = "Cross-breed lineage (no old-age permadeath)" if is_kari_b else f"~{lifespan}h natural lifespan (permadeath)"
        c1_snip = (
            f"{name} ({sci}), {cat_label}/{circ.title()} in {biome}. "
            f"Food: {food}, treat: {treat}, care: {care}. {life_str}."
        )
        chunks.append({
            "cat": CAT_GENETICS if is_kari_b else CAT_SPECIES,
            "title": _clean_utf8(f"{name} Care", 39),
            "tag": _clean_utf8(f"species:{slug}", 35),
            "snippet": _clean_utf8(c1_snip, 159),
            "keywords": f"{slug.replace('_', ' ')} {name} {sci} {biome} {food} {treat} {care} {cat_label} {circ}",
        })
        c2_snip = f"{name} fun facts: {fact0} {fact1}"
        chunks.append({
            "cat": CAT_SPECIES,
            "title": _clean_utf8(f"{name} Biology", 39),
            "tag": _clean_utf8(f"species:{slug}", 35),
            "snippet": _clean_utf8(c2_snip, 159),
            "keywords": f"{slug.replace('_', ' ')} {name} {fact2}",
        })
    assert len(chunks) == 36, f"Expected 36 species chunks, got {len(chunks)}"
    return chunks


def extract_study_chunks() -> list[dict]:
    card_pat = re.compile(
        r'\{\s*"([^"]+)",\s*\{"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)"\},\s*'
        r'([0-3]),\s*"([^"]+)",\s*"([^"]+)"\s*\}',
        re.DOTALL,
    )
    deck_map = [
        ("kDeckDsaPatterns", "dsa_patterns.h", CAT_DSA, 16),
        ("kDeckDsaBigO", "dsa_bigo.h", CAT_DSA, 10),
        ("kDeckLangJp", "japanese_jlpt.h", CAT_JLPT, 24),
        ("kDeckZooBio", "zoo_bio.h", CAT_SPECIES, 14),
    ]
    chunks = []
    for arr_name, header_name, cat, limit in deck_map:
        src = (STUDY_DECK_DIR / header_name).read_text(encoding="utf-8")
        block = src.split(f"static const StudyCard {arr_name}[] = {{")[1].split("};")[0]
        cards = card_pat.findall(block)[:limit]
        for prompt, c0, c1, c2, c3, ans_s, takeaway, tag in cards:
            ans = (c0, c1, c2, c3)[int(ans_s)]
            snip = _clean_utf8(f"Answer: {ans}. {takeaway}", 159)
            extra = "jlpt japanese " if arr_name == "kDeckLangJp" else ""
            chunks.append({
                "cat": cat,
                "title": _clean_utf8(tag, 39),
                "tag": _clean_utf8(tag, 35),
                "snippet": snip,
                "keywords": f"{extra}{prompt} {ans} {takeaway} {tag}",
            })
    return chunks


def static_care_and_genetics_chunks() -> list[dict]:
    entries = [
        (CAT_GENETICS, "Karimono Cross-Breeding", "Karimono Breed",
         "Raise a Karimono to Adult stage, then cross-breed via Plaza Spirit or 12-char Base32+CRC5 Genome Code from your phone.",
         "karimono breeding genetics lineage genome code crc5 base32 adult"),
        (CAT_GENETICS, "Karimono Mutations", "Mutations",
         "Each visual gene blends Mendelian 50/50 traits plus ~8% fresh mutation roll for Star Glow, Crystal Wings, or Prism.",
         "karimono mutation star glow crystal wing prism mendelian genes"),
        (CAT_CARE, "Manatee Habitat Guide", "species:manatee",
         "West Indian Manatee lives in Spring Lagoon (cathemeral), loves warm water above 20C, Seagrass, and Crisp Romaine treats.",
         "manatee seagrass romaine spring lagoon water temp 20c care"),
        (CAT_CARE, "Sleep & Circadian Rhythm", "Circadian Care",
         "Diurnal pets rest 21-06h, Nocturnal 08-19h, Crepuscular midday/night, Cathemeral short naps. Rock IMU for lullaby!",
         "sleep circadian nocturnal diurnal crepuscular cathemeral lullaby"),
        (CAT_CARE, "Hardware Battery & RTC", "Hardware Safety",
         "Verify MX1.25 3.7V LiPo +/- polarity against PCB silkscreen and only fit a rechargeable coin cell (ML414/MS621FE) in RTC.",
         "battery lipo mx1.25 polarity rtc coin cell rechargeable ml414 safety"),
        (CAT_CARE, "Phone Web & Study Sync", "SoftAP Web UI",
         "Double-press PWR for ZooGotchi-S3 Wi-Fi (192.168.4.1) to chat, upload custom cards, sync RTC, or search on-device RAG.",
         "wifi softap web companion pwr double press rtc sync custom deck"),
        (CAT_CARE, "Focus Timer & Protege", "Protege Effect",
         "Finish 15/25/35m Focus sprints or Quizzes to earn Zoo Coins, unlock Scholar Glasses, and teach terms to your pet's memory.",
         "focus pomodoro timer protege lastlearned study quiz coins glasses"),
        (CAT_CARE, "AI Safety Architecture", "3-Layer Guardrails",
         "On-chip PLE outputs strictly from 1,421 vetted V_out tokens backed by input sanitization, output check, and RAG grounding.",
         "safety guardrails vocab v_out ple micro llm injection protection"),
    ]
    return [
        {
            "cat": cat,
            "title": _clean_utf8(title, 39),
            "tag": _clean_utf8(tag, 35),
            "snippet": _clean_utf8(snip, 159),
            "keywords": kw,
        }
        for cat, title, tag, snip, kw in entries
    ]


def build_index():
    chunks = extract_species_chunks() + extract_study_chunks() + static_care_and_genetics_chunks()
    n_chunks = len(chunks)

    # Deduplicate/assign IDs and index BM25 tokens
    postings_by_hash: dict[int, dict[int, int]] = {}
    doc_lens = []
    jsonl_rows = []
    for cid, ch in enumerate(chunks):
        full_text = f"{ch['title']} {ch['tag']} {ch['snippet']} {ch['keywords']}"
        toks = tokenize_bm25(full_text)
        doc_len = max(1, min(255, len(toks)))
        doc_lens.append(doc_len)
        tf_map: dict[int, int] = {}
        for t in toks:
            h = fnv1a_32(t)
            tf_map[h] = min(255, tf_map.get(h, 0) + 1)
        for h, tf in tf_map.items():
            postings_by_hash.setdefault(h, {})[cid] = tf

        ch["id"] = cid
        ch["doc_len"] = doc_len
        cat_names = ["species", "dsa", "jlpt", "genetics", "care"]
        jsonl_rows.append({
            "doc_id": f"ZOO-{cid:03d}",
            "category": cat_names[ch["cat"]],
            "title": ch["title"],
            "tag": ch["tag"],
            "text": ch["snippet"],
            "search_text": _clean_utf8(full_text, 480),
        })

    avg_dl = sum(doc_lens) / float(max(1, n_chunks))
    avg_dl_x100 = max(100, min(65535, int(round(avg_dl * 100.0))))

    chunk_bytes = bytearray()
    for ch in chunks:
        chunk_bytes.extend(
            struct.pack(
                CHUNK_FMT,
                ch["id"],
                ch["cat"],
                ch["doc_len"],
                ch["title"].encode("utf-8")[:39],
                ch["tag"].encode("utf-8")[:35],
                ch["snippet"].encode("utf-8")[:159],
            )
        )
    assert len(chunk_bytes) == n_chunks * CHUNK_SIZE

    sorted_hashes = sorted(postings_by_hash.keys())
    term_bytes = bytearray()
    post_bytes = bytearray()

    for h in sorted_hashes:
        plist = sorted(postings_by_hash[h].items())
        df = len(plist)
        idf = math.log((n_chunks - df + 0.5) / (df + 0.5) + 1.0)
        idf_x100 = max(1, min(65535, int(round(idf * 100.0))))
        post_rel_off = len(post_bytes)
        term_bytes.extend(struct.pack("<IHHI", h, idf_x100, df, post_rel_off))
        for cid, tf in plist:
            post_bytes.extend(struct.pack("<HBB", cid, tf, 0))

    chunk_off = 36
    term_off = chunk_off + len(chunk_bytes)
    post_off = term_off + len(term_bytes)
    payload = bytes(chunk_bytes) + bytes(term_bytes) + bytes(post_bytes)
    total_bytes = 36 + len(payload)
    assert total_bytes <= 64 * 1024, f"ZRAG exceeded 64KB budget: {total_bytes}"
    crc = zlib.crc32(payload) & 0xFFFFFFFF

    hdr = struct.pack(
        "<4sI4H5I",
        b"ZRAG",
        1,
        n_chunks,
        len(sorted_hashes),
        avg_dl_x100,
        0,
        chunk_off,
        term_off,
        post_off,
        total_bytes,
        crc,
    )
    assert len(hdr) == 36

    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    OUT_BIN.write_bytes(hdr + payload)

    OUT_JSONL.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT_JSONL, "w", encoding="utf-8", newline="\n") as f:
        for row in jsonl_rows:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")

    print(
        f"Built {OUT_BIN} ({total_bytes:,} bytes < 64 KB) | "
        f"chunks={n_chunks}, terms={len(sorted_hashes)}, avg_dl={avg_dl:.2f}, crc32=0x{crc:08X}"
    )
    print(f"Exported {OUT_JSONL} ({len(jsonl_rows)} docs)")
    smoke_query_bin(OUT_BIN, ["manatee seagrass", "に限って", "jlpt murphy law", "sliding window"])


def smoke_query_bin(bin_path: Path, queries: list[str]):
    raw = bin_path.read_bytes()
    magic, ver, n_c, n_t, avg_dl_x100, _r, c_off, t_off, p_off, tot, crc = struct.unpack("<4sI4H5I", raw[:36])
    assert magic == b"ZRAG" and ver == 1 and len(raw) == tot
    assert (zlib.crc32(raw[36:]) & 0xFFFFFFFF) == crc

    avg_dl = avg_dl_x100 / 100.0
    hashes = [struct.unpack_from("<I", raw, t_off + i * 12)[0] for i in range(n_t)]

    for q in queries:
        scores = [0.0] * n_c
        seen_h = set()
        for tok in tokenize_bm25(q):
            h = fnv1a_32(tok)
            if h in seen_h:
                continue
            seen_h.add(h)
            pos = bisect.bisect_left(hashes, h)
            if pos < n_t and hashes[pos] == h:
                _th, idf_x100, p_cnt, p_rel = struct.unpack_from("<IHHI", raw, t_off + pos * 12)
                idf = idf_x100 / 100.0
                for j in range(p_cnt):
                    cid, tf, _p = struct.unpack_from("<HBB", raw, p_off + p_rel + j * 4)
                    dl = raw[c_off + cid * CHUNK_SIZE + 3]
                    denom = tf + 1.2 * (0.25 + 0.75 * (dl / avg_dl))
                    scores[cid] += idf * ((tf * 2.2) / denom)
        best = max(range(n_c), key=lambda i: scores[i])
        _id, _cat, _dl, title_b, tag_b, snip_b = struct.unpack_from(CHUNK_FMT, raw, c_off + best * CHUNK_SIZE)
        title = title_b.split(b"\x00", 1)[0].decode("utf-8")
        snip = snip_b.split(b"\x00", 1)[0].decode("utf-8")
        assert scores[best] > 1.0, f"Smoke query {q!r} scored low: {scores[best]}"
        print(f"  Smoke [{q}] -> ({scores[best]:.2f}) {title}: {snip}")


if __name__ == "__main__":
    build_index()
