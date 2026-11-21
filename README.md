# ZooGotchi S3 — Standalone Real-Zoo & Karimono Virtual Pet

An open-source, pocket-sized virtual pet for the **Waveshare ESP32-S3-Touch-LCD-1.69** (`16 MB Flash / 8 MB OPI PSRAM`), powered by a **3.7V 1000mAh LiPo**, a **rechargeable RTC backup coin cell**, and a custom **3D-printed enclosure** (Bambu Lab X1 Carbon + 4× M2×6mm self-tapping screws). **Zero PC required at runtime.**

---

## Key Features

1. **6 Categories × 3 Species (18 Total):**
   - **Land (Permadeath):** Red Panda, Fennec Fox, Capybara
   - **Sky (Permadeath):** Barn Owl, Scarlet Macaw, Ruby Hummingbird
   - **Insect (Permadeath):** Monarch Butterfly (full metamorphosis!), Hercules Beetle, Orchid Mantis
   - **Water (Permadeath):** **West Indian Manatee**, Axolotl, Giant Pacific Octopus
   - **Reptile (Permadeath):** Galapagos Tortoise, Veiled Chameleon (mood-reactive hue!), Leopard Gecko
   - **Karimono (Cross-Breedable Lineage):** Hoshi-Karimono, Kumo-Karimono, Kuro-Karimono
2. **Authentic Lifecycles vs Lineage:**
   - **Real Zoo Animals** follow authentic circadian schedules (diurnal, nocturnal, crepuscular, cathemeral) via the `PCF85063A` RTC and experience real **permadeath** (from natural old age or severe neglect), leaving a permanent tribute in `/memorial.json`.
   - **Karimono** can **cross-breed** at Adult stage (offline Plaza Spirit or via 12-char Base32 Genome Codes) to pass down modular traits, colors, and rare mutations across generations in `/lineage.json`.
3. **100% On-Chip Safe Personality & Micro-LLM:**
   - **3-Layer Defense-in-Depth Safety:** Asymmetric output vocabulary (`include/zoo_vocab_out.h`, 1,421 audited safe words), Layer 2 prompt-injection & leetspeak sanitizer, and Layer 3 output length/loop validator.
   - Runs a **Per-Layer Embedding (PLE) INT4** transformer directly from Flash/PSRAM (`llm_model` partition) on Core 0, plus an instant species/trait/memory-aware fallback engine right out of the box!
4. **Spaced-Repetition Learning Academy & LeetCode Drills (`PCF85063A` SM-2):**
   - **5 Built-In Decks (106 Cards):** *LeetCode / NeetCode Patterns* (24 cards), *Big-O & Interview Edge Cases* (16 cards), *Spanish Vocab* (20 cards), *Japanese JLPT (N4-N1)* (28 cards), and *Zoo Biology* (18 cards), plus up to **40 Custom Cards** uploaded from your phone (including 1-tap *NeetCode Trees/Graphs/DP* and *JLPT N2/N1 Bonus* packs).
   - **Focus / Pomodoro Study Buddy:** 15m / 25m LeetCode Sprint / 35m Mock Interview countdown ring beside your pet.
   - **Protégé Effect & Rewards:** Mastered words/patterns unlock Zoo Coins, Scholar Glasses / Grad Cap cosmetics, and appear in your pet's memory & AI replies.
5. **Touch, 6-Axis IMU Play & Deep Sleep:**
   - Capacitive touch (`CST816T`) + petting strokes, 3 mini-games (`Forage Catch` IMU tilt, `Habitat Care` touch, `Rhythm Chirp` buzzer), pocket step-walker (`QMI8658C`), Zoo Shop, and 4-stage deep sleep.
6. **Local Phone Web Chat & Study Studio (No PC):**
   - Double-press `PWR` (or tap the top-right battery pill / Menu) to start **`ZooGotchi-S3`** SoftAP (or Home Wi-Fi STA) and chat, cross-breed, sync clock, or upload custom LeetCode/language decks right from your phone browser (`http://192.168.4.1` or `http://zoogotchi.local`, with on-screen QR code!).

---

## Project Layout (Where to Edit)

| What You Want to Edit | Folder / Files |
|---|---|
| **Animal Categories & Species** | [`src/zoo/categories/`](src/zoo/categories/) (`land.h`, `sky.h`, `insect.h`, `water.h`, `reptile.h`, `karimono.h`) |
| **Study & Language Flashcard Decks** | [`src/learn/decks/`](src/learn/decks/) (`dsa_patterns.h`, `dsa_bigo.h`, `spanish.h`, `japanese_jlpt.h`, `zoo_bio.h`) |
| **Mini-Games** | [`src/game/minigames/`](src/game/minigames/) (`forage_catch.cpp`, `habitat_care.cpp`, `rhythm_chirp.cpp`) |
| **Micro-LLM, BM25 RAG & Safety** | [`src/ai/`](src/ai/) (`micro_llm_ple.cpp`, `rag_retriever.cpp`, `safety_guardrails.cpp`, `personality_memory.cpp`) |
| **Packed Pixel-Art Sprites & RAG Index** | [`data/sprites/`](data/sprites/) (`*_3.bin`), [`data/rag/kb.bin`](data/rag/kb.bin) |
| **Phone Companion Web App** | [`web_ui/index.html`](web_ui/index.html) (packed by `tools/build_web_gz.py`) |
| **Dataset Curation, Hybrid RAG & Post-Training** | [`llm_training/`](llm_training/) (`data_pipeline/`, `rag/`, `post_training/`, `eval/`, `tracking/`) |

---

## Hardware Safety Warnings (Read First!)

> **CRITICAL — Battery & Coin Cell Wiring:**
> 1. **1000mAh LiPo `MX1.25` Polarity:** Verify Red (`+`) / Black (`-`) against the Waveshare PCB silkscreen before plugging in — generic LiPo leads are often wired backwards and can short the `ETA6098` charger.
> 2. **Rechargeable RTC Coin Cell Only:** The `SH1.0` RTC connector trickle-charges the cell. Only use a **rechargeable** coin cell (`ML414` / `MS621FE`), never a primary `CR`-series coin cell.

See [`hardware/enclosure_notes.md`](hardware/enclosure_notes.md) for Bambu Lab X1 Carbon fitment and M2×6mm screw boss guidelines.

---

## Build & Flash Instructions

Run the complete automated check & build gate (or individual steps below):

```powershell
powershell -ExecutionPolicy Bypass -File "..\.agents\skills\zoogotchi-s3\scripts\check_all.ps1"
```

```powershell
# 1. Activate virtual env (if using D:\ cache on tight C:\ drives, set PLATFORMIO_CORE_DIR)
$env:PLATFORMIO_CORE_DIR = "d:\.platformio"

# 2. Build & audit safe vocabulary + Phone Web PWA + verification suite
.\.venv\Scripts\python.exe llm_training\build_asymmetric_vocab.py
.\.venv\Scripts\python.exe llm_training\verify_vocab_safety.py
.\.venv\Scripts\python.exe tools\build_web_gz.py
.\.venv\Scripts\python.exe test\verify_zoogotchi_suite.py

# 3. Compile firmware & LittleFS image (includes /web/index.html.gz + /sprites/*_3.bin)
.\.venv\Scripts\pio.exe run -e waveshare_s3_169
.\.venv\Scripts\pio.exe run -e waveshare_s3_169 -t buildfs

# 4. Flash firmware + LittleFS over USB-C
.\.venv\Scripts\pio.exe run -e waveshare_s3_169 -t upload
.\.venv\Scripts\pio.exe run -e waveshare_s3_169 -t uploadfs
```

### AI Sprites & Custom Study Decks (`google-genai` Interactions API)
- **Sprites (`data/sprites/`):** 12 species pixel-art sprites (`barn_owl`, `capybara`, `chameleon`, `fennec_fox`, `hercules_beetle`, `kumo_karimono`, `leopard_gecko`, `manatee`, `monarch`, `orchid_mantis`, `red_panda`, `tortoise`) are pre-packed (`12,292 B` each with border flood-fill alpha keying); the remaining 6 render cleanly via the built-in animated C++ renderer until topped up with your `GEMINI_API_KEY` (in `.env` or env):
  ```powershell
  .\.venv\Scripts\python.exe tools\generate_zoo_sprites.py --missing-only --pack
  ```
- **Custom Study Decks:** Generate flashcards with `gemini-3.8-flash` structured JSON output (`tools/generate_study_deck.py --topic "JLPT N2" --count 15`).

### Train & Flash On-Chip PLE Micro-LLM Weights (`zoo_pet.bin`)
1. Curate zero-leakage train/val/test splits (`llm_training/data_pipeline/curate_dataset.py`) or generate fresh samples with `generate_zoo_dataset.py` (`--dry-run`, `--live`, or `--submit-batch`).
2. Run the reproducible GPU post-training suite (**Base SFT -> LoRA SFT -> GRPO vs SAPO -> INT4 ZPLE export + C++ parity**):
   ```powershell
   .\.venv\Scripts\python.exe llm_training\rag\hybrid_rag.py
   .\.venv\Scripts\python.exe llm_training\post_training\run_post_training_suite.py
   .\.venv\Scripts\python.exe llm_training\eval\benchmark_suite.py
   ```
   *(Or run `llm_training/train_ple_zoopet.py --epochs 45 --verify` / [`llm_training/train_ple_zoopet.ipynb`](llm_training/train_ple_zoopet.ipynb) directly.)*
3. Flash `llm_training/out/zoo_pet.bin` (`3,543,028 B` <= `9.875 MB`) to the `llm_model` partition (`0x610000`):
   ```powershell
   D:\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 write_flash 0x610000 llm_training\out\zoo_pet.bin
   ```

---

## On-Device RAG & Micro-LLM Post-Training

All training, alignment, and retrieval components run reproducibly on the local RTX 3060 GPU and deploy straight to the ESP32-S3:

- **On-Device & Host Hybrid RAG:** Packed binary BM25 index (`/rag/kb.bin`, `42,216 B`, `108` chunks across species care, DSA, JLPT N4–N1, genetics, and hardware) queried directly on the ESP32-S3 (`src/ai/rag_retriever.cpp`, `POST /api/rag`), benchmarked against host **BM25 Sparse vs Dense SVD vs Hybrid RRF** (`llm_training/rag/hybrid_rag.py`, `R@1 = 1.000`, `nDCG@5 = 0.970` across 36 queries).
- **Zero-Leakage Curation & LoRA + GRPO/SAPO Post-Training:** Automated SHA-256 + 3-gram Jaccard dedup with 0-prompt-overlap splits (`llm_training/data_pipeline/curate_dataset.py`), followed by `Base SFT` (`R = 0.4865`, `41.0%` species match) -> `LoRA SFT` (`r=8`, `3.20%` trainable weights, `R = 0.9347`, `100.0%` match) -> **`GRPO`** (`R = 0.9381`) vs **`SAPO`** (`R = 0.9313`, lower KL drift `0.0021` vs `0.0071`) -> INT4 `zoo_pet.bin` (`0.9992` C++ parity), logged to SQLite (`llm_training/tracking/experiment_tracker.py`).
- **Architecture & Benchmarks:**
  - [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — Hardware, on-device RAG, post-training pipeline & safety stack
  - [`docs/BENCHMARK_REPORT.md`](docs/BENCHMARK_REPORT.md) — Auto-generated SFT/LoRA/GRPO/SAPO & Hybrid RAG benchmarks + [SVG training curves](docs/assets/training_curves.svg)

---

## License & Attribution

Licensed under the **Apache License, Version 2.0** (`LICENSE`). See `NOTICE` for upstream hardware bring-up attribution and trademark notices.
