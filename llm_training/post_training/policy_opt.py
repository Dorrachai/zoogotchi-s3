#!/usr/bin/env python3
"""
ZooGotchi S3 — GRPO (Group Relative Policy Optimization) &
SAPO (Soft Adaptive Policy Optimization) Policy-Reward Trainer.

Runs on-policy grouped rollouts (G completions per prompt), scores via the domain
RewardModel, normalizes advantages within each prompt group, and updates policy
weights (full or LoRA-only) with Schulman non-negative KL regularization against
the frozen SFT reference policy.
"""

import copy
import time
from typing import Callable

import numpy as np
import torch
import torch.nn.functional as F

from llm_training.post_training.reward_model import score_completion


def hash_word_to_vin(word: str, v_in: int = 2048) -> int:
    h = 2166136261
    for ch in word.lower().encode("ascii", "ignore"):
        h ^= ch
        h = (h * 16777619) & 0xFFFFFFFF
    return 4 + (h % (v_in - 4))


def decode_ids(token_ids: list[int], vout_words: list[str]) -> str:
    reply = ""
    for tid in token_ids:
        if tid == 2 or tid >= len(vout_words):
            break
        if tid in (0, 1, 3):
            continue
        w = vout_words[tid]
        is_punct = w[0] in {".", ",", "!", "?", ":", ";", "..."}
        if not reply or is_punct:
            reply += w
        else:
            reply += " " + w
    return reply


def _masked_log_softmax(logits: torch.Tensor, temp: float = 1.0) -> torch.Tensor:
    masked = logits.clone()
    masked[..., 0] = -1e9
    masked[..., 1] = -1e9
    masked[..., 3] = -1e9
    return F.log_softmax(masked / max(0.1, temp), dim=-1)


@torch.no_grad()
def _sample_rollouts_for_prompt(
    policy: torch.nn.Module,
    prompt_ids: list[int],
    vout_words: list[str],
    vout_hashes: torch.Tensor,
    group_size: int = 8,
    max_gen: int = 28,
    temp: float = 0.85,
    top_k: int = 16,
    device: str = "cuda",
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor, list[str]]:
    """
    Returns:
      prompt_t:    [L_p] (starts with <bos>=1 and ends with <bos>=1 trigger)
      gen_out_ids: [G, L_gen]
      gen_in_ids:  [G, L_gen] (FNV-1a hashes matching C++ runtime)
      old_logp:    [G, L_gen]
      mask:        [G, L_gen] (1.0 up to and including first <eos>=2)
      texts:       list[str] of length G
    """
    policy.eval()
    g = group_size
    seq = torch.tensor(prompt_ids, dtype=torch.long, device=device).unsqueeze(0).repeat(g, 1)  # [G, L_p]
    done = torch.zeros(g, dtype=torch.bool, device=device)

    out_cols = []
    in_cols = []
    logp_cols = []
    mask_cols = []

    for _step in range(max_gen):
        logits = policy(seq)[:, -1, :]  # [G, V_OUT]
        logp_all = _masked_log_softmax(logits, temp=temp)

        # Top-k filter for sampling while keeping normalized probabilities over top-k
        k = min(top_k, logp_all.size(-1))
        top_vals, top_idx = torch.topk(logp_all, k, dim=-1)
        probs_k = torch.softmax(top_vals, dim=-1)
        picked_rel = torch.multinomial(probs_k, num_samples=1).squeeze(-1)
        nxt = top_idx.gather(1, picked_rel.unsqueeze(1)).squeeze(1)  # [G]
        tok_logp = logp_all.gather(1, nxt.unsqueeze(1)).squeeze(1)

        nxt = torch.where(done, torch.full_like(nxt, 2), nxt)
        step_mask = (~done).float()

        out_cols.append(nxt)
        in_tok = vout_hashes[nxt]
        in_cols.append(in_tok)
        logp_cols.append(tok_logp * step_mask)
        mask_cols.append(step_mask)

        done = done | (nxt == 2)
        if done.all():
            break
        seq = torch.cat([seq, in_tok.unsqueeze(1)], dim=1)

    gen_out = torch.stack(out_cols, dim=1)
    gen_in = torch.stack(in_cols, dim=1)
    old_lp = torch.stack(logp_cols, dim=1)
    mask = torch.stack(mask_cols, dim=1)

    texts = [decode_ids(gen_out[i].tolist(), vout_words) for i in range(g)]
    prompt_t = torch.tensor(prompt_ids, dtype=torch.long, device=device)
    return prompt_t, gen_out, gen_in, old_lp, mask, texts


def _sequence_token_logps(
    model: torch.nn.Module,
    prompt_t: torch.Tensor,
    gen_in: torch.Tensor,
    gen_out: torch.Tensor,
    temp: float = 0.85,
) -> tuple[torch.Tensor, torch.Tensor]:
    g, l_gen = gen_out.shape
    l_p = prompt_t.numel()
    pt = prompt_t.unsqueeze(0).repeat(g, 1)
    if l_gen > 1:
        full_in = torch.cat([pt, gen_in[:, :-1]], dim=1)
    else:
        full_in = pt
    logits = model(full_in)[:, l_p - 1 : l_p - 1 + l_gen, :]
    logp_all = _masked_log_softmax(logits, temp=temp)
    tok_lp = logp_all.gather(2, gen_out.unsqueeze(2)).squeeze(2)
    probs = torch.exp(logp_all)
    entropy = -(probs * logp_all).sum(dim=-1)
    return tok_lp, entropy


def run_policy_optimization(
    policy: torch.nn.Module,
    train_rows: list[dict],
    vout_words: list[str],
    domain: str,
    method: str = "grpo",  # "grpo" or "sapo"
    steps: int = 36,
    batch_prompts: int = 8,
    group_size: int = 8,
    inner_epochs: int = 2,
    lr: float = 2e-4,
    kl_beta: float = 0.04,
    clip_eps: float = 0.20,
    sapo_tau_pos: float = 1.0,
    sapo_tau_neg: float = 1.05,
    temp: float = 0.85,
    v_in: int = 2048,
    device: str = "cuda",
    seed: int = 42,
    step_cb: Callable[[int, dict[str, float]], None] | None = None,
) -> dict[str, float]:
    assert method in ("grpo", "sapo"), f"Unsupported policy method: {method}"
    torch.manual_seed(seed)
    np.random.seed(seed)

    ref_model = copy.deepcopy(policy).eval()
    for p in ref_model.parameters():
        p.requires_grad = False

    vout_hashes = torch.tensor([hash_word_to_vin(w, v_in) for w in vout_words], dtype=torch.long, device=device)
    trainable = [p for p in policy.parameters() if p.requires_grad]
    assert trainable, "No trainable parameters enabled for policy optimization!"
    opt = torch.optim.AdamW(trainable, lr=lr, weight_decay=0.01)

    t0 = time.time()
    last_metrics: dict[str, float] = {}

    for step in range(1, steps + 1):
        idxs = np.random.choice(len(train_rows), size=min(batch_prompts, len(train_rows)), replace=False)
        rollouts = []
        r_totals, r_accs, r_rags, r_safs, unsafe_flags = [], [], [], [], []

        for idx in idxs:
            row = train_rows[int(idx)]
            p_ids = [1] + [hash_word_to_vin(w, v_in) for w in row["prompt"].split()] + [1]
            prompt_t, gen_out, gen_in, old_lp, mask, texts = _sample_rollouts_for_prompt(
                policy,
                p_ids,
                vout_words,
                vout_hashes,
                group_size=group_size,
                max_gen=28,
                temp=temp,
                top_k=8,
                device=device,
            )
            scores = [
                score_completion(row["prompt"], txt, domain=domain, reference=row["reply"])
                for txt in texts
            ]
            rew_t = torch.tensor([s.total for s in scores], dtype=torch.float32, device=device)
            adv = (rew_t - rew_t.mean()) / (rew_t.std(unbiased=False) + 1e-4)

            with torch.no_grad():
                ref_lp, _ = _sequence_token_logps(ref_model, prompt_t, gen_in, gen_out, temp=temp)

            rollouts.append((prompt_t, gen_out, gen_in, old_lp.detach(), ref_lp.detach(), mask.detach(), adv.detach()))
            r_totals.extend(s.total for s in scores)
            r_accs.extend(s.accuracy for s in scores)
            r_rags.extend(s.rag_grounding for s in scores)
            r_safs.extend(s.safety for s in scores)
            unsafe_flags.extend(1.0 if s.is_unsafe else 0.0 for s in scores)

        # Policy optimization update (2 inner passes so trust-region / soft-gating activates!)
        policy.train()
        pg_val, kl_val, ent_val, loss_val = 0.0, 0.0, 0.0, 0.0
        for _inner in range(inner_epochs):
            opt.zero_grad()
            batch_loss_sum = 0.0
            n_r = float(len(rollouts))
            for prompt_t, gen_out, gen_in, old_lp, ref_lp, mask, adv in rollouts:
                cur_lp, ent = _sequence_token_logps(policy, prompt_t, gen_in, gen_out, temp=temp)
                ratio = torch.exp((cur_lp - old_lp).clamp(-8.0, 8.0))
                adv_2d = adv.unsqueeze(1)  # [G, 1]

                if method == "grpo":
                    s1 = ratio * adv_2d
                    s2 = torch.clamp(ratio, 1.0 - clip_eps, 1.0 + clip_eps) * adv_2d
                    pg_tok = -torch.min(s1, s2)
                else:
                    # SAPO: Soft Adaptive Policy Optimization (smooth asymmetric gating)
                    tau = torch.where(adv_2d >= 0.0, torch.full_like(ratio, sapo_tau_pos), torch.full_like(ratio, sapo_tau_neg))
                    gate = (4.0 / tau) * torch.sigmoid(tau * (ratio - 1.0))
                    pg_tok = -gate * adv_2d

                # Non-negative Schulman KL approximation (k3)
                log_r = (ref_lp - cur_lp).clamp(-8.0, 8.0)
                kl_tok = torch.exp(log_r) - log_r - 1.0

                denom = mask.sum().clamp_min(1.0)
                loss_item = ((pg_tok + kl_beta * kl_tok) * mask).sum() / denom
                (loss_item / n_r).backward()

                with torch.no_grad():
                    loss_val += float(loss_item.item()) / (n_r * inner_epochs)
                    pg_val += float((pg_tok * mask).sum().item() / denom.item()) / (n_r * inner_epochs)
                    kl_val += float((kl_tok * mask).sum().item() / denom.item()) / (n_r * inner_epochs)
                    ent_val += float((ent * mask).sum().item() / denom.item()) / (n_r * inner_epochs)

            torch.nn.utils.clip_grad_norm_(trainable, 1.0)
            opt.step()

        last_metrics = {
            "mean_reward": float(np.mean(r_totals)),
            "r_accuracy": float(np.mean(r_accs)),
            "r_rag": float(np.mean(r_rags)),
            "r_safety": float(np.mean(r_safs)),
            "unsafe_rate": float(np.mean(unsafe_flags)),
            "policy_loss": float(loss_val),
            "pg_surrogate": float(pg_val),
            "kl_div": float(kl_val),
            "entropy": float(ent_val),
        }
        if step_cb:
            step_cb(step, last_metrics)
        if step == 1 or step % 6 == 0 or step == steps:
            print(
                f"  [{domain} {method.upper():4s} step {step:2d}/{steps}] "
                f"R={last_metrics['mean_reward']:.4f} (acc={last_metrics['r_accuracy']:.3f}, rag={last_metrics['r_rag']:.3f}) "
                f"kl={last_metrics['kl_div']:.4f} ent={last_metrics['entropy']:.3f} ({time.time() - t0:.1f}s)"
            )

    return last_metrics
