#!/usr/bin/env python3
"""
ZooGotchi S3 — End-to-End Post-Training Orchestrator (RTX 3060 / CUDA)

Executes the post-training pipeline for `zoogotchi`:
  1. Stage 1 (`base_sft`)  — Supervised Fine-Tuning on curated `train.jsonl`
  2. Stage 2 (`lora_sft`)  — Parameter-Efficient LoRA SFT (r=8, alpha=16; ~3.2% weights)
  3. Stage 3a (`grpo`)     — Group Relative Policy Optimization from LoRA SFT
  4. Stage 3b (`sapo`)     — Soft Adaptive Policy Optimization from identical LoRA SFT
  5. Held-out `test.jsonl` + safety evaluation after every stage, logged to SQLite & `catalog.json`
  6. Selects best aligned policy, merges LoRA into `nn.Linear`, exports
     INT4 Group-128 `llm_training/out/zoo_pet.bin`, and verifies C++ runtime parity (`>= 0.90`).

Usage:
  .venv\\Scripts\\python.exe llm_training\\post_training\\run_post_training_suite.py
"""

import argparse
import copy
import json
import re
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

ROOT = Path(__file__).resolve().parent.parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from llm_training.post_training.lora import (  # noqa: E402
    freeze_base_enable_lora,
    inject_lora,
    merge_and_unload_lora,
    save_lora_adapter,
)
from llm_training.post_training.policy_opt import (  # noqa: E402
    decode_ids,
    hash_word_to_vin,
    run_policy_optimization,
)
from llm_training.post_training.reward_model import (  # noqa: E402
    SPECIES_NAME_BY_SLUG,
    score_completion,
)
from llm_training.tracking.experiment_tracker import ExperimentTracker  # noqa: E402
from llm_training.train_ple_zoopet import (  # noqa: E402
    V_IN,
    ZooPleLLM,
    encode_sample,
    export_zple_binary,
    verify_binary,
)

CURATED_ROOT = ROOT / "llm_training" / "data" / "curated"
CKPT_ROOT = ROOT / "llm_training" / "out" / "checkpoints"
OUT_ZOO_BIN = ROOT / "llm_training" / "out" / "zoo_pet.bin"


def _load_curated(domain: str) -> tuple[list[dict], list[dict], list[dict], list[str], dict]:
    d_dir = CURATED_ROOT / domain
    manifest = json.loads((d_dir / "manifest.json").read_text(encoding="utf-8"))
    vout_words = json.loads((d_dir / "vocab_out.json").read_text(encoding="utf-8"))
    splits = {}
    for sp in ("train", "val", "test"):
        splits[sp] = [
            json.loads(ln)
            for ln in (d_dir / f"{sp}.jsonl").read_text(encoding="utf-8").splitlines()
            if ln.strip()
        ]
    return splits["train"], splits["val"], splits["test"], vout_words, manifest


@torch.no_grad()
def compute_sft_loss(
    model: torch.nn.Module,
    encoded: list[tuple[list[int], list[int]]],
    v_out: int,
    batch_size: int,
    device: str,
) -> float:
    model.eval()
    tot_loss, steps = 0.0, 0
    for start in range(0, len(encoded), batch_size):
        chunk = encoded[start : start + batch_size]
        max_l = max(len(x[0]) for x in chunk)
        xb = torch.zeros((len(chunk), max_l), dtype=torch.long)
        yb = torch.full((len(chunk), max_l), -100, dtype=torch.long)
        for i, (s_in, tgt) in enumerate(chunk):
            xb[i, : len(s_in)] = torch.tensor(s_in, dtype=torch.long)
            yb[i, : len(tgt)] = torch.tensor(tgt, dtype=torch.long)
        xb, yb = xb.to(device), yb.to(device)
        logits = model(xb)
        loss = F.cross_entropy(logits.view(-1, v_out), yb.view(-1), ignore_index=-100)
        tot_loss += float(loss.item())
        steps += 1
    return tot_loss / float(max(1, steps))


@torch.no_grad()
def greedy_generate(
    model: torch.nn.Module,
    prompt: str,
    vout_words: list[str],
    vout_hashes: torch.Tensor,
    max_tok: int = 28,
    device: str = "cuda",
) -> str:
    model.eval()
    p_ids = [1] + [hash_word_to_vin(w, V_IN) for w in prompt.split()] + [1]
    seq = torch.tensor([p_ids], dtype=torch.long, device=device)
    out_ids = []
    for _ in range(max_tok):
        logits = model(seq)[0, -1, :].clone()
        logits[0] = -1e9
        logits[1] = -1e9
        logits[3] = -1e9
        nxt = int(torch.argmax(logits).item())
        if nxt == 2 or nxt >= len(vout_words):
            break
        out_ids.append(nxt)
        nxt_in = vout_hashes[nxt].view(1, 1)
        seq = torch.cat([seq, nxt_in], dim=1)
    return decode_ids(out_ids, vout_words)


@torch.no_grad()
def evaluate_on_test(
    model: torch.nn.Module,
    test_rows: list[dict],
    vout_words: list[str],
    word2out: dict[str, int],
    domain: str,
    device: str,
) -> dict[str, float]:
    enc_test = [encode_sample(r["prompt"], r["reply"], word2out) for r in test_rows]
    test_loss = compute_sft_loss(model, enc_test, len(vout_words), batch_size=32, device=device)
    vout_hashes = torch.tensor([hash_word_to_vin(w, V_IN) for w in vout_words], dtype=torch.long, device=device)

    rewards, accs, rags, safs, fmts = [], [], [], [], []
    unsafe_cnt = 0
    domain_match_cnt = 0

    for row in test_rows:
        pred = greedy_generate(model, row["prompt"], vout_words, vout_hashes, max_tok=28, device=device)
        sc = score_completion(row["prompt"], pred, domain=domain, reference=row["reply"])
        rewards.append(sc.total)
        accs.append(sc.accuracy)
        rags.append(sc.rag_grounding)
        safs.append(sc.safety)
        fmts.append(sc.format_score)
        if sc.is_unsafe:
            unsafe_cnt += 1

        low_p = row["prompt"].lower()
        low_pred = pred.lower()
        m = re.search(r"\[sp:([a-z0-9_]+)\]", low_p)
        target_sp = SPECIES_NAME_BY_SLUG.get(m.group(1), "") if m else ""
        if target_sp and target_sp in low_pred:
            domain_match_cnt += 1

    n = float(max(1, len(test_rows)))
    return {
        "test_loss": round(test_loss, 4),
        "test_perplexity": round(float(min(9999.0, np.exp(min(9.0, test_loss)))), 2),
        "mean_reward": round(float(np.mean(rewards)), 4),
        "r_accuracy": round(float(np.mean(accs)), 4),
        "r_rag": round(float(np.mean(rags)), 4),
        "r_safety": round(float(np.mean(safs)), 4),
        "format_score": round(float(np.mean(fmts)), 4),
        "unsafe_rate": round(unsafe_cnt / n, 4),
        "domain_match_rate": round(domain_match_cnt / n, 4),
    }


def _run_sft_loop(
    model: torch.nn.Module,
    train_enc: list[tuple[list[int], list[int]]],
    val_enc: list[tuple[list[int], list[int]]],
    v_out: int,
    epochs: int,
    batch_size: int,
    lr: float,
    device: str,
    tracker: ExperimentTracker,
    run_id: str,
    label: str,
):
    trainable = [p for p in model.parameters() if p.requires_grad]
    opt = torch.optim.AdamW(trainable, lr=lr, weight_decay=0.01)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=max(1, epochs))
    t0 = time.time()

    for ep in range(1, epochs + 1):
        model.train()
        perm = np.random.permutation(len(train_enc))
        tot_loss, steps = 0.0, 0
        for start in range(0, len(train_enc), batch_size):
            bidx = perm[start : start + batch_size]
            max_l = max(len(train_enc[int(i)][0]) for i in bidx)
            xb = torch.zeros((len(bidx), max_l), dtype=torch.long)
            yb = torch.full((len(bidx), max_l), -100, dtype=torch.long)
            for r_i, s_i in enumerate(bidx):
                s_in, tgt = train_enc[int(s_i)]
                xb[r_i, : len(s_in)] = torch.tensor(s_in, dtype=torch.long)
                yb[r_i, : len(tgt)] = torch.tensor(tgt, dtype=torch.long)
            xb, yb = xb.to(device), yb.to(device)
            logits = model(xb)
            loss = F.cross_entropy(logits.view(-1, v_out), yb.view(-1), ignore_index=-100)
            opt.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(trainable, 1.0)
            opt.step()
            tot_loss += float(loss.item())
            steps += 1
        sched.step()
        tr_loss = tot_loss / float(max(1, steps))
        va_loss = compute_sft_loss(model, val_enc, v_out, batch_size=batch_size, device=device)
        tracker.log_step(run_id, ep, {"train_loss": tr_loss, "val_loss": va_loss})
        if ep == 1 or ep % 5 == 0 or ep == epochs:
            print(f"  [{label} ep {ep:2d}/{epochs}] train_loss={tr_loss:.4f} val_loss={va_loss:.4f} ({time.time() - t0:.1f}s)")


def run_domain_pipeline(domain: str, args, tracker: ExperimentTracker, device: str):
    print(f"\n==================== POST-TRAINING DOMAIN: {domain} ====================")
    torch.manual_seed(42)
    np.random.seed(42)

    train_rows, val_rows, test_rows, vout_words, manifest = _load_curated(domain)
    v_out = len(vout_words)
    word2out = {w.lower(): i for i, w in enumerate(vout_words)}
    d_sha = manifest["dataset_sha256"]

    train_enc = [encode_sample(r["prompt"], r["reply"], word2out) for r in train_rows]
    val_enc = [encode_sample(r["prompt"], r["reply"], word2out) for r in val_rows]

    ckpt_dir = CKPT_ROOT / domain
    ckpt_dir.mkdir(parents=True, exist_ok=True)

    # 1. Base SFT
    base_cfg = {"epochs": args.base_epochs, "batch_size": 32, "lr": 1.0e-3, "v_out": v_out}
    base_run_id = tracker.start_run(domain, "base_sft", base_cfg, dataset_sha256=d_sha)
    base_model = ZooPleLLM(v_out).to(device)
    _run_sft_loop(
        base_model, train_enc, val_enc, v_out,
        epochs=args.base_epochs, batch_size=32, lr=1.0e-3,
        device=device, tracker=tracker, run_id=base_run_id, label=f"{domain}/base_sft",
    )
    base_eval = evaluate_on_test(base_model, test_rows, vout_words, word2out, domain, device)
    base_ckpt = ckpt_dir / "base_sft.pt"
    torch.save(base_model.state_dict(), base_ckpt)
    tracker.finish_run(base_run_id, "completed", base_eval, str(base_ckpt))
    tracker.register_model_variant(base_run_id, domain, "base_sft", str(base_ckpt), base_eval)
    print(f"  [BASE_SFT TEST] {base_eval}")

    # 2. LoRA SFT (frozen base, trainable LoRA r=8, alpha=16 across attention + SwiGLU FFN)
    lora_cfg = {"epochs": args.lora_epochs, "rank": 8, "alpha": 16.0, "batch_size": 32, "lr": 7.0e-4}
    lora_run_id = tracker.start_run(domain, "lora_sft", lora_cfg, dataset_sha256=d_sha, parent_run_id=base_run_id)
    lora_model = copy.deepcopy(base_model).to(device)
    n_inj = inject_lora(lora_model, rank=8, alpha=16.0, dropout=0.05)
    tr_cnt, tot_cnt = freeze_base_enable_lora(lora_model, unfreeze_head=False)
    print(f"  [LoRA SFT] Injected {n_inj} LoRALinear layers | trainable={tr_cnt:,}/{tot_cnt:,} ({100.0*tr_cnt/tot_cnt:.2f}%)")
    _run_sft_loop(
        lora_model, train_enc, val_enc, v_out,
        epochs=args.lora_epochs, batch_size=32, lr=7.0e-4,
        device=device, tracker=tracker, run_id=lora_run_id, label=f"{domain}/lora_sft",
    )
    lora_eval = evaluate_on_test(lora_model, test_rows, vout_words, word2out, domain, device)
    lora_eval["trainable_params"] = tr_cnt
    lora_eval["total_params"] = tot_cnt
    lora_adapter_ckpt = ckpt_dir / "lora_sft_adapter.pt"
    save_lora_adapter(lora_model, lora_adapter_ckpt, metadata={"rank": 8, "alpha": 16.0})
    lora_merged = copy.deepcopy(lora_model)
    merge_and_unload_lora(lora_merged)
    lora_merged_ckpt = ckpt_dir / "lora_sft_merged.pt"
    torch.save(lora_merged.state_dict(), lora_merged_ckpt)
    tracker.finish_run(lora_run_id, "completed", lora_eval, str(lora_adapter_ckpt))
    tracker.register_model_variant(
        lora_run_id, domain, "lora_sft", str(lora_merged_ckpt), lora_eval,
        parent_run_id=base_run_id, extra={"adapter_path": str(lora_adapter_ckpt), "trainable_pct": round(100.0 * tr_cnt / tot_cnt, 2)},
    )
    print(f"  [LORA_SFT TEST] {lora_eval}")

    # 3a & 3b: Parallel apples-to-apples Policy-Reward Comparison (GRPO vs SAPO from identical LoRA SFT)
    rl_candidates = {}
    for method in ("grpo", "sapo"):
        rl_cfg = {
            "method": method, "steps": args.rl_steps, "batch_prompts": 8,
            "group_size": 8, "inner_epochs": 2, "lr": 1.5e-4, "kl_beta": 0.05,
            "temp": 0.80, "clip_eps": 0.20, "sapo_tau_pos": 1.0, "sapo_tau_neg": 1.05,
        }
        rl_run_id = tracker.start_run(domain, method, rl_cfg, dataset_sha256=d_sha, parent_run_id=lora_run_id)
        rl_model = copy.deepcopy(lora_model).to(device)
        freeze_base_enable_lora(rl_model, unfreeze_head=False)
        rl_train_last = run_policy_optimization(
            rl_model, train_rows, vout_words, domain=domain, method=method,
            steps=args.rl_steps, batch_prompts=8, group_size=8, inner_epochs=2,
            lr=1.5e-4, kl_beta=0.05, temp=0.80, device=device, seed=42,
            step_cb=lambda st, met, rid=rl_run_id: tracker.log_step(rid, st, met),
        )
        rl_eval = evaluate_on_test(rl_model, test_rows, vout_words, word2out, domain, device)
        rl_eval["final_kl_div"] = round(rl_train_last["kl_div"], 4)
        rl_eval["final_entropy"] = round(rl_train_last["entropy"], 4)

        merged_rl = copy.deepcopy(rl_model)
        merge_and_unload_lora(merged_rl)
        ckpt_path = ckpt_dir / f"{method}_merged.pt"
        torch.save(merged_rl.state_dict(), ckpt_path)
        tracker.finish_run(rl_run_id, "completed", rl_eval, str(ckpt_path))
        tracker.register_model_variant(rl_run_id, domain, method, str(ckpt_path), rl_eval, parent_run_id=lora_run_id)
        print(f"  [{method.upper()} TEST] {rl_eval}")
        rl_candidates[method] = (rl_run_id, rl_eval, merged_rl)

    # 4. For `zoogotchi`: Export winning aligned & merged model -> INT4 zoo_pet.bin & verify C++ parity
    if domain == "zoogotchi":
        def rank_key(item):
            _m, (_rid, ev, _mod) = item
            return ev["mean_reward"] - 2.0 * ev["unsafe_rate"] + 0.25 * ev["domain_match_rate"]

        best_method, (best_rid, best_ev, best_model) = max(rl_candidates.items(), key=rank_key)
        print(f"\n  [DEPLOY] Selected winner policy '{best_method.upper()}' (R={best_ev['mean_reward']}, match={best_ev['domain_match_rate']}) -> exporting {OUT_ZOO_BIN}")
        head_rmse = export_zple_binary(best_model, v_out, OUT_ZOO_BIN)
        verify_binary(OUT_ZOO_BIN, vout_words, pt_model=best_model)
        tracker.register_model_variant(
            f"{best_rid}-int4", domain, "zple_int4_deployed", str(OUT_ZOO_BIN),
            {**best_ev, "head_quant_rmse": round(head_rmse, 6), "winner_policy": best_method},
            parent_run_id=best_rid,
        )


def main():
    ap = argparse.ArgumentParser(description="ZooGotchi S3 Post-Training Pipeline (Base SFT -> LoRA SFT -> GRPO vs SAPO)")
    ap.add_argument("--domain", choices=("zoogotchi",), default="zoogotchi")
    ap.add_argument("--base-epochs", type=int, default=2)
    ap.add_argument("--lora-epochs", type=int, default=8)
    ap.add_argument("--rl-steps", type=int, default=20)
    ap.add_argument("--cpu", action="store_true")
    args = ap.parse_args()

    device = "cuda" if torch.cuda.is_available() and not args.cpu else "cpu"
    print(f"Post-Training Hardware: {device}" + (f" ({torch.cuda.get_device_name(0)})" if device == "cuda" else ""))

    tracker = ExperimentTracker()
    run_domain_pipeline(args.domain, args, tracker, device)


if __name__ == "__main__":
    main()
