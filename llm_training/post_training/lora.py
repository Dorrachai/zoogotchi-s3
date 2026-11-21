#!/usr/bin/env python3
"""
ZooGotchi S3 — Pure-PyTorch LoRA Adapter Layer
Supports freezing base Transformer matrices, training low-rank adapters, saving/loading
compact adapter checkpoints, and folding deltas cleanly into nn.Linear for INT4 export.
"""

import math
from pathlib import Path

import torch
import torch.nn as nn


class LoRALinear(nn.Module):
    def __init__(self, base: nn.Linear, rank: int = 8, alpha: float = 16.0, dropout: float = 0.05):
        super().__init__()
        self.base = base
        self.rank = int(rank)
        self.alpha = float(alpha)
        self.scaling = float(alpha) / float(rank)
        self.drop = nn.Dropout(p=dropout) if dropout > 0.0 else nn.Identity()

        in_f = base.in_features
        out_f = base.out_features
        dev = base.weight.device
        dt = base.weight.dtype

        self.lora_A = nn.Parameter(torch.empty((self.rank, in_f), device=dev, dtype=dt))
        self.lora_B = nn.Parameter(torch.zeros((out_f, self.rank), device=dev, dtype=dt))
        nn.init.kaiming_uniform_(self.lora_A, a=math.sqrt(5))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        out = self.base(x)
        delta = (self.drop(x) @ self.lora_A.t() @ self.lora_B.t()) * self.scaling
        return out + delta

    @torch.no_grad()
    def merge_into_linear(self) -> nn.Linear:
        merged = nn.Linear(
            self.base.in_features,
            self.base.out_features,
            bias=(self.base.bias is not None),
            device=self.base.weight.device,
            dtype=self.base.weight.dtype,
        )
        merged.weight.copy_(self.base.weight + (self.lora_B @ self.lora_A) * self.scaling)
        if self.base.bias is not None and merged.bias is not None:
            merged.bias.copy_(self.base.bias)
        return merged


DEFAULT_TARGETS = ("wq", "wk", "wv", "wo", "w1", "w2", "w3")


def inject_lora(
    model: nn.Module,
    rank: int = 8,
    alpha: float = 16.0,
    dropout: float = 0.05,
    target_names: tuple[str, ...] = DEFAULT_TARGETS,
) -> int:
    replaced = 0
    for mod in model.modules():
        for child_name, child in list(mod.named_children()):
            if child_name in target_names and isinstance(child, nn.Linear):
                setattr(mod, child_name, LoRALinear(child, rank=rank, alpha=alpha, dropout=dropout))
                replaced += 1
    return replaced


def freeze_base_enable_lora(model: nn.Module, unfreeze_head: bool = False) -> tuple[int, int]:
    for p in model.parameters():
        p.requires_grad = False
    for name, p in model.named_parameters():
        if "lora_A" in name or "lora_B" in name:
            p.requires_grad = True
        elif unfreeze_head and (name.startswith("head.") or name.startswith("out_norm.")):
            p.requires_grad = True
    trainable = sum(p.numel() for p in model.parameters() if p.requires_grad)
    total = sum(p.numel() for p in model.parameters())
    return trainable, total


def extract_lora_state_dict(model: nn.Module) -> dict[str, torch.Tensor]:
    return {k: v.detach().cpu().clone() for k, v in model.state_dict().items() if "lora_" in k}


def save_lora_adapter(model: nn.Module, out_path: Path, metadata: dict | None = None):
    out_path.parent.mkdir(parents=True, exist_ok=True)
    torch.save({"lora_state_dict": extract_lora_state_dict(model), "metadata": metadata or {}}, out_path)


def merge_and_unload_lora(model: nn.Module) -> int:
    merged_cnt = 0
    for mod in model.modules():
        for child_name, child in list(mod.named_children()):
            if isinstance(child, LoRALinear):
                setattr(mod, child_name, child.merge_into_linear())
                merged_cnt += 1
    return merged_cnt
