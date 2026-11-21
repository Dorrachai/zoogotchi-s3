#!/usr/bin/env python3
"""
ZooGotchi S3 — On-Chip Safe PLE Micro-LLM Local Trainer (RTX 3060 / CUDA / CPU)
and Exact C++ ZPLE INT4 Runtime Parity Verifier.

Matches src/ai/micro_llm_ple.cpp verbatim:
  - V_IN=2048 FNV-1a hashed input embeddings (Per-Layer Embedding)
  - D_MODEL=192, N_LAYERS=6, N_HEADS=4, N_KV_HEADS=4, HIDDEN_DIM=512, MAX_SEQ_LEN=256
  - Group-128 symmetric signed INT4 weights + IEEE754 fp16 scales
  - Asymmetric safe output head V_OUT=1421 (from llm_training/vocab_out.json)
  - 64-byte ZpleHeader + CRC32 payload packed to llm_training/out/zoo_pet.bin

Usage:
  .venv\\Scripts\\python.exe llm_training\\train_ple_zoopet.py --epochs 60 --verify
  .venv\\Scripts\\python.exe llm_training\\train_ple_zoopet.py --verify-only
"""

import argparse
import json
import math
import os
import re
import struct
import time
import zlib
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DATASET = ROOT / "llm_training" / "data" / "zoo_seed_conversations.jsonl"
DEFAULT_VOUT = ROOT / "llm_training" / "vocab_out.json"
DEFAULT_OUT_BIN = ROOT / "llm_training" / "out" / "zoo_pet.bin"

V_IN = 2048
D_MODEL = 192
N_LAYERS = 6
N_HEADS = 4
N_KV_HEADS = 4
HIDDEN_DIM = 512
MAX_SEQ_LEN = 256
GROUP_SIZE = 128
MAX_PARTITION_BYTES = 0x9E0000  # 9.875 MB llm_model partition in partitions_16M.csv

TOKEN_RE = re.compile(r"\.\.\.|[A-Za-z0-9'-]+|[.,!?:;()]")


def hash_word_to_vin(word: str, v_in: int = V_IN) -> int:
    """Exact FNV-1a hash matching hash_word_to_vin() in src/ai/micro_llm_ple.cpp."""
    h = 2166136261
    for ch in word.lower().encode("ascii", "ignore"):
        h ^= ch
        h = (h * 16777619) & 0xFFFFFFFF
    return 4 + (h % (v_in - 4))


def load_vout(path: Path) -> tuple[list[str], dict[str, int]]:
    with open(path, "r", encoding="utf-8") as f:
        vout_words = json.load(f)
    word2out = {w.lower(): i for i, w in enumerate(vout_words)}
    return vout_words, word2out


class RMSNorm(nn.Module):
    def __init__(self, dim: int):
        super().__init__()
        self.weight = nn.Parameter(torch.ones(dim))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.weight * x * torch.rsqrt(x.pow(2).mean(-1, keepdim=True) + 1e-5)


def apply_rope(q: torch.Tensor, k: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
    _b, t, _nh, hd = q.shape
    pos = torch.arange(t, device=q.device).unsqueeze(1)
    dim_idx = torch.arange(0, hd, 2, device=q.device).float()
    freq = 1.0 / (10000.0 ** (dim_idx / hd))
    ang = pos * freq
    cos = torch.cos(ang)[None, :, None, :]
    sin = torch.sin(ang)[None, :, None, :]

    def rot(tensor: torch.Tensor) -> torch.Tensor:
        t0, t1 = tensor[..., 0::2], tensor[..., 1::2]
        return torch.stack([t0 * cos - t1 * sin, t0 * sin + t1 * cos], dim=-1).flatten(-2)

    return rot(q), rot(k)


class PleBlock(nn.Module):
    def __init__(self):
        super().__init__()
        self.att_norm = RMSNorm(D_MODEL)
        self.wq = nn.Linear(D_MODEL, D_MODEL, bias=False)
        self.wk = nn.Linear(D_MODEL, D_MODEL, bias=False)
        self.wv = nn.Linear(D_MODEL, D_MODEL, bias=False)
        self.wo = nn.Linear(D_MODEL, D_MODEL, bias=False)
        self.ffn_norm = RMSNorm(D_MODEL)
        self.w1 = nn.Linear(D_MODEL, HIDDEN_DIM, bias=False)
        self.w3 = nn.Linear(D_MODEL, HIDDEN_DIM, bias=False)
        self.w2 = nn.Linear(HIDDEN_DIM, D_MODEL, bias=False)

    def forward(self, x: torch.Tensor, ple_l: torch.Tensor, mask: torch.Tensor) -> torch.Tensor:
        x = x + ple_l
        bsz, seqlen, dim = x.shape
        hd = dim // N_HEADS
        h = self.att_norm(x)
        q = self.wq(h).view(bsz, seqlen, N_HEADS, hd)
        k = self.wk(h).view(bsz, seqlen, N_HEADS, hd)
        v = self.wv(h).view(bsz, seqlen, N_HEADS, hd)
        q, k = apply_rope(q, k)
        q, k, v = q.transpose(1, 2), k.transpose(1, 2), v.transpose(1, 2)
        att = (q @ k.transpose(-2, -1)) / math.sqrt(hd)
        att = F.softmax(att.masked_fill(mask, float("-inf")), dim=-1)
        y = (att @ v).transpose(1, 2).contiguous().view(bsz, seqlen, dim)
        x = x + self.wo(y)
        h = self.ffn_norm(x)
        x = x + self.w2(F.silu(self.w1(h)) * self.w3(h))
        return x


class ZooPleLLM(nn.Module):
    def __init__(self, v_out: int):
        super().__init__()
        self.ple = nn.Embedding(V_IN, N_LAYERS * D_MODEL)
        self.layers = nn.ModuleList([PleBlock() for _ in range(N_LAYERS)])
        self.out_norm = RMSNorm(D_MODEL)
        self.head = nn.Linear(D_MODEL, v_out, bias=False)

    def forward(self, x_ids: torch.Tensor) -> torch.Tensor:
        bsz, seqlen = x_ids.shape
        ple_all = self.ple(x_ids).view(bsz, seqlen, N_LAYERS, D_MODEL)
        x = torch.zeros(bsz, seqlen, D_MODEL, device=x_ids.device, dtype=ple_all.dtype)
        mask = torch.triu(torch.ones(seqlen, seqlen, device=x_ids.device, dtype=torch.bool), diagonal=1)
        for l_idx, blk in enumerate(self.layers):
            x = blk(x, ple_all[:, :, l_idx, :], mask)
        return self.head(self.out_norm(x))


def encode_sample(prompt: str, reply: str, word2out: dict[str, int]) -> tuple[list[int], list[int]]:
    """
    Exact alignment with micro_llm_ple.cpp pet_brain_generate_sync():
      pos 0..len(p_ids)-1: <bos>(1) + prompt tokens (targets masked -100)
      pos len(p_ids)     : <bos>(1) triggers r_out[0]
      pos len(p_ids)+j   : hash(r_toks[j-1]) predicts r_out[j]
      last pos           : hash(r_toks[-1]) predicts <eos>(2)
    """
    p_ids = [1] + [hash_word_to_vin(w) for w in prompt.split()]
    r_toks = TOKEN_RE.findall(reply)
    r_out = [word2out.get(t.lower(), 3) for t in r_toks] + [2]  # <eos>=2
    r_in = [1] + [hash_word_to_vin(t) for t in r_toks]
    seq_in = (p_ids + r_in)[:MAX_SEQ_LEN]
    targets = ([-100] * len(p_ids) + r_out)[:len(seq_in)]
    if len(targets) < len(seq_in):
        targets += [-100] * (len(seq_in) - len(targets))
    return seq_in, targets


def quantize_q4_matrix(w: np.ndarray, gs: int = GROUP_SIZE) -> tuple[bytes, np.ndarray]:
    """Symmetric INT4 [-8..+7] stored +8 in [0..15] + IEEE754 fp16 scale per group."""
    rows, cols = w.shape
    out = bytearray()
    deq = np.zeros_like(w, dtype=np.float32)
    for r in range(rows):
        for g_start in range(0, cols, gs):
            grp = np.zeros(gs, dtype=np.float32)
            end = min(cols, g_start + gs)
            grp[: end - g_start] = w[r, g_start:end]
            amax = float(np.max(np.abs(grp)))
            scale = np.float16(amax / 7.0 if amax > 1e-8 else 1e-8)
            sc_f = float(scale)
            q = np.clip(np.round(grp / sc_f), -8, 7).astype(np.int8)
            deq[r, g_start:end] = q[: end - g_start] * sc_f
            u = (q + 8).astype(np.uint8)
            packed = (u[0::2] & 0x0F) | ((u[1::2] & 0x0F) << 4)
            out.extend(packed.tobytes())
            out.extend(struct.pack("<H", int(scale.view(np.uint16))))
    return bytes(out), deq


def export_zple_binary(model: ZooPleLLM, v_out: int, out_path: Path) -> float:
    model.eval()
    sd = {k: v.detach().cpu().numpy().astype(np.float32) for k, v in model.state_dict().items()}

    ple_bytes = bytearray()
    ple_w = sd["ple.weight"].reshape(V_IN, N_LAYERS, D_MODEL)
    for t in range(V_IN):
        for l_idx in range(N_LAYERS):
            b, _ = quantize_q4_matrix(ple_w[t, l_idx : l_idx + 1, :], GROUP_SIZE)
            ple_bytes.extend(b)

    dense_bytes = bytearray()
    for l_idx in range(N_LAYERS):
        dense_bytes.extend(sd[f"layers.{l_idx}.att_norm.weight"].astype("<f4").tobytes())
        for k in ("wq", "wk", "wv", "wo"):
            b, _ = quantize_q4_matrix(sd[f"layers.{l_idx}.{k}.weight"], GROUP_SIZE)
            dense_bytes.extend(b)
        dense_bytes.extend(sd[f"layers.{l_idx}.ffn_norm.weight"].astype("<f4").tobytes())
        for k in ("w1", "w3", "w2"):
            b, _ = quantize_q4_matrix(sd[f"layers.{l_idx}.{k}.weight"], GROUP_SIZE)
            dense_bytes.extend(b)
    dense_bytes.extend(sd["out_norm.weight"].astype("<f4").tobytes())

    head_bytes, head_deq = quantize_q4_matrix(sd["head.weight"], GROUP_SIZE)
    head_rmse = float(np.sqrt(np.mean((sd["head.weight"] - head_deq) ** 2)))

    vocab_offset = 64
    ple_offset = vocab_offset
    dense_offset = ple_offset + len(ple_bytes)
    head_offset = dense_offset + len(dense_bytes)
    total_bytes = head_offset + len(head_bytes)
    payload = bytes(ple_bytes) + bytes(dense_bytes) + head_bytes
    crc = zlib.crc32(payload) & 0xFFFFFFFF

    assert total_bytes <= MAX_PARTITION_BYTES, f"Model {total_bytes} exceeds partition {MAX_PARTITION_BYTES}"

    hdr = struct.pack(
        "<4sI10H6I12s",
        b"ZPLE",
        1,
        D_MODEL,
        N_LAYERS,
        N_HEADS,
        N_KV_HEADS,
        HIDDEN_DIM,
        MAX_SEQ_LEN,
        V_IN,
        v_out,
        GROUP_SIZE,
        D_MODEL,
        vocab_offset,
        ple_offset,
        dense_offset,
        head_offset,
        total_bytes,
        crc,
        b"\x00" * 12,
    )
    assert len(hdr) == 64

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(hdr)
        f.write(payload)

    print(
        f"Exported {out_path} ({total_bytes:,} bytes / {total_bytes / 1024 / 1024:.2f} MB) "
        f"| head RMSE={head_rmse:.6f} | crc32=0x{crc:08X}"
    )
    return head_rmse


def _q4_matrix_bytes(rows: int, cols: int, gs: int) -> int:
    groups_per_row = (cols + gs - 1) // gs
    return rows * groups_per_row * ((gs // 2) + 2)


def _dequantize_q4_matrix(buf: bytes | memoryview, rows: int, cols: int, gs: int) -> np.ndarray:
    """Exact mirror of q4_matmul() INT4+fp16 decoding in src/ai/micro_llm_ple.cpp."""
    groups_per_row = (cols + gs - 1) // gs
    grp_bytes = (gs // 2) + 2
    row_bytes = groups_per_row * grp_bytes
    raw = np.frombuffer(buf[: rows * row_bytes], dtype=np.uint8).reshape(rows, groups_per_row, grp_bytes)

    packed = raw[:, :, : gs // 2]
    q0 = (packed & 0x0F).astype(np.int8) - 8
    q1 = ((packed >> 4) & 0x0F).astype(np.int8) - 8
    q_all = np.empty((rows, groups_per_row, gs), dtype=np.float32)
    q_all[:, :, 0::2] = q0
    q_all[:, :, 1::2] = q1

    sc_u16 = raw[:, :, gs // 2 :].copy().view(np.uint16).reshape(rows, groups_per_row, 1)
    scales = sc_u16.view(np.float16).astype(np.float32)
    deq = (q_all * scales).reshape(rows, groups_per_row * gs)
    return deq[:, :cols]


class ZpleNumpyRuntime:
    """Exact NumPy mirror of try_mount_zple_partition() + ple_forward_step()."""

    def __init__(self, bin_path: Path):
        data = bin_path.read_bytes()
        (
            magic,
            version,
            d_model,
            n_layers,
            n_heads,
            n_kv_heads,
            hidden_dim,
            max_seq_len,
            v_in,
            v_out,
            group_size,
            ple_dim,
            vocab_offset,
            ple_offset,
            dense_offset,
            head_offset,
            total_bytes,
            crc,
            _res,
        ) = struct.unpack("<4sI10H6I12s", data[:64])

        assert magic == b"ZPLE", f"Bad magic: {magic!r}"
        assert version == 1, f"Bad version: {version}"
        assert len(data) == total_bytes, f"File size {len(data)} != header total_bytes {total_bytes}"
        assert total_bytes <= MAX_PARTITION_BYTES, f"Exceeds llm_model partition: {total_bytes}"
        actual_crc = zlib.crc32(data[vocab_offset:]) & 0xFFFFFFFF
        assert actual_crc == crc, f"CRC mismatch: 0x{actual_crc:08X} != 0x{crc:08X}"

        self.d_model = d_model
        self.n_layers = n_layers
        self.n_heads = n_heads
        self.n_kv_heads = n_kv_heads
        self.hidden_dim = hidden_dim
        self.max_seq_len = max_seq_len
        self.v_in = v_in
        self.v_out = v_out
        self.group_size = group_size
        self.hd = d_model // n_heads
        self.kvd = self.hd * n_kv_heads

        # Decode PLE table and dense layers matching C++ pointer walking
        ple_row_bytes = n_layers * _q4_matrix_bytes(1, d_model, group_size)
        self.ple_raw = memoryview(data)[ple_offset:dense_offset]
        self.ple_row_bytes = ple_row_bytes

        ptr = dense_offset
        norm_sz = d_model * 4
        wq_sz = _q4_matrix_bytes(d_model, d_model, group_size)
        wk_sz = _q4_matrix_bytes(self.kvd, d_model, group_size)
        wv_sz = _q4_matrix_bytes(self.kvd, d_model, group_size)
        wo_sz = _q4_matrix_bytes(d_model, d_model, group_size)
        w1_sz = _q4_matrix_bytes(hidden_dim, d_model, group_size)
        w3_sz = _q4_matrix_bytes(hidden_dim, d_model, group_size)
        w2_sz = _q4_matrix_bytes(d_model, hidden_dim, group_size)

        self.layers = []
        mv = memoryview(data)
        for _ in range(n_layers):
            att_norm = np.frombuffer(mv[ptr : ptr + norm_sz], dtype="<f4").copy(); ptr += norm_sz
            wq = _dequantize_q4_matrix(mv[ptr : ptr + wq_sz], d_model, d_model, group_size); ptr += wq_sz
            wk = _dequantize_q4_matrix(mv[ptr : ptr + wk_sz], self.kvd, d_model, group_size); ptr += wk_sz
            wv = _dequantize_q4_matrix(mv[ptr : ptr + wv_sz], self.kvd, d_model, group_size); ptr += wv_sz
            wo = _dequantize_q4_matrix(mv[ptr : ptr + wo_sz], d_model, d_model, group_size); ptr += wo_sz
            ffn_norm = np.frombuffer(mv[ptr : ptr + norm_sz], dtype="<f4").copy(); ptr += norm_sz
            w1 = _dequantize_q4_matrix(mv[ptr : ptr + w1_sz], hidden_dim, d_model, group_size); ptr += w1_sz
            w3 = _dequantize_q4_matrix(mv[ptr : ptr + w3_sz], hidden_dim, d_model, group_size); ptr += w3_sz
            w2 = _dequantize_q4_matrix(mv[ptr : ptr + w2_sz], d_model, hidden_dim, group_size); ptr += w2_sz
            self.layers.append((att_norm, wq, wk, wv, wo, ffn_norm, w1, w3, w2))
        self.out_norm = np.frombuffer(mv[ptr : ptr + norm_sz], dtype="<f4").copy(); ptr += norm_sz
        assert ptr == head_offset, f"Dense pointer {ptr} != head_offset {head_offset}"
        self.head = _dequantize_q4_matrix(mv[head_offset:total_bytes], v_out, d_model, group_size)

    def _ple_vec(self, token_in: int, layer_idx: int) -> np.ndarray:
        row_off = (token_in % self.v_in) * self.ple_row_bytes
        one_l_bytes = _q4_matrix_bytes(1, self.d_model, self.group_size)
        l_off = row_off + layer_idx * one_l_bytes
        return _dequantize_q4_matrix(self.ple_raw[l_off : l_off + one_l_bytes], 1, self.d_model, self.group_size)[0]

    @staticmethod
    def _rmsnorm(x: np.ndarray, w: np.ndarray) -> np.ndarray:
        ss = 1.0 / math.sqrt(float(np.mean(x * x)) + 1e-5)
        return w * (ss * x)

    def forward_step(self, token_in: int, pos: int, k_cache: np.ndarray, v_cache: np.ndarray, need_logits: bool) -> np.ndarray | None:
        x = np.zeros(self.d_model, dtype=np.float32)
        hd = self.hd
        kvd = self.kvd
        nh = self.n_heads
        nkv = self.n_kv_heads
        kv_mul = nh // nkv

        for l_idx, (att_norm, wq, wk, wv, wo, ffn_norm, w1, w3, w2) in enumerate(self.layers):
            x = x + self._ple_vec(token_in, l_idx)
            xb = self._rmsnorm(x, att_norm)
            q = wq @ xb
            k = wk @ xb
            v = wv @ xb

            for i in range(0, self.d_model, 2):
                head_dim = i % hd
                freq = 1.0 / (10000.0 ** (float(head_dim) / float(hd)))
                val = float(pos) * freq
                fcr, fci = math.cos(val), math.sin(val)
                q0, q1 = float(q[i]), float(q[i + 1])
                q[i] = q0 * fcr - q1 * fci
                q[i + 1] = q0 * fci + q1 * fcr
                if i < kvd:
                    k0, k1 = float(k[i]), float(k[i + 1])
                    k[i] = k0 * fcr - k1 * fci
                    k[i + 1] = k0 * fci + k1 * fcr

            k_cache[l_idx, pos] = k
            v_cache[l_idx, pos] = v

            xb_att = np.zeros(self.d_model, dtype=np.float32)
            for h_idx in range(nh):
                qh = q[h_idx * hd : (h_idx + 1) * hd]
                kvh = h_idx // kv_mul
                kt = k_cache[l_idx, : pos + 1, kvh * hd : (kvh + 1) * hd]
                scores = (kt @ qh) / math.sqrt(float(hd))
                scores = scores - np.max(scores)
                probs = np.exp(scores)
                probs = probs / np.sum(probs)
                vt = v_cache[l_idx, : pos + 1, kvh * hd : (kvh + 1) * hd]
                xb_att[h_idx * hd : (h_idx + 1) * hd] = probs @ vt

            x = x + (wo @ xb_att)
            xb_f = self._rmsnorm(x, ffn_norm)
            h1 = w1 @ xb_f
            h2 = w3 @ xb_f
            silu_h1 = h1 * (1.0 / (1.0 + np.exp(-np.clip(h1, -60.0, 60.0))))
            x = x + (w2 @ (silu_h1 * h2))

        if not need_logits:
            return None
        xb_out = self._rmsnorm(x, self.out_norm)
        return self.head @ xb_out

    def generate_greedy(self, prompt: str, vout_words: list[str], max_tok: int = 24) -> tuple[str, np.ndarray]:
        k_cache = np.zeros((self.n_layers, self.max_seq_len, self.kvd), dtype=np.float32)
        v_cache = np.zeros((self.n_layers, self.max_seq_len, self.kvd), dtype=np.float32)

        pos = 0
        self.forward_step(1, pos, k_cache, v_cache, need_logits=False); pos += 1
        for w in prompt.split():
            tid = hash_word_to_vin(w, self.v_in)
            self.forward_step(tid, pos, k_cache, v_cache, need_logits=False); pos += 1

        cur_in = 1
        first_logits = None
        words_out = []
        for step in range(max_tok):
            logits = self.forward_step(cur_in, pos, k_cache, v_cache, need_logits=True); pos += 1
            if first_logits is None:
                first_logits = logits.copy()
            # Mirror sample_top_k(temp <= 0.05) + EOS=2 check
            masked = logits.copy()
            masked[0] = -1e9
            masked[1] = -1e9
            masked[3] = -1e9
            nxt = int(np.argmax(masked))
            if nxt == 2 or nxt >= len(vout_words):
                break
            word = vout_words[nxt]
            words_out.append(word)
            cur_in = hash_word_to_vin(word, self.v_in)

        reply = ""
        for w in words_out:
            is_punct = w[0] in {".", ",", "!", "?", ":", ";"}
            if not reply or is_punct:
                reply += w
            else:
                reply += " " + w
        return reply, first_logits


def verify_binary(bin_path: Path, vout_words: list[str], pt_model: ZooPleLLM | None = None):
    print(f"Verifying ZPLE binary parity against micro_llm_ple.cpp: {bin_path}")
    rt = ZpleNumpyRuntime(bin_path)
    test_prompts = [
        ("[SP:manatee] [ST:Calf] [MD:Happy] [TR:SWEET] [TM:DAY] [MEM:walked] Hi buddy! How are you feeling?", "manatee"),
        ("[SP:red_panda] [ST:Cub] [MD:Happy] [TR:PLAYFUL] [TM:MORNING] [MEM:snacked] Want a fresh snack?", "red panda"),
        ("[SP:hoshi_karimono] [ST:Sprout] [MD:Calm] [TR:CURIOUS] [TM:DUSK] [MEM:studied] [LRN:[N2] ~ni kagitte] Ready for a quick study quiz?", "hoshi"),
    ]
    cos_sims = []
    for pr, expected_kw in test_prompts:
        reply, np_logits = rt.generate_greedy(pr, vout_words, max_tok=24)
        assert len(reply) >= 4, f"Empty/tiny reply for {pr}: {reply!r}"
        assert expected_kw.lower() in reply.lower(), f"Expected '{expected_kw}' in decoded reply for {pr}: {reply!r}"
        cos_str = ""
        if pt_model is not None:
            p_ids = [1] + [hash_word_to_vin(w, rt.v_in) for w in pr.split()] + [1]
            with torch.no_grad():
                dev = next(pt_model.parameters()).device
                x_t = torch.tensor([p_ids], dtype=torch.long, device=dev)
                pt_logits = pt_model(x_t)[0, -1].detach().cpu().numpy()
            denom = (np.linalg.norm(pt_logits) * np.linalg.norm(np_logits)) + 1e-12
            cos = float(np.dot(pt_logits, np_logits) / denom)
            cos_sims.append(cos)
            cos_str = f" | cos_sim={cos:.4f}"
            assert cos >= 0.90, f"Quantized-vs-float divergence for {pr}: cos={cos:.4f}"
        print(f"  Prompt: {pr}\n  Decode: {reply}{cos_str}")

    if cos_sims:
        print(f"PARITY PASS: Mean float-vs-INT4 logit cosine similarity = {np.mean(cos_sims):.4f} (>= 0.90)")
    print(f"BINARY CHECK PASS: ZPLE v1 header, CRC32, and C++ runtime mirror verified for {bin_path.name}")


def train(args):
    torch.manual_seed(42)
    np.random.seed(42)
    device = "cuda" if torch.cuda.is_available() and not args.cpu else "cpu"
    print(f"Training device: {device}" + (f" ({torch.cuda.get_device_name(0)})" if device == "cuda" else ""))

    vout_words, word2out = load_vout(Path(args.vocab))
    v_out = len(vout_words)
    assert v_out == 1421, f"Expected V_OUT=1421, got {v_out}"

    encoded = []
    with open(args.dataset, "r", encoding="utf-8") as f:
        for line in f:
            row = json.loads(line)
            seq_in, tgt = encode_sample(row["prompt"], row["reply"], word2out)
            encoded.append((seq_in, tgt))

    model = ZooPleLLM(v_out).to(device)
    n_params = sum(p.numel() for p in model.parameters())
    print(f"Loaded {len(encoded)} samples | Model params: {n_params:,} | V_IN={V_IN} | V_OUT={v_out}")

    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=0.01)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=max(1, args.epochs))

    t0 = time.time()
    bsz = args.batch_size
    final_loss = 0.0
    model.train()
    for epoch in range(1, args.epochs + 1):
        perm = np.random.permutation(len(encoded))
        tot_loss = 0.0
        steps = 0
        for start in range(0, len(encoded), bsz):
            batch_idx = perm[start : start + bsz]
            max_l = max(len(encoded[i][0]) for i in batch_idx)
            xb = torch.zeros((len(batch_idx), max_l), dtype=torch.long)
            yb = torch.full((len(batch_idx), max_l), -100, dtype=torch.long)
            for row_i, s_idx in enumerate(batch_idx):
                s_in, tgt = encoded[s_idx]
                xb[row_i, : len(s_in)] = torch.tensor(s_in, dtype=torch.long)
                yb[row_i, : len(tgt)] = torch.tensor(tgt, dtype=torch.long)
            xb, yb = xb.to(device), yb.to(device)
            logits = model(xb)
            loss = F.cross_entropy(logits.view(-1, v_out), yb.view(-1), ignore_index=-100)
            opt.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            opt.step()
            tot_loss += float(loss.item())
            steps += 1
        sched.step()
        final_loss = tot_loss / max(1, steps)
        if epoch == 1 or epoch % 5 == 0 or epoch == args.epochs:
            print(f"Epoch {epoch:2d}/{args.epochs}: loss={final_loss:.4f} elapsed={time.time() - t0:.1f}s")

    assert final_loss < 0.5, f"Training loss did not converge below 0.5: {final_loss:.4f}"
    out_path = Path(args.out)
    export_zple_binary(model, v_out, out_path)
    if args.verify:
        verify_binary(out_path, vout_words, pt_model=model)


def main():
    ap = argparse.ArgumentParser(description="ZooGotchi S3 Local ZPLE Trainer & Parity Verifier")
    ap.add_argument("--dataset", default=str(DEFAULT_DATASET))
    ap.add_argument("--vocab", default=str(DEFAULT_VOUT))
    ap.add_argument("--out", default=str(DEFAULT_OUT_BIN))
    ap.add_argument("--epochs", type=int, default=45)
    ap.add_argument("--batch-size", type=int, default=32)
    ap.add_argument("--lr", type=float, default=1.5e-3)
    ap.add_argument("--cpu", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--verify-only", action="store_true")
    args = ap.parse_args()

    vout_words, _ = load_vout(Path(args.vocab))
    if args.verify_only:
        verify_binary(Path(args.out), vout_words)
    else:
        train(args)


if __name__ == "__main__":
    main()
