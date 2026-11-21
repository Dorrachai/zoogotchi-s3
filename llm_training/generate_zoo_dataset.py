#!/usr/bin/env python3
"""
ZooGotchi S3 — Synthetic Wholesome Zoo & Karimono Dataset Generator
Aligned with src/ai/personality_memory.cpp prompt tags, src/zoo/species_registry.cpp,
and the modern Google GenAI Interactions API (gemini-3.8-flash).

Usage:
  python llm_training/generate_zoo_dataset.py --dry-run
  python llm_training/generate_zoo_dataset.py --live --per-species 16
  GEMINI_API_KEY=... python llm_training/generate_zoo_dataset.py --submit-batch --per-species 64
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "llm_training" / "data"
VOUT_PATH = ROOT / "llm_training" / "vocab_out.json"

sys.path.insert(0, str(ROOT / "llm_training"))
import verify_vocab_safety  # noqa: E402


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


# Exact C++ species_registry.cpp alignment:
# (slug, name, category, stages[5], primary_food, treat_food, biome_tag, env_care, action_verb)
SPECIES_SPECS = [
    ("red_panda", "Red Panda", "Land", ["Newborn", "Cub", "Subadult", "Adult", "Elder"], "bamboo", "apple", "bamboo forest", "forest clean", "climbs"),
    ("fennec_fox", "Fennec Fox", "Land", ["Newborn", "Kit", "Juvenile", "Adult", "Elder"], "cricket", "date", "warm dunes", "sand clean", "hops"),
    ("capybara", "Capybara", "Land", ["Newborn", "Pup", "Juvenile", "Adult", "Elder"], "grass", "watermelon", "river marsh", "bath", "floats"),
    ("barn_owl", "Barn Owl", "Sky", ["Egg", "Owlet", "Fledgling", "Adult", "Elder"], "berry", "berry", "moonlit meadow", "nest clean", "glides"),
    ("scarlet_macaw", "Scarlet Macaw", "Sky", ["Egg", "Chick", "Fledgling", "Adult", "Elder"], "nut", "mango", "green canopy", "branch clean", "chirps"),
    ("hummingbird", "Ruby Hummingbird", "Sky", ["Egg", "Chick", "Fledgling", "Adult", "Elder"], "nectar", "dew", "flower glade", "mist", "hums"),
    ("monarch", "Monarch Butterfly", "Insect", ["Egg", "Caterpillar", "Chrysalis", "Butterfly", "Elder"], "leaf", "nectar", "flower meadow", "mist", "flutters"),
    ("hercules_beetle", "Hercules Beetle", "Insect", ["Egg", "Grub", "Pupa", "Horned Adult", "Elder"], "moss", "banana", "cloud forest", "humidity", "crawls"),
    ("orchid_mantis", "Orchid Mantis", "Insect", ["Ootheca", "Nymph", "Subadult", "Adult", "Elder"], "honey", "nectar", "orchid garden", "mist", "dances"),
    ("manatee", "West Indian Manatee", "Water", ["Newborn", "Calf", "Juvenile", "Adult", "Elder"], "seagrass", "romaine", "warm lagoon", "water warmth", "paddles"),
    ("axolotl", "Axolotl", "Water", ["Egg", "Larva", "Juvenile", "Adult", "Elder"], "pellet", "worm", "cool lake", "water quality", "swims"),
    ("octopus", "Pacific Octopus", "Water", ["Egg", "Paralarva", "Juvenile", "Adult", "Elder"], "crab", "clam", "kelp reef", "salinity", "solves"),
    ("tortoise", "Galapagos Tortoise", "Reptile", ["Egg", "Hatchling", "Juvenile", "Adult", "Ancient"], "cactus", "dandelion", "sunny meadow", "basking warmth", "strolls"),
    ("chameleon", "Veiled Chameleon", "Reptile", ["Egg", "Hatchling", "Juvenile", "Adult", "Elder"], "cricket", "collard", "misty leaves", "leaf mist", "blinks"),
    ("leopard_gecko", "Leopard Gecko", "Reptile", ["Egg", "Hatchling", "Juvenile", "Adult", "Elder"], "worm", "honey", "warm rock", "warm stone", "wiggles"),
    ("hoshi_karimono", "Hoshi Karimono", "Karimono", ["Egg", "Sprout", "Youth", "Adult", "Grand"], "rice", "comet", "starlight shrine", "starlight glow", "sparkles"),
    ("kumo_karimono", "Kumo Karimono", "Karimono", ["Egg", "Puff", "Breeze", "Adult", "Grand"], "honey", "mochi", "cloud garden", "dew mist", "bounces"),
    ("kuro_karimono", "Kuro Karimono", "Karimono", ["Egg", "Spark", "Flame", "Adult", "Grand"], "bean", "spice", "ember hearth", "warm care", "grins"),
]

SYSTEM_INSTRUCTION = (
    "You generate ultra-wholesome, kid-safe, charming 1-to-2 sentence virtual pet "
    "dialogue lines for ZooGotchi S3. Every reply must reflect the species' authentic "
    "biology or Karimono genetics, current mood, and personality trait using simple, "
    "warm vocabulary. Never include violence, profanity, medical advice, or unsafe topics."
)

PROMPTS_BY_INTENT = [
    ("greet", "Hi buddy! How are you feeling?"),
    ("snack", "Want a fresh snack?"),
    ("fact", "Tell me a fun fact!"),
    ("pet", "Here is a gentle pet and hug!"),
    ("walk", "How were our steps today?"),
    ("study", "Ready for a quick study quiz?"),
    ("lineage", "Tell me about your family heritage!"),
    ("sleep", "Time for a cozy rest tonight?"),
]

LEARNED_TOPICS = [
    "Two Pointers",
    "Sliding Window",
    "Binary Search",
    "[N4] ~te shimau",
    "[N3] ~koto ni naru",
    "[N2] ~ni kagitte",
    "[N2] ~wake ni wa ikanai",
    "[N1] ~ni taenai",
]

SAFE_WORD_TOKEN_RE = re.compile(r"\.\.\.|[A-Za-z0-9'-]+|[.,!?:;()]")


def load_vout_list():
    with open(VOUT_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def clamp_to_vout(text: str, vout_low: set[str]) -> str:
    tokens = SAFE_WORD_TOKEN_RE.findall(text)
    kept = [t for t in tokens if (t.lower() in vout_low)]
    out = ""
    for t in kept[:28]:
        if t in {".", ",", "!", "?", ":", ";", "..."}:
            out += t
        elif not out:
            out = t
        else:
            out += " " + t
    return out or "Hello friend!"


def validate_reply_against_vout(reply: str, vout_low: set[str]) -> tuple[bool, list[str]]:
    tokens = SAFE_WORD_TOKEN_RE.findall(reply)
    missing = [t for t in tokens if t.lower() not in vout_low]
    words_only = [t for t in tokens if t not in {".", ",", "!", "?", ":", ";", "..."}]
    low_words = {w.lower() for w in words_only}
    has_bad = any(b in low_words for b in verify_vocab_safety.STRICT_BLOCKLIST)
    return (len(missing) == 0 and len(words_only) >= 4 and not has_bad), missing


def _curated_reply(intent: str, sp: tuple, mood: str, trait: str, tod: str, learned: str, vout_low: set[str]) -> str:
    slug, name, cat, _stages, food, treat, biome, care, _verb = sp
    is_karimono = (cat == "Karimono")

    trait_phrase = {
        "PLAYFUL": "I bounce with joy",
        "CALM": "I rest calm and cozy",
        "SWEET": "I nuzzle you gently",
        "CURIOUS": "I blink with happy eyes",
    }[trait]

    if mood == "Asleep" or (intent == "sleep" and tod == "NIGHT"):
        raw = f"I am {name}! I yawn softly in the {biome} and nap at night... zzz!"
    elif intent == "snack" or mood == "Hungry":
        raw = f"I am {name}! Fresh {food} and a sweet {treat} treat feel great today!"
    elif mood == "Unwell":
        raw = f"I am {name} and I feel tired. Gentle {care} and a warm nap will help me!"
    elif mood == "Dizzy":
        raw = f"I am {name}! I feel a little dizzy in the {biome}. Please rest near me, friend!"
    elif intent == "fact":
        if slug == "manatee":
            raw = "I am West Indian Manatee! I love warm lagoon water and crisp seagrass every day!"
        elif is_karimono:
            raw = f"I am {name}! I shine with bright glow and share sweet family heritage!"
        else:
            raw = f"I am {name}! I thrive here in the peaceful {biome} and love fresh {food}!"
    elif intent == "study":
        raw = f"I am {name}! Remember that smart word we learned? Let's study and learn together!"
    elif intent == "lineage":
        if is_karimono:
            raw = f"I am {name}! My family lineage and bright heritage shine in the {biome}!"
        else:
            raw = f"I am {name}! I am proud to live safely with you, Keeper, in our sanctuary!"
    elif intent == "walk":
        raw = f"I am {name}! That sunny trail walk was pure joy today! Hooray for healthy steps!"
    elif intent == "pet":
        raw = f"I am {name}! {trait_phrase}! Thank you for the gentle hug and sweet care!"
    else:
        raw = f"Hi Keeper! I am {name} in the {biome} this {tod.lower()}, and {trait_phrase}!"

    ok, missing = validate_reply_against_vout(raw, vout_low)
    assert ok, f"Template word missing from V_out in {slug}/{intent}/{mood}: {missing} | raw={raw!r}"
    return raw


def build_batch_jsonl(per_species: int, out_jsonl: Path, vout_low: set[str]):
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    requests = []
    local_samples = []

    moods = ["Calm", "Happy", "Hungry", "Asleep", "Unwell", "Dizzy", "Bored"]
    traits = ["PLAYFUL", "CALM", "SWEET", "CURIOUS"]
    tods = ["MORNING", "DAY", "DUSK", "NIGHT"]
    mems = ["exploring", "snacked", "walked", "played", "studied", "napped"]

    for s_idx, sp in enumerate(SPECIES_SPECS):
        slug, name, cat, stages, food, treat, biome, _care, _verb = sp
        for i in range(per_species):
            stage = stages[i % len(stages)]
            mood = moods[(i + s_idx) % len(moods)]
            trait = traits[(i // 2 + s_idx) % len(traits)]
            tod = tods[(i // 4 + s_idx) % len(tods)]
            mem = mems[(i + 2) % len(mems)]
            intent, u_msg = PROMPTS_BY_INTENT[i % len(PROMPTS_BY_INTENT)]
            learned = LEARNED_TOPICS[(i + s_idx) % len(LEARNED_TOPICS)]

            if intent == "study" or (i % 3 == 0):
                ctx = f"[SP:{slug}] [ST:{stage}] [MD:{mood}] [TR:{trait}] [TM:{tod}] [MEM:{mem}] [LRN:{learned}] {u_msg}"
            else:
                ctx = f"[SP:{slug}] [ST:{stage}] [MD:{mood}] [TR:{trait}] [TM:{tod}] [MEM:{mem}] {u_msg}"

            req_obj = {
                "key": f"{slug}-{i}",
                "request": {
                    "system_instruction": {"parts": [{"text": SYSTEM_INSTRUCTION}]},
                    "contents": [{
                        "role": "user",
                        "parts": [{
                            "text": (
                                f"Species: {name} ({cat}, lives in {biome}, loves {food} and {treat}).\n"
                                f"Input prompt tag: {ctx}\n"
                                "Return strict JSON {\"reply\": \"...\"} under 22 simple kid-safe words."
                            )
                        }]
                    }],
                    "generation_config": {
                        "response_mime_type": "application/json",
                        "max_output_tokens": 64,
                    },
                    "safety_settings": [
                        {"category": "HARM_CATEGORY_HARASSMENT", "threshold": "BLOCK_LOW_AND_ABOVE"},
                        {"category": "HARM_CATEGORY_HATE_SPEECH", "threshold": "BLOCK_LOW_AND_ABOVE"},
                        {"category": "HARM_CATEGORY_SEXUALLY_EXPLICIT", "threshold": "BLOCK_LOW_AND_ABOVE"},
                        {"category": "HARM_CATEGORY_DANGEROUS_CONTENT", "threshold": "BLOCK_LOW_AND_ABOVE"},
                    ],
                },
            }
            requests.append(req_obj)

            reply = _curated_reply(intent, sp, mood, trait, tod, learned, vout_low)
            ok, missing = validate_reply_against_vout(reply, vout_low)
            assert ok, f"Invalid V_out tokens in {slug}-{i}: {missing} ({reply!r})"

            local_samples.append({
                "prompt": ctx,
                "reply": reply,
                "species": slug,
                "category": cat,
                "intent": intent,
            })

    with open(out_jsonl, "w", encoding="utf-8", newline="\n") as f:
        for r in requests:
            f.write(json.dumps(r) + "\n")

    seed_path = OUT_DIR / "zoo_seed_conversations.jsonl"
    with open(seed_path, "w", encoding="utf-8", newline="\n") as f:
        for s in local_samples:
            f.write(json.dumps(s) + "\n")

    return len(requests), seed_path


def run_live_interactions(per_species: int, vout_low: set[str]) -> Path:
    try:
        from google import genai
    except ImportError as exc:
        raise SystemExit("Install google-genai>=2.30.1 to run --live generation.") from exc

    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        raise SystemExit("Set GEMINI_API_KEY (or add it to .env) before running --live.")

    client = genai.Client(api_key=api_key)
    out_path = OUT_DIR / "zoo_seed_conversations.jsonl"
    schema = {
        "type": "object",
        "properties": {"reply": {"type": "string"}},
        "required": ["reply"],
    }
    moods = ["Calm", "Happy", "Hungry", "Asleep"]
    traits = ["PLAYFUL", "CALM", "SWEET", "CURIOUS"]
    rows = []
    for sp in SPECIES_SPECS:
        slug, name, cat, stages, food, treat, biome, _care, _verb = sp
        for i in range(per_species):
            stage = stages[i % len(stages)]
            mood = moods[i % len(moods)]
            trait = traits[i % len(traits)]
            intent, u_msg = PROMPTS_BY_INTENT[i % len(PROMPTS_BY_INTENT)]
            ctx = f"[SP:{slug}] [ST:{stage}] [MD:{mood}] [TR:{trait}] [TM:DAY] [MEM:played] {u_msg}"
            prompt = (
                f"Species: {name} ({cat}, biome={biome}, loves {food}/{treat}).\n"
                f"Context: {ctx}\nReturn 1 short kid-safe reply under 20 simple English words."
            )
            interaction = client.interactions.create(
                model="gemini-3.8-flash",
                system_instruction=SYSTEM_INSTRUCTION,
                input=prompt,
                response_format={
                    "type": "text",
                    "mime_type": "application/json",
                    "schema": schema,
                },
            )
            raw_json = json.loads(interaction.output_text)
            reply = clamp_to_vout(raw_json.get("reply", ""), vout_low)
            ok, _ = validate_reply_against_vout(reply, vout_low)
            if ok:
                rows.append({"prompt": ctx, "reply": reply, "species": slug, "category": cat, "intent": intent})

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    print(f"Generated {len(rows)} live Gemini Interactions replies -> {out_path}")
    return out_path


def submit_gemini_batch(jsonl_path: Path):
    try:
        from google import genai
        from google.genai import types
    except ImportError as exc:
        raise SystemExit("Install google-genai>=2.30.1 to submit live Gemini Batch jobs.") from exc

    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        raise SystemExit("Set GEMINI_API_KEY (or add it to .env) before running --submit-batch.")

    client = genai.Client(api_key=api_key)
    uploaded = client.files.upload(
        file=str(jsonl_path),
        config=types.UploadFileConfig(display_name="zoogotchi-s3-batch", mime_type="jsonl"),
    )
    job = client.batches.create(
        model="gemini-3.8-flash",
        src=uploaded.name,
        config={"display_name": "zoogotchi-s3-dataset"},
    )
    print(f"Submitted Batch Job: {job.name} (state={job.state})")


def main():
    _load_dotenv(ROOT / ".env")
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true", help="Generate & validate V_out-safe offline dataset + batch JSONL")
    ap.add_argument("--live", action="store_true", help="Generate live replies via Gemini Interactions API")
    ap.add_argument("--submit-batch", action="store_true", help="Upload & start Gemini Batch job")
    ap.add_argument("--per-species", type=int, default=64, help="Samples per species (default 64 -> 1,152 total)")
    args = ap.parse_args()

    vout_words = load_vout_list()
    vout_low = {w.lower() for w in vout_words if w not in {"<pad>", "<bos>", "<eos>", "<unk>"}}

    if args.live and not args.dry_run:
        run_live_interactions(args.per_species, vout_low)
        return

    batch_path = OUT_DIR / "batch_requests.jsonl"
    n, seed_path = build_batch_jsonl(args.per_species, batch_path, vout_low)
    print(f"Built {n} Gemini Batch requests across {len(SPECIES_SPECS)} species -> {batch_path}")
    print(f"Built {n} V_out-verified training samples -> {seed_path}")

    if args.submit_batch and not args.dry_run:
        submit_gemini_batch(batch_path)
    else:
        with open(batch_path, "r", encoding="utf-8") as f:
            lines = [json.loads(ln) for ln in f]
        assert len(lines) == len(SPECIES_SPECS) * args.per_species
        with open(seed_path, "r", encoding="utf-8") as f:
            seeds = [json.loads(ln) for ln in f]
        assert len(seeds) == n
        for s in seeds:
            ok, missing = validate_reply_against_vout(s["reply"], vout_low)
            assert ok, f"Seed failed V_out check: {missing} in {s}"
        print(f"DRY-RUN PASS: {n} samples across 18 species, 0 <unk> tokens, 0 unsafe words.")


if __name__ == "__main__":
    main()
