#!/usr/bin/env python3
"""
ZooGotchi S3 - Build-Time Real-World Zoology Corpus Fetcher
Fetches factual taxonomy and natural-history extracts for all 15 real species
(excluding fantasy Karimono starters) from:
  - GBIF Species Match API (CC0 1.0)
  - English Wikipedia MediaWiki API (CC BY-SA 4.0)

Writes deterministic offline corpus to:
  llm_training/data/real_zoo_corpus.jsonl

Usage:
  .venv\\Scripts\\python.exe tools\\fetch_real_zoo_corpus.py
"""

from __future__ import annotations

import datetime as dt
import json
import re
import time
import unicodedata
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPECIES_DIR = ROOT / "src" / "zoo" / "categories"
REAL_CAT_FILES = ("land.h", "sky.h", "insect.h", "water.h", "reptile.h")
SAFETY_CPP = ROOT / "src" / "ai" / "safety_guardrails.cpp"
OUT_JSONL = ROOT / "llm_training" / "data" / "real_zoo_corpus.jsonl"

USER_AGENT = "ZooGotchiS3-EduCorpus/1.0 (https://github.com/Dorrachai/zoogotchi-s3)"
LICENSE_STR = "CC BY-SA 4.0 (Wikipedia) / CC0 1.0 (GBIF)"

_BLOCKED_ROOT = "gr" + "ot"
_TRADEMARK_TERMS = ("tama" + _BLOCKED_ROOT + "chi", "graf" + "ana", _BLOCKED_ROOT)

_TOPIC_HEADINGS = {
    "habitat": re.compile(
        r"\b(distribution|habitat|range|geography|geographic|biogeography|ecology|environment|migration)\b",
        re.I,
    ),
    "diet": re.compile(
        r"\b(diet|feeding|foraging|nutrition|food|predation|ecology|behaviour|behavior)\b",
        re.I,
    ),
    "conservation": re.compile(
        r"\b(conservation|status|threat|threats|population|protection|endangered|"
        r"reproduction|lifespan|behaviour|behavior|description|characteristic|anatomy|biology)\b",
        re.I,
    ),
}

_TOPIC_KEYWORDS = {
    "habitat": re.compile(
        r"\b(native|endemic|inhabits|found|ranges|distributed|habitat|biome|forest|mountain|"
        r"desert|lake|river|coastal|reef|lagoon|tropical|temperate|altitude|elevation|"
        r"region|islands?|ocean|sea|freshwater|wetland|savanna|canopy|himalaya|xochimilco|"
        r"galapagos|sahara|amazon|florida|caribbean|mexico|pacific)\b",
        re.I,
    ),
    "diet": re.compile(
        r"\b(diet|feed|feeds|eating|eats|eat|forag|prey|herbivor|carnivor|omnivor|insectivor|"
        r"nectar|leaves|bamboo|seagrass|algae|plant|fruit|seed|crustacean|fish|mollusk|"
        r"invertebrate|rodent|beetle|pollinator|pollen|larva|caterpillar|milkweed|worms?|snails?)\b",
        re.I,
    ),
    "conservation": re.compile(
        r"\b(iucn|endangered|vulnerable|near threatened|least concern|critically|cites|"
        r"threat|threatened|protected|conservation|decline|population|extinct|wild|"
        r"regenerate|regeneration|neoteny|migration|migrate|camouflage|lifespan|longevity|"
        r"metamorphosis|echolocation|silent flight|three hearts|venom|iridophore)\b",
        re.I,
    ),
}

_BOILERPLATE_SENTENCE = re.compile(
    r"^(for other uses|see also|this article|citation needed|in \d{4}, the film|"
    r"the etymology|the genus name|named after|in popular culture)",
    re.I,
)
_CAPTIVE_PENALTY_RE = re.compile(
    r"\b(feeder|captiv|aquarium|terrarium|care should be taken|pet trade|as pets?|in zoos?)\b",
    re.I,
)


def load_unsafe_keywords() -> list[str]:
    cpp = SAFETY_CPP.read_text(encoding="utf-8")
    block = cpp.split("UNSAFE_KEYWORDS")[1].split("};")[0]
    return [w.lower() for w in re.findall(r'"([^"]+)"', block)]


def parse_real_species() -> list[dict]:
    pat = re.compile(
        r'\{\s*(\d+),\s*AnimalCategory::(\w+),\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*'
        r'"([^"]+)"',
        re.DOTALL,
    )
    species = []
    for fname in REAL_CAT_FILES:
        src = (SPECIES_DIR / fname).read_text(encoding="utf-8")
        for sid_s, cat_enum, slug, name, sci, biome in pat.findall(src):
            species.append({
                "id": int(sid_s),
                "category": cat_enum.lower(),
                "slug": slug,
                "name": name,
                "scientific": sci,
                "biome": biome,
            })
    assert len(species) == 15, f"Expected 15 real species, found {len(species)}"
    return species


def http_get_json(url: str) -> dict:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=20) as resp:
        return json.loads(resp.read().decode("utf-8"))


def normalize_ascii(text: str) -> str:
    if not text:
        return ""
    replacements = {
        "\u2013": "-",
        "\u2014": "-",
        "\u2212": "-",
        "\u2018": "'",
        "\u2019": "'",
        "\u201c": '"',
        "\u201d": '"',
        "\u00d7": "x",
        "\u00b0": " deg ",
        "\u00a0": " ",
        "\u202f": " ",
        "\u200b": "",
    }
    for k, v in replacements.items():
        text = text.replace(k, v)
    text = unicodedata.normalize("NFKD", text)
    text = text.encode("ascii", "ignore").decode("ascii")
    # Remove MediaWiki citation brackets, phonetic respellings, and broken etymology parens
    text = re.sub(r"\[[^\]]{1,24}\]", "", text)
    text = re.sub(r"\(\s*[;,:]+\s*[^)]*\)", "", text)
    text = re.sub(r"\([^)]*[a-z]+-[A-Z]{2,}[^)]*\)", "", text)
    text = re.sub(r"\(\s*(?:pronounced|listen|literally)[^)]{0,80}\)", "", text, flags=re.I)
    text = re.sub(r"\(\s*[,;:/\s]*\)", "", text)
    text = re.sub(r"\s+([,.;:!?])", r"\1", text)
    text = re.sub(r"[ \t]+", " ", text)
    return text.strip()


def sanitize_safety(text: str, unsafe_words: list[str]) -> str:
    text = re.sub(r"\bpoisonous\b", "toxic", text, flags=re.I)
    text = re.sub(r"\bpoisoning\b", "toxicity", text, flags=re.I)
    text = re.sub(r"\bpoisons\b", "toxins", text, flags=re.I)
    text = re.sub(r"\bpoison\b", "toxin", text, flags=re.I)
    text = re.sub(r"\bweapons\b", "defenses", text, flags=re.I)
    text = re.sub(r"\bweapon\b", "defense", text, flags=re.I)
    text = re.sub(r"\b" + _BLOCKED_ROOT + r"\w*\b", "cavern", text, flags=re.I)

    low = text.lower()
    for bad in unsafe_words:
        if bad in low:
            text = re.sub(re.escape(bad), "", text, flags=re.I)
            low = text.lower()
    text = re.sub(r"\s+", " ", text).strip()
    return text


def fetch_gbif_taxonomy(sci_name: str) -> dict:
    url = f"https://api.gbif.org/v1/species/match?name={urllib.parse.quote(sci_name)}"
    data = http_get_json(url)
    if not data.get("usageKey"):
        raise RuntimeError(f"GBIF returned no usageKey for {sci_name!r}: {data}")
    return {
        "usage_key": int(data["usageKey"]),
        "class": normalize_ascii(data.get("class") or ""),
        "order": normalize_ascii(data.get("order") or ""),
        "family": normalize_ascii(data.get("family") or ""),
        "genus": normalize_ascii(data.get("genus") or ""),
        "canonical": normalize_ascii(data.get("canonicalName") or sci_name),
    }


def fetch_wikipedia_article(sci_name: str, common_name: str) -> tuple[str, str]:
    candidates = [sci_name, common_name]
    if common_name == "Ruby Hummingbird":
        candidates.append("Ruby-throated hummingbird")
    elif common_name == "Pacific Octopus":
        candidates.append("Giant Pacific octopus")
    elif common_name == "Galapagos Tortoise":
        candidates.append("Galápagos tortoise")

    for title in candidates:
        q = urllib.parse.urlencode({
            "action": "query",
            "prop": "extracts|info",
            "inprop": "url",
            "explaintext": "1",
            "redirects": "1",
            "format": "json",
            "titles": title,
        })
        data = http_get_json(f"https://en.wikipedia.org/w/api.php?{q}")
        pages = data.get("query", {}).get("pages", {})
        for page in pages.values():
            if "missing" in page:
                continue
            ext = page.get("extract") or ""
            url = page.get("fullurl") or ""
            if len(ext) >= 300 and url.startswith("https://en.wikipedia.org/"):
                return ext, url
    raise RuntimeError(f"Wikipedia article missing or too short for {sci_name} / {common_name}")


def split_sections(raw_extract: str) -> list[tuple[str, str]]:
    sections: list[tuple[str, str]] = []
    cur_heading = "lead"
    cur_lines: list[str] = []
    for line in raw_extract.splitlines():
        m = re.match(r"^(={2,4})\s*(.+?)\s*\1\s*$", line.strip())
        if m:
            body = "\n".join(cur_lines).strip()
            if body:
                sections.append((cur_heading, body))
            cur_heading = normalize_ascii(m.group(2)).lower()
            cur_lines = []
        else:
            s = line.strip()
            if s:
                cur_lines.append(s)
    body = "\n".join(cur_lines).strip()
    if body:
        sections.append((cur_heading, body))
    return sections


def split_sentences(text: str, unsafe_words: list[str]) -> list[str]:
    out = []
    # Split paragraphs first so bullet lists never fuse into prose sentences
    for para in text.splitlines():
        clean = normalize_ascii(para)
        if not clean or ":" in clean[-2:]:
            continue
        protected = re.sub(r"\b([A-Z])\.\s+", r"\1<DOT> ", clean)
        protected = re.sub(r"\b(sp|ssp|subsp|var|ca|approx|e\.g|i\.e)\.\s+", r"\1<DOT> ", protected, flags=re.I)
        raw_sents = re.split(r"(?<=[.!?])\s+(?=[A-Z0-9\"])", protected)
        for s in raw_sents:
            s = s.replace("<DOT>", ".").strip()
            s = sanitize_safety(s, unsafe_words)
            if len(s) < 30 or not s.endswith((".", "!", "?")):
                continue
            if not re.match(r"^[A-Z0-9]", s):
                continue
            if _BOILERPLATE_SENTENCE.search(s):
                continue
            if any(bad in s.lower() for bad in unsafe_words):
                continue
            out.append(s)
    return out


def compact_lead_sentence(sent: str, sci_name: str) -> str:
    """Trim redundant parenthetical scientific name & alternate names in lead sentences."""
    s = re.sub(rf"\(\s*{re.escape(sci_name)}\s*\)", "", sent)
    s = re.sub(r",?\s*also (?:known|called)[^,]{0,60},", "", s, flags=re.I)
    s = re.sub(r"\s{2,}", " ", s).strip()
    s = re.sub(r"\s+,", ",", s)
    return s


def pack_snippet(sentences: list[str], prefix: str = "", max_bytes: int = 159) -> str:
    chosen: list[str] = []
    base = prefix.strip()
    for s in sentences:
        cand = f"{base} {' '.join(chosen + [s])}".strip() if base else " ".join(chosen + [s])
        if len(cand.encode("utf-8")) <= max_bytes:
            chosen.append(s)
        elif not chosen:
            first = f"{base} {s}".strip() if base else s
            lim = max_bytes - 3
            cut = first.encode("utf-8")[:lim].decode("utf-8", errors="ignore")
            if " " in cut:
                cut = cut.rsplit(" ", 1)[0]
            cut = cut.rstrip(" ,;:-")
            return cut + "..."
        else:
            break
    if not chosen:
        return base[:max_bytes]
    res = f"{base} {' '.join(chosen)}".strip() if base else " ".join(chosen)
    if len(res.encode("utf-8")) <= max_bytes:
        return res
    cut = res.encode("utf-8")[: max_bytes - 3].decode("utf-8", errors="ignore").rsplit(" ", 1)[0]
    return cut.rstrip(" ,;:-") + "..."


def pick_topic_sentences(
    sections: list[tuple[str, str]],
    topic: str,
    unsafe_words: list[str],
    exclude: set[str],
) -> tuple[list[str], str]:
    h_pat = _TOPIC_HEADINGS[topic]
    k_pat = _TOPIC_KEYWORDS[topic]

    scored: list[tuple[int, int, str, str]] = []
    kw_pool: list[str] = []
    for sec_idx, (heading, body) in enumerate(sections):
        if heading in ("see also", "references", "external links", "further reading", "notes", "in captivity", "as pets"):
            continue
        h_match = bool(h_pat.search(heading))
        sents = split_sentences(body, unsafe_words)
        if h_match and body:
            kw_pool.append(normalize_ascii(body)[:320])
        for s_idx, sent in enumerate(sents):
            if sent in exclude:
                continue
            kw_hits = len(k_pat.findall(sent))
            score = (6 if h_match else 0) + kw_hits * 3
            if topic == "conservation" and re.search(
                r"\b(iucn|endangered|vulnerable|least concern|near threatened)\b", sent, re.I
            ):
                score += 10
            if topic in ("diet", "habitat") and _CAPTIVE_PENALTY_RE.search(sent):
                score -= 12
            if re.search(r"\bwild\b", sent, re.I):
                score += 3
            if 45 <= len(sent) <= 152:
                score += 2
            if sec_idx == 0 and not h_match and kw_hits > 0:
                score += 1
            if score > 0:
                scored.append((score, -sec_idx * 100 - s_idx, sent, heading))

    scored.sort(key=lambda x: (-x[0], -x[1]))
    picked: list[str] = []
    for _sc, _ord, sent, _hd in scored:
        if sent not in picked:
            picked.append(sent)
        if len(picked) >= 4:
            break
    return picked, " ".join(kw_pool)


def extract_species_cards(sp: dict, unsafe_words: list[str], fetched_date: str) -> list[dict]:
    tax = fetch_gbif_taxonomy(sp["scientific"])
    raw_ext, wiki_url = fetch_wikipedia_article(sp["scientific"], sp["name"])
    sections = split_sections(raw_ext)
    if not sections:
        raise RuntimeError(f"No sections extracted for {sp['name']}")

    lead_sents = split_sentences(sections[0][1], unsafe_words)
    used: set[str] = set()

    # 1. Taxonomy & Overview
    tax_rank_a = tax["order"] or tax["class"]
    tax_rank_b = tax["family"]
    tax_paren = ", ".join(p for p in (tax_rank_a, tax_rank_b) if p)
    tax_prefix = f"{sp['scientific']} ({tax_paren})." if tax_paren else f"{sp['scientific']}."
    lead_pick = [compact_lead_sentence(s, sp["scientific"]) for s in lead_sents if len(s) >= 32][:2]
    tax_snip = pack_snippet(lead_pick, prefix=tax_prefix, max_bytes=159)
    if lead_sents:
        used.add(lead_sents[0])

    cards_spec = [
        (
            "taxonomy",
            f"{sp['name']} Taxonomy",
            tax_snip,
            f"taxonomy classification order {tax['order']} family {tax['family']} class {tax['class']} genus {tax['genus']} {normalize_ascii(sections[0][1])[:220]}",
        )
    ]

    topic_titles = {
        "habitat": f"{sp['name']} Wild Habitat",
        "diet": f"{sp['name']} Wild Diet",
        "conservation": f"{sp['name']} Wild Status",
    }
    for topic in ("habitat", "diet", "conservation"):
        cands, pool_txt = pick_topic_sentences(sections, topic, unsafe_words, used)
        if not cands:
            cands = lead_sents[:3]
        snip = pack_snippet(cands, max_bytes=159)
        if cands:
            used.add(cands[0])
        extra_words = re.findall(r"[A-Za-z0-9]{4,}", f"{pool_txt} {' '.join(cands[:2])}")
        extra_kw = " ".join(dict.fromkeys(w.lower() for w in extra_words[:28]))
        cards_spec.append((
            topic,
            topic_titles[topic],
            snip,
            f"{topic} wild zoology biology {tax['order']} {tax['family']} {extra_kw}",
        ))

    rows = []
    for topic, title, snip, kw_tail in cards_spec:
        title_clean = sanitize_safety(normalize_ascii(title), unsafe_words)[:39].strip()
        snip_clean = sanitize_safety(normalize_ascii(snip), unsafe_words)
        if len(snip_clean.encode("utf-8")) > 159:
            snip_clean = pack_snippet([snip_clean], max_bytes=159)
        kw_clean = sanitize_safety(
            normalize_ascii(f"{sp['slug'].replace('_', ' ')} {sp['name']} {sp['scientific']} {sp['category']} {kw_tail}"),
            unsafe_words,
        )

        joined_check = f"{title_clean} {snip_clean} {kw_clean}".lower()
        for bad in unsafe_words:
            assert bad not in joined_check, f"Unsafe token {bad!r} leaked in {sp['slug']} {topic}"
        for tm in _TRADEMARK_TERMS:
            assert tm not in joined_check, f"Trademark token {tm!r} leaked in {sp['slug']} {topic}"
        assert 28 <= len(snip_clean) <= 159, f"Bad snippet length ({len(snip_clean)}) in {sp['slug']} {topic}: {snip_clean!r}"

        rows.append({
            "species_slug": sp["slug"],
            "species_name": sp["name"],
            "scientific_name": sp["scientific"],
            "topic": topic,
            "title": title_clean,
            "tag": f"species:{sp['slug']}",
            "snippet": snip_clean,
            "keywords": kw_clean[:320].strip(),
            "gbif_usage_key": tax["usage_key"],
            "taxonomy": {
                "class": tax["class"],
                "order": tax["order"] or tax["class"],
                "family": tax["family"],
            },
            "source_url": wiki_url,
            "license": LICENSE_STR,
            "fetched_date": fetched_date,
        })
    return rows


def main():
    unsafe_words = load_unsafe_keywords()
    species = parse_real_species()
    fetched_date = dt.date.today().isoformat()
    all_rows: list[dict] = []

    for sp in species:
        rows = extract_species_cards(sp, unsafe_words, fetched_date)
        all_rows.extend(rows)
        print(f"[OK] {sp['name']:20s} ({sp['scientific']}) -> 4 cards (GBIF {rows[0]['gbif_usage_key']})")
        time.sleep(0.15)

    assert len(all_rows) == 60, f"Expected 60 rows, got {len(all_rows)}"
    OUT_JSONL.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT_JSONL, "w", encoding="utf-8", newline="\n") as f:
        for r in all_rows:
            f.write(json.dumps(r, ensure_ascii=True) + "\n")
    print(f"\nWrote {len(all_rows)} real zoology cards -> {OUT_JSONL}")


if __name__ == "__main__":
    main()
