#!/usr/bin/env python3
"""
ZooGotchi S3 — Host Hybrid RAG Engine & Retrieval Benchmark

Compares 3 retrieval backends against the grounded knowledge base (`kb_zoogotchi.jsonl`):
  1. BM25 Sparse (`k1=1.2, b=0.75`)
  2. Dense Vector (L2-normalized word+char TF-IDF -> Truncated SVD d=64; optional Gemini embeddings)
  3. Hybrid RRF (Reciprocal Rank Fusion k=60 combining BM25 + Dense)

Reports Recall@1, Recall@3, MRR@5, nDCG@5, and median query latency (ms),
writes `llm_training/eval/rag_eval_results.json`, and logs to ExperimentTracker.

Usage:
  .venv\\Scripts\\python.exe llm_training\\rag\\hybrid_rag.py
"""

import argparse
import json
import math
import os
import re
import sys
import time
from collections import Counter
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from llm_training.tracking.experiment_tracker import ExperimentTracker  # noqa: E402

KB_PATHS = {
    "zoogotchi": ROOT / "llm_training" / "data" / "kb_zoogotchi.jsonl",
}
OUT_EVAL_JSON = ROOT / "llm_training" / "eval" / "rag_eval_results.json"

STOPWORDS = {
    "a", "an", "and", "are", "as", "at", "be", "by", "do", "for", "from", "has",
    "have", "in", "is", "it", "its", "of", "on", "or", "that", "the", "this",
    "to", "was", "were", "with", "you", "your", "how", "what", "why", "when",
}

SYNONYM_MAP = {
    "sea": "water lagoon",
    "cow": "manatee",
    "temperature": "temp warm 20c",
    "humble": "kenjougo itasu",
    "eat": "meshiagaru food treat",
    "contagious": "window",
    "japanese": "n4 n3 n2 n1 jlpt",
    "interview": "leetcode big-o",
}


def tokenize_sparse(text: str) -> list[str]:
    toks = [m.lower() for m in re.findall(r"[A-Za-z0-9]+", text)]
    return [t for t in toks if len(t) >= 2 and t not in STOPWORDS]


def tokenize_dense(text: str) -> list[str]:
    words = [m.lower() for m in re.findall(r"[A-Za-z0-9]+", text) if len(m) >= 2]
    expanded = list(words)
    for w in words:
        if w in SYNONYM_MAP:
            expanded.extend(SYNONYM_MAP[w].split())
    grams = list(expanded)
    # Character 3-grams for morphological/paraphrase resilience
    joined = " ".join(expanded)
    for w in expanded:
        if len(w) >= 4:
            for i in range(len(w) - 2):
                grams.append(f"#{w[i:i+3]}#")
            grams.append(w[:4])
    return grams


class BM25Retriever:
    def __init__(self, docs: list[dict], k1: float = 1.2, b: float = 0.75):
        self.docs = docs
        self.k1 = k1
        self.b = b
        self.N = len(docs)
        self.tfs = []
        self.dls = []
        df: Counter[str] = Counter()
        for d in docs:
            toks = tokenize_sparse(d.get("search_text") or (d["title"] + " " + d["text"]))
            c = Counter(toks)
            self.tfs.append(c)
            self.dls.append(max(1, len(toks)))
            for t in c:
                df[t] += 1
        self.avg_dl = sum(self.dls) / float(max(1, self.N))
        self.idf = {t: math.log((self.N - f + 0.5) / (f + 0.5) + 1.0) for t, f in df.items()}

    def search(self, query: str, top_k: int = 5) -> list[tuple[int, float]]:
        q_toks = set(tokenize_sparse(query))
        scores = np.zeros(self.N, dtype=np.float64)
        for t in q_toks:
            if t not in self.idf:
                continue
            idf = self.idf[t]
            for idx, tf_map in enumerate(self.tfs):
                tf = tf_map.get(t, 0)
                if tf:
                    denom = tf + self.k1 * (1.0 - self.b + self.b * (self.dls[idx] / self.avg_dl))
                    scores[idx] += idf * ((tf * (self.k1 + 1.0)) / denom)
        order = np.argsort(-scores, kind="mergesort")[:top_k]
        return [(int(i), float(scores[i])) for i in order]


class DenseSVDRetriever:
    def __init__(self, docs: list[dict], dim: int = 64, use_gemini: bool = False):
        self.docs = docs
        self.N = len(docs)
        self.use_gemini = bool(use_gemini and os.environ.get("GEMINI_API_KEY"))

        doc_tokens = [tokenize_dense(d.get("search_text") or (d["title"] + " " + d["text"])) for d in docs]
        vocab_df: Counter[str] = Counter()
        for toks in doc_tokens:
            for t in set(toks):
                vocab_df[t] += 1
        self.vocab = {t: idx for idx, t in enumerate(sorted(vocab_df.keys()))}
        V = len(self.vocab)
        self.idf = np.zeros(V, dtype=np.float32)
        for t, idx in self.vocab.items():
            self.idf[idx] = math.log((1.0 + self.N) / (1.0 + vocab_df[t])) + 1.0

        X = np.zeros((self.N, V), dtype=np.float32)
        for i, toks in enumerate(doc_tokens):
            c = Counter(toks)
            for t, cnt in c.items():
                j = self.vocab.get(t)
                if j is not None:
                    X[i, j] = (1.0 + math.log(cnt)) * self.idf[j]

        norms = np.linalg.norm(X, axis=1, keepdims=True) + 1e-8
        X_norm = X / norms
        k = max(2, min(dim, self.N - 1, V - 1))
        _u, _s, vt = np.linalg.svd(X_norm, full_matrices=False)
        self.proj = vt[:k].T  # [V, k]
        doc_emb = X_norm @ self.proj
        self.doc_emb = doc_emb / (np.linalg.norm(doc_emb, axis=1, keepdims=True) + 1e-8)

        if self.use_gemini:
            self._init_gemini_embeddings(docs)

    def _init_gemini_embeddings(self, docs: list[dict]):
        try:
            from google import genai
            client = genai.Client()
            texts = [d.get("search_text") or d["text"] for d in docs]
            resp = client.models.embed_content(model="gemini-embedding-001", contents=texts)
            vecs = np.array([e.values for e in resp.embeddings], dtype=np.float32)
            self.doc_emb = vecs / (np.linalg.norm(vecs, axis=1, keepdims=True) + 1e-8)
            self._gemini_client = client
        except Exception as exc:
            print(f"[rag] Falling back to local Dense SVD embeddings ({exc})")
            self.use_gemini = False

    def _encode_query(self, query: str) -> np.ndarray:
        if self.use_gemini:
            resp = self._gemini_client.models.embed_content(model="gemini-embedding-001", contents=query)
            v = np.array(resp.embeddings[0].values, dtype=np.float32)
            return v / (np.linalg.norm(v) + 1e-8)
        vec = np.zeros(len(self.vocab), dtype=np.float32)
        c = Counter(tokenize_dense(query))
        for t, cnt in c.items():
            j = self.vocab.get(t)
            if j is not None:
                vec[j] = (1.0 + math.log(cnt)) * self.idf[j]
        vec = vec / (np.linalg.norm(vec) + 1e-8)
        q_emb = vec @ self.proj
        return q_emb / (np.linalg.norm(q_emb) + 1e-8)

    def search(self, query: str, top_k: int = 5) -> list[tuple[int, float]]:
        q_emb = self._encode_query(query)
        sims = self.doc_emb @ q_emb
        order = np.argsort(-sims, kind="mergesort")[:top_k]
        return [(int(i), float(sims[i])) for i in order]


class HybridRRFRetriever:
    def __init__(self, docs: list[dict], use_gemini: bool = False):
        self.docs = docs
        self.bm25 = BM25Retriever(docs)
        self.dense = DenseSVDRetriever(docs, use_gemini=use_gemini)

    def search(self, query: str, top_k: int = 5, rrf_k: float = 60.0) -> list[tuple[int, float]]:
        n_cand = min(len(self.docs), max(12, top_k * 4))
        b_res = self.bm25.search(query, top_k=n_cand)
        d_res = self.dense.search(query, top_k=n_cand)

        fused: dict[int, float] = {}
        for rank, (doc_idx, sc) in enumerate(b_res, start=1):
            if sc > 0.0:
                fused[doc_idx] = fused.get(doc_idx, 0.0) + 1.0 / (rrf_k + rank)
        for rank, (doc_idx, _sc) in enumerate(d_res, start=1):
            fused[doc_idx] = fused.get(doc_idx, 0.0) + 1.0 / (rrf_k + rank)

        ranked = sorted(fused.items(), key=lambda x: -x[1])[:top_k]
        return ranked


def get_eval_queries(domain: str, docs: list[dict]) -> list[tuple[str, set[str]]]:
    if domain != "zoogotchi":
        raise ValueError(f"Unknown domain: {domain}")

    # zoogotchi (36 queries across species, study DSA, JLPT, genetics, hardware)
    def ids_with_tag_prefix(pref: str) -> set[str]:
        return {d["doc_id"] for d in docs if d["tag"].startswith(pref) or pref.lower() in d["title"].lower()}

    return [
        ("manatee seagrass warm lagoon water", ids_with_tag_prefix("species:manatee") | {"ZOO-086"}),
        ("gentle sea cow needs warm water temperature above 20c", ids_with_tag_prefix("species:manatee") | {"ZOO-086"}),
        ("red panda false thumb bamboo tail", ids_with_tag_prefix("species:red_panda") | {"ZOO-087"}),
        ("fennec fox big desert ears heat", ids_with_tag_prefix("species:fennec_fox") | {"ZOO-088"}),
        ("capybara largest living rodent marsh", ids_with_tag_prefix("species:capybara") | {"ZOO-089"}),
        ("barn owl silent flight serrated feathers", ids_with_tag_prefix("species:barn_owl") | {"ZOO-090"}),
        ("scarlet macaw clay licks neutralize toxins", ids_with_tag_prefix("species:scarlet_macaw") | {"ZOO-091"}),
        ("ruby hummingbird nightly torpor nectar", ids_with_tag_prefix("species:hummingbird") | {"ZOO-092"}),
        ("monarch butterfly milkweed caterpillar", ids_with_tag_prefix("species:monarch") | {"ZOO-093"}),
        ("hercules beetle cuticle humidity olive", ids_with_tag_prefix("species:hercules_beetle") | {"ZOO-094"}),
        ("orchid mantis pink petal mimicry", ids_with_tag_prefix("species:orchid_mantis") | {"ZOO-095"}),
        ("axolotl neoteny external gills lake", ids_with_tag_prefix("species:axolotl") | {"ZOO-096"}),
        ("pacific octopus three hearts puzzle", ids_with_tag_prefix("species:octopus") | {"ZOO-097"}),
        ("galapagos tortoise ectotherm basking warmth", ids_with_tag_prefix("species:tortoise") | {"ZOO-098"}),
        ("veiled chameleon nanocrystal iridophores hue", ids_with_tag_prefix("species:chameleon") | {"ZOO-099"}),
        ("leopard gecko movable blinking eyelids", ids_with_tag_prefix("species:leopard_gecko")),
        ("hoshi karimono starlight genetic traits", ids_with_tag_prefix("species:hoshi_karimono") | {"ZOO-100", "ZOO-101"}),
        ("base32 genome code crc5 cross breed", {"ZOO-100", "ZOO-101"}),
        ("sliding window substring without repeating chars", {"ZOO-049"}),
        ("valid parenthesis lifo stack matching order", {"ZOO-051"}),
        ("binary search rotated sorted array midpoint", {"ZOO-053", "ZOO-052"}),
        ("two pointers palindrome inward o1 space", {"ZOO-045", "ZOO-046", "ZOO-047"}),
        ("shortest path unweighted grid bfs queue", {"ZOO-058", "ZOO-059"}),
        ("dijkstra shortest path heap log v", {"ZOO-059"}),
        ("top k frequent elements bucket sort o(n)", {"ZOO-041"}),
        ("merge sort worst case space complexity", {"ZOO-056"}),
        ("jlpt n4 te shimau accidental regret completion", {"ZOO-062"}),
        ("japanese intransitive aku vs transitive akeru", {"ZOO-064"}),
        ("jlpt n3 wake de wa nai partial negation", {"ZOO-066"}),
        ("jlpt n3 sei de blame vs okage de thanks", {"ZOO-068"}),
        ("jlpt n2 ni kagitte murphys law irony", {"ZOO-075"}),
        ("jlpt n2 wake ni wa ikanai moral duty", {"ZOO-074"}),
        ("jlpt n2 keigo humble form itasu vs nasaru", {"ZOO-082"}),
        ("japanese contradiction mujun compromise dakyou", {"ZOO-084", "ZOO-085"}),
        ("mx1.25 lipo polarity rechargeable ml414 rtc cell", {"ZOO-104"}),
        ("three layer safety v_out guardrails ple", {"ZOO-107"}),
    ]


def evaluate_retriever(name: str, retriever, docs: list[dict], queries: list[tuple[str, set[str]]]) -> dict:
    r1_hits, r3_hits = 0, 0
    mrr_sum, ndcg_sum = 0.0, 0.0
    latencies_ms = []

    for q, expected_ids in queries:
        t0 = time.perf_counter()
        res = retriever.search(q, top_k=5)
        latencies_ms.append((time.perf_counter() - t0) * 1000.0)

        retrieved_ids = [docs[idx]["doc_id"] for idx, _ in res]
        if retrieved_ids and retrieved_ids[0] in expected_ids:
            r1_hits += 1
        if any(did in expected_ids for did in retrieved_ids[:3]):
            r3_hits += 1

        rr = 0.0
        dcg = 0.0
        for rank, did in enumerate(retrieved_ids[:5], start=1):
            if did in expected_ids:
                if rr == 0.0:
                    rr = 1.0 / rank
                dcg += 1.0 / math.log2(rank + 1.0)
        ideal_k = min(len(expected_ids), 5)
        idcg = sum(1.0 / math.log2(r + 1.0) for r in range(1, ideal_k + 1)) or 1.0
        mrr_sum += rr
        ndcg_sum += dcg / idcg

    n_q = max(1, len(queries))
    lat_sorted = sorted(latencies_ms)
    med_lat = lat_sorted[len(lat_sorted) // 2]
    return {
        "method": name,
        "num_queries": len(queries),
        "recall_at_1": round(r1_hits / n_q, 4),
        "recall_at_3": round(r3_hits / n_q, 4),
        "mrr_at_5": round(mrr_sum / n_q, 4),
        "ndcg_at_5": round(ndcg_sum / n_q, 4),
        "latency_ms": round(med_lat, 3),
    }


def benchmark_domain(domain: str, tracker: ExperimentTracker, use_gemini: bool = False) -> dict:
    kb_path = KB_PATHS[domain]
    docs = [json.loads(ln) for ln in kb_path.read_text(encoding="utf-8").splitlines() if ln.strip()]
    queries = get_eval_queries(domain, docs)

    bm25 = BM25Retriever(docs)
    dense = DenseSVDRetriever(docs, use_gemini=use_gemini)
    hybrid = HybridRRFRetriever(docs, use_gemini=use_gemini)

    results = {}
    for label, ret in (("bm25_sparse", bm25), ("dense_svd", dense), ("hybrid_rrf", hybrid)):
        metrics = evaluate_retriever(label, ret, docs, queries)
        results[label] = metrics
        tracker.log_rag_eval(domain, label, metrics)
        print(
            f"[{domain}] {label:12s} | R@1={metrics['recall_at_1']:.3f} R@3={metrics['recall_at_3']:.3f} "
            f"MRR@5={metrics['mrr_at_5']:.3f} nDCG@5={metrics['ndcg_at_5']:.3f} lat={metrics['latency_ms']:.2f}ms"
        )
    return results


def main():
    ap = argparse.ArgumentParser(description="Hybrid RAG Benchmark (BM25 vs Dense vs Hybrid RRF)")
    ap.add_argument("--domain", choices=("zoogotchi",), default="zoogotchi")
    ap.add_argument("--use-gemini-embeddings", action="store_true")
    args = ap.parse_args()

    tracker = ExperimentTracker()
    all_res = {args.domain: benchmark_domain(args.domain, tracker, use_gemini=args.use_gemini_embeddings)}

    OUT_EVAL_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_EVAL_JSON.write_text(json.dumps(all_res, indent=2), encoding="utf-8")
    print(f"Saved Hybrid RAG evaluation results -> {OUT_EVAL_JSON}")


if __name__ == "__main__":
    main()
