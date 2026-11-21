#!/usr/bin/env python3
"""
ZooGotchi S3 — Quantitative Evaluation Benchmark Suite & Report Generator

Reads live run histories from `llm_training/registry/experiments.db`, evaluates all saved
`zoogotchi` checkpoints (`base_sft`, `lora_sft`, `grpo`, `sapo`, plus quantized INT4 `zoo_pet.bin`),
runs safety & held-out checks, and emits:
  1. `docs/assets/training_curves.svg` (pure-Python SVG plots: SFT loss, GRPO vs SAPO, RAG)
  2. `docs/BENCHMARK_REPORT.md` (100% reproducible metrics from SQLite + live inference)

Usage:
  .venv\\Scripts\\python.exe llm_training\\eval\\benchmark_suite.py
"""

import json
import sys
import time
from pathlib import Path

import torch

ROOT = Path(__file__).resolve().parent.parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from llm_training.post_training.policy_opt import hash_word_to_vin  # noqa: E402
from llm_training.post_training.reward_model import score_completion  # noqa: E402
from llm_training.post_training.run_post_training_suite import greedy_generate  # noqa: E402
from llm_training.tracking.experiment_tracker import ExperimentTracker  # noqa: E402
from llm_training.train_ple_zoopet import V_IN, ZpleNumpyRuntime, ZooPleLLM  # noqa: E402

CURATED_ROOT = ROOT / "llm_training" / "data" / "curated"
RAG_JSON = ROOT / "llm_training" / "eval" / "rag_eval_results.json"
OUT_ZOO_BIN = ROOT / "llm_training" / "out" / "zoo_pet.bin"
OUT_SVG = ROOT / "docs" / "assets" / "training_curves.svg"
OUT_REPORT = ROOT / "docs" / "BENCHMARK_REPORT.md"


def _polyline(pts: list[tuple[float, float]], x0: float, y0: float, w: float, h: float,
              x_min: float, x_max: float, y_min: float, y_max: float) -> str:
    if not pts:
        return ""
    dx = max(1e-6, x_max - x_min)
    dy = max(1e-6, y_max - y_min)
    coords = []
    for x, y in pts:
        px = x0 + ((x - x_min) / dx) * w
        py = y0 + h - ((max(y_min, min(y_max, y)) - y_min) / dy) * h
        coords.append(f"{px:.1f},{py:.1f}")
    return " ".join(coords)


def render_svg_curves(tracker: ExperimentTracker, rag_res: dict, out_path: Path):
    zoo_runs = tracker.get_latest_by_stage("zoogotchi")

    def hist(runs: dict, stage: str, metric: str) -> list[tuple[float, float]]:
        r = runs.get(stage)
        if not r:
            return []
        return [(float(st), float(val)) for st, _m, val in tracker.get_step_history(r["run_id"], metric)]

    # Shift LoRA epochs sequentially after base_sft for clear timeline visual
    z_base_val = hist(zoo_runs, "base_sft", "val_loss")
    z_base_tr = hist(zoo_runs, "base_sft", "train_loss")
    z_base_end = z_base_val[-1][0] if z_base_val else 0.0
    z_lora_val = [(x + z_base_end, y) for x, y in hist(zoo_runs, "lora_sft", "val_loss")]
    z_lora_tr = [(x + z_base_end, y) for x, y in hist(zoo_runs, "lora_sft", "train_loss")]

    loss_pts = z_base_val + z_lora_val + z_base_tr + z_lora_tr
    max_ep = max([x for x, _ in (loss_pts + [(10.0, 0.0)])])
    max_loss = max([y for _, y in (loss_pts + [(0.0, 1.0)])] + [1.0])

    # GRPO vs SAPO curves (ZooGotchi)
    z_grpo_r = hist(zoo_runs, "grpo", "mean_reward")
    z_sapo_r = hist(zoo_runs, "sapo", "mean_reward")
    max_rl = max([x for x, _ in (z_grpo_r + z_sapo_r + [(20.0, 0.0)])])
    min_r = min([y for _, y in (z_grpo_r + z_sapo_r)] + [0.8])
    r_lo = max(0.0, round(min_r - 0.05, 2))

    z_grpo_kl = hist(zoo_runs, "grpo", "kl_div")
    z_sapo_kl = hist(zoo_runs, "sapo", "kl_div")
    max_kl = max([y for _, y in (z_grpo_kl + z_sapo_kl)] + [0.01])

    svg = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="980" height="620" viewBox="0 0 980 620">',
        '<rect width="980" height="620" fill="#0f172a" rx="12"/>',
        '<text x="28" y="34" fill="#f8fafc" font-family="Segoe UI, Arial" font-size="18" font-weight="700">'
        'ZooGotchi S3 — Post-Training, RL Alignment &amp; Hybrid RAG Benchmark</text>',
        '<text x="28" y="54" fill="#94a3b8" font-family="Segoe UI, Arial" font-size="12">'
        'Generated directly from SQLite experiments.db runs on NVIDIA GeForce RTX 3060 (12 GB)</text>',
    ]

    # Panel 1: SFT -> LoRA Train & Validation Loss (Top-Left)
    svg.append('<rect x="24" y="72" width="452" height="240" fill="#1e293b" rx="8" stroke="#334155"/>')
    svg.append('<text x="40" y="96" fill="#e2e8f0" font-family="Segoe UI, Arial" font-size="13" font-weight="600">1. Loss Curve: Base SFT -> LoRA SFT (r=8)</text>')
    x0, y0, pw, ph = 64, 112, 392, 162
    svg.append(f'<line x1="{x0}" y1="{y0+ph}" x2="{x0+pw}" y2="{y0+ph}" stroke="#475569"/>')
    svg.append(f'<line x1="{x0}" y1="{y0}" x2="{x0}" y2="{y0+ph}" stroke="#475569"/>')
    if z_base_val:
        split_x = x0 + ((z_base_end - 1.0) / max(1.0, max_ep - 1.0)) * pw
        svg.append(f'<line x1="{split_x:.1f}" y1="{y0}" x2="{split_x:.1f}" y2="{y0+ph}" stroke="#64748b" stroke-dasharray="4,4"/>')
        svg.append(f'<text x="{split_x+4:.1f}" y="{y0+12}" fill="#94a3b8" font-family="Segoe UI" font-size="10">LoRA start</text>')
    svg.append(f'<polyline fill="none" stroke="#94a3b8" stroke-width="2.0" stroke-dasharray="5,3" points="{_polyline(z_base_tr + z_lora_tr, x0, y0, pw, ph, 1, max_ep, 0, max_loss)}"/>')
    svg.append(f'<polyline fill="none" stroke="#38bdf8" stroke-width="2.4" points="{_polyline(z_base_val + z_lora_val, x0, y0, pw, ph, 1, max_ep, 0, max_loss)}"/>')
    svg.append(f'<text x="30" y="{y0+10}" fill="#94a3b8" font-family="Segoe UI" font-size="10">{max_loss:.1f}</text>')
    svg.append(f'<text x="38" y="{y0+ph}" fill="#94a3b8" font-family="Segoe UI" font-size="10">0.0</text>')
    svg.append('<circle cx="72" cy="296" r="4" fill="#38bdf8"/><text x="82" y="300" fill="#cbd5e1" font-family="Segoe UI" font-size="11">val_loss (solid)</text>')
    svg.append('<circle cx="210" cy="296" r="4" fill="#94a3b8"/><text x="220" y="300" fill="#cbd5e1" font-family="Segoe UI" font-size="11">train_loss (dashed)</text>')

    # Panel 2: GRPO vs SAPO Reward Signal (Top-Right)
    svg.append('<rect x="500" y="72" width="456" height="240" fill="#1e293b" rx="8" stroke="#334155"/>')
    svg.append('<text x="516" y="96" fill="#e2e8f0" font-family="Segoe UI, Arial" font-size="13" font-weight="600">2. RL Policy Alignment: GRPO vs SAPO Mean Reward</text>')
    x0, y0, pw, ph = 544, 112, 392, 162
    svg.append(f'<line x1="{x0}" y1="{y0+ph}" x2="{x0+pw}" y2="{y0+ph}" stroke="#475569"/>')
    svg.append(f'<line x1="{x0}" y1="{y0}" x2="{x0}" y2="{y0+ph}" stroke="#475569"/>')
    svg.append(f'<polyline fill="none" stroke="#38bdf8" stroke-width="2.4" points="{_polyline(z_grpo_r, x0, y0, pw, ph, 1, max_rl, r_lo, 1.0)}"/>')
    svg.append(f'<polyline fill="none" stroke="#a78bfa" stroke-width="2.4" points="{_polyline(z_sapo_r, x0, y0, pw, ph, 1, max_rl, r_lo, 1.0)}"/>')
    svg.append(f'<text x="510" y="{y0+10}" fill="#94a3b8" font-family="Segoe UI" font-size="10">1.00</text>')
    svg.append(f'<text x="510" y="{y0+ph}" fill="#94a3b8" font-family="Segoe UI" font-size="10">{r_lo:.2f}</text>')
    svg.append('<circle cx="548" cy="296" r="4" fill="#38bdf8"/><text x="558" y="300" fill="#cbd5e1" font-family="Segoe UI" font-size="11">GRPO rollout reward</text>')
    svg.append('<circle cx="712" cy="296" r="4" fill="#a78bfa"/><text x="722" y="300" fill="#cbd5e1" font-family="Segoe UI" font-size="11">SAPO rollout reward</text>')

    # Panel 3: KL Divergence Stability (Bottom-Left)
    svg.append('<rect x="24" y="330" width="452" height="260" fill="#1e293b" rx="8" stroke="#334155"/>')
    svg.append('<text x="40" y="354" fill="#e2e8f0" font-family="Segoe UI, Arial" font-size="13" font-weight="600">3. Policy KL Divergence vs SFT Reference</text>')
    x0, y0, pw, ph = 64, 370, 392, 180
    svg.append(f'<line x1="{x0}" y1="{y0+ph}" x2="{x0+pw}" y2="{y0+ph}" stroke="#475569"/>')
    svg.append(f'<line x1="{x0}" y1="{y0}" x2="{x0}" y2="{y0+ph}" stroke="#475569"/>')
    svg.append(f'<polyline fill="none" stroke="#a78bfa" stroke-width="2.4" points="{_polyline(z_sapo_kl, x0, y0, pw, ph, 1, max_rl, 0.0, max_kl)}"/>')
    svg.append(f'<polyline fill="none" stroke="#f97316" stroke-width="2.4" points="{_polyline(z_grpo_kl, x0, y0, pw, ph, 1, max_rl, 0.0, max_kl)}"/>')
    svg.append(f'<text x="28" y="{y0+10}" fill="#94a3b8" font-family="Segoe UI" font-size="10">{max_kl:.3f}</text>')
    svg.append(f'<text x="36" y="{y0+ph}" fill="#94a3b8" font-family="Segoe UI" font-size="10">0.0</text>')
    svg.append('<circle cx="72" cy="572" r="4" fill="#a78bfa"/><text x="82" y="576" fill="#cbd5e1" font-family="Segoe UI" font-size="11">SAPO soft gate KL</text>')
    svg.append('<circle cx="240" cy="572" r="4" fill="#f97316"/><text x="250" y="576" fill="#cbd5e1" font-family="Segoe UI" font-size="11">GRPO clipped KL</text>')

    # Panel 4: Hybrid RAG Retrieval Bar Chart (Bottom-Right)
    svg.append('<rect x="500" y="330" width="456" height="260" fill="#1e293b" rx="8" stroke="#334155"/>')
    svg.append('<text x="516" y="354" fill="#e2e8f0" font-family="Segoe UI, Arial" font-size="13" font-weight="600">4. RAG Benchmark: R@1 &amp; nDCG@5 (36 Queries)</text>')
    dr = rag_res.get("zoogotchi", {})
    bars = []
    for m, lbl, col in (
        ("bm25_sparse", "BM25", "#64748b"),
        ("dense_svd", "Dense", "#38bdf8"),
        ("hybrid_rrf", "Hybrid", "#22c55e"),
    ):
        r1 = float(dr.get(m, {}).get("recall_at_1", 0.0))
        ndcg = float(dr.get(m, {}).get("ndcg_at_5", 0.0))
        bars.append((f"{lbl} R@1", r1, col))
        bars.append((f"{lbl} nDCG", ndcg, col))
    bx0, by0, bh = 524, 382, 160
    bw = 54
    for idx, (lbl, val, col) in enumerate(bars):
        x = bx0 + idx * 70
        bar_h = max(4.0, val * bh)
        svg.append(f'<rect x="{x}" y="{by0+bh-bar_h:.1f}" width="{bw}" height="{bar_h:.1f}" fill="{col}" rx="4"/>')
        svg.append(f'<text x="{x+bw/2:.1f}" y="{by0+bh-bar_h-5:.1f}" fill="#f8fafc" font-family="Segoe UI" font-size="10" text-anchor="middle">{val:.3f}</text>')
        svg.append(f'<text x="{x+bw/2:.1f}" y="{by0+bh+14}" fill="#cbd5e1" font-family="Segoe UI" font-size="9.5" text-anchor="middle">{lbl}</text>')

    svg.append("</svg>")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(svg), encoding="utf-8")


def run_qualitative_checks(tracker: ExperimentTracker) -> list[dict]:
    device = "cuda" if torch.cuda.is_available() else "cpu"
    d_dir = CURATED_ROOT / "zoogotchi"
    vout = json.loads((d_dir / "vocab_out.json").read_text(encoding="utf-8"))
    test_rows = [json.loads(ln) for ln in (d_dir / "test.jsonl").read_text(encoding="utf-8").splitlines() if ln.strip()]
    vout_hashes = torch.tensor([hash_word_to_vin(w, V_IN) for w in vout], dtype=torch.long, device=device)

    runs = tracker.get_latest_by_stage("zoogotchi")
    probe_rows = [test_rows[0]]
    rag_rows = [r for r in test_rows if "[RAG:" in r["prompt"]]
    if rag_rows:
        probe_rows.append(rag_rows[0])

    dom_out = []
    for row in probe_rows:
        entry = {"prompt": row["prompt"], "outputs": {}}
        for stage in ("base_sft", "lora_sft", "grpo", "sapo"):
            r = runs.get(stage)
            if not r:
                continue
            ckpt = Path(r["artifact_path"])
            if not ckpt.is_file():
                continue
            alt_merged = ckpt.parent / f"{stage}_merged.pt"
            if alt_merged.is_file():
                ckpt = alt_merged
            mod = ZooPleLLM(len(vout)).to(device)
            mod.load_state_dict(torch.load(ckpt, map_location=device, weights_only=True))
            txt = greedy_generate(mod, row["prompt"], vout, vout_hashes, max_tok=28, device=device)
            sc = score_completion(row["prompt"], txt, domain="zoogotchi", reference=row["reply"])
            entry["outputs"][stage] = {"text": txt, "reward": sc.total, "unsafe": sc.is_unsafe}

        if OUT_ZOO_BIN.is_file():
            rt = ZpleNumpyRuntime(OUT_ZOO_BIN)
            txt_q, _ = rt.generate_greedy(row["prompt"], vout, max_tok=28)
            sc_q = score_completion(row["prompt"], txt_q, domain="zoogotchi", reference=row["reply"])
            entry["outputs"]["zple_int4"] = {"text": txt_q, "reward": sc_q.total, "unsafe": sc_q.is_unsafe}
        dom_out.append(entry)

    return dom_out


def generate_report():
    tracker = ExperimentTracker()
    rag_res = json.loads(RAG_JSON.read_text(encoding="utf-8")) if RAG_JSON.is_file() else {}
    render_svg_curves(tracker, rag_res, OUT_SVG)
    samples = run_qualitative_checks(tracker)

    cat = json.loads((ROOT / "llm_training" / "registry" / "catalog.json").read_text(encoding="utf-8"))
    m = json.loads((CURATED_ROOT / "zoogotchi" / "manifest.json").read_text(encoding="utf-8"))
    sp = m["splits"]
    runs = tracker.get_latest_by_stage("zoogotchi")

    lines = [
        "# ZooGotchi S3 — RAG & Micro-LLM Post-Training Benchmark Report",
        "",
        f"Generated `{time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}` directly from `llm_training/registry/experiments.db` and `llm_training/eval/rag_eval_results.json`.",
        "",
        "![Training & RAG Benchmark Curves](./assets/training_curves.svg)",
        "",
        "---",
        "",
        "## 1. Curated Dataset Governance & Leakage Prevention",
        "",
        "| Domain | Raw Count | Exact Dedup | Near Dedup | Curated | Train / Val / Test | `dataset_sha256` | Prompt Overlap |",
        "| :--- | ---: | ---: | ---: | ---: | :---: | :--- | ---: |",
        (
            f"| `zoogotchi` | {m['raw_count']} | -{m['exact_dups_removed']} | -{m['near_dups_removed']} | "
            f"**{m['curated_total']}** | {sp['train']} / {sp['val']} / {sp['test']} | `{m['dataset_sha256'][:12]}` | **{m['prompt_overlap_count']}** |"
        ),
        "",
        "## 2. Post-Training Stage Comparison (`Base SFT -> LoRA SFT -> GRPO vs SAPO -> INT4`)",
        "",
        "| Stage | Run ID | Test Loss | PPL | Mean Reward | Accuracy Score | RAG Grounding | Safety Score | Species Match | Unsafe Rate |",
        "| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]

    for stage in ("base_sft", "lora_sft", "grpo", "sapo"):
        r = runs.get(stage)
        if not r:
            continue
        s = r["summary"]
        lines.append(
            f"| **{stage}** | `{r['run_id']}` | {s.get('test_loss', '-')} | {s.get('test_perplexity', '-')} | "
            f"**{s.get('mean_reward', '-')}** | {s.get('r_accuracy', '-')} | {s.get('r_rag', '-')} | "
            f"{s.get('r_safety', '-')} | **{s.get('domain_match_rate', 0):.1%}** | {s.get('unsafe_rate', 0):.1%} |"
        )
    if "zoogotchi:zple_int4_deployed" in cat.get("latest_by_domain_stage", {}):
        z = cat["latest_by_domain_stage"]["zoogotchi:zple_int4_deployed"]
        zm = z["metrics"]
        lines.append(
            f"| **zple_int4** | `{z['run_id']}` | — | — | **{zm.get('mean_reward', '-')}** | {zm.get('r_accuracy', '-')} | "
            f"{zm.get('r_rag', '-')} | {zm.get('r_safety', '-')} | **{zm.get('domain_match_rate', 0):.1%}** | "
            f"{zm.get('unsafe_rate', 0):.1%} (winner `{zm.get('winner_policy')}`, head RMSE `{zm.get('head_quant_rmse')}`) |"
        )

    lines.extend([
        "",
        "## 3. Host Hybrid RAG Baseline (`BM25 Sparse` vs `Dense SVD` vs `Hybrid RRF`)",
        "",
        "| Retriever | Queries | Recall@1 | Recall@3 | MRR@5 | nDCG@5 | Median Latency (ms) |",
        "| :--- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for meth in ("bm25_sparse", "dense_svd", "hybrid_rrf"):
        rm = rag_res.get("zoogotchi", {}).get(meth)
        if rm:
            lines.append(
                f"| **{meth}** | {rm['num_queries']} | {rm['recall_at_1']:.3f} | "
                f"{rm['recall_at_3']:.3f} | {rm['mrr_at_5']:.3f} | **{rm['ndcg_at_5']:.3f}** | {rm['latency_ms']:.3f} |"
            )

    lines.extend([
        "",
        "## 4. Qualitative Held-Out Test Outputs",
        "",
    ])
    for it in samples:
        lines.append(f"- **Prompt:** `{it['prompt']}`")
        for st, res in it["outputs"].items():
            lines.append(f"  - `{st}` (`R={res['reward']:.4f}`, `unsafe={res['unsafe']}`): {res['text']}")
    lines.append("")

    OUT_REPORT.parent.mkdir(parents=True, exist_ok=True)
    OUT_REPORT.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote SVG curves -> {OUT_SVG}")
    print(f"Wrote Benchmark Report -> {OUT_REPORT}")


if __name__ == "__main__":
    generate_report()
