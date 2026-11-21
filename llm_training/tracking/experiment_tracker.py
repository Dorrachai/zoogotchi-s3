#!/usr/bin/env python3
"""
ZooGotchi S3 — Local Experiment Tracker + Model Lineage Catalog
Zero-dependency SQLite (`experiments.db`) + JSON (`catalog.json`).
"""

import json
import sqlite3
import subprocess
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
REGISTRY_DIR = ROOT / "llm_training" / "registry"
DEFAULT_DB = REGISTRY_DIR / "experiments.db"
DEFAULT_CATALOG = REGISTRY_DIR / "catalog.json"


def _git_short_sha() -> str:
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=str(ROOT),
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=2,
        ).strip()
        return out or "workspace"
    except Exception:
        return "workspace"


class ExperimentTracker:
    def __init__(self, db_path: Path = DEFAULT_DB, catalog_path: Path = DEFAULT_CATALOG):
        self.db_path = Path(db_path)
        self.catalog_path = Path(catalog_path)
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self._init_schema()

    def _conn(self) -> sqlite3.Connection:
        con = sqlite3.connect(str(self.db_path))
        con.row_factory = sqlite3.Row
        return con

    def _init_schema(self):
        with self._conn() as con:
            con.executescript(
                """
                CREATE TABLE IF NOT EXISTS runs (
                    run_id TEXT PRIMARY KEY,
                    domain TEXT NOT NULL,
                    stage TEXT NOT NULL,
                    parent_run_id TEXT DEFAULT '',
                    git_sha TEXT NOT NULL,
                    dataset_sha256 TEXT DEFAULT '',
                    status TEXT NOT NULL,
                    config_json TEXT NOT NULL,
                    summary_json TEXT DEFAULT '{}',
                    artifact_path TEXT DEFAULT '',
                    started_at REAL NOT NULL,
                    finished_at REAL DEFAULT 0
                );
                CREATE TABLE IF NOT EXISTS step_metrics (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    run_id TEXT NOT NULL,
                    step INTEGER NOT NULL,
                    metric_name TEXT NOT NULL,
                    value REAL NOT NULL,
                    ts REAL NOT NULL
                );
                CREATE INDEX IF NOT EXISTS idx_step_run ON step_metrics(run_id, metric_name, step);
                CREATE TABLE IF NOT EXISTS rag_evals (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    domain TEXT NOT NULL,
                    method TEXT NOT NULL,
                    recall_at_1 REAL NOT NULL,
                    recall_at_3 REAL NOT NULL,
                    mrr_at_5 REAL NOT NULL,
                    ndcg_at_5 REAL NOT NULL,
                    latency_ms REAL NOT NULL,
                    num_queries INTEGER NOT NULL,
                    ts REAL NOT NULL
                );
                """
            )

    def start_run(
        self,
        domain: str,
        stage: str,
        config: dict,
        dataset_sha256: str = "",
        parent_run_id: str = "",
    ) -> str:
        run_id = f"{domain}-{stage}-{time.strftime('%Y%m%d-%H%M%S')}-{uuid.uuid4().hex[:6]}"
        with self._conn() as con:
            con.execute(
                """
                INSERT INTO runs (run_id, domain, stage, parent_run_id, git_sha, dataset_sha256, status, config_json, started_at)
                VALUES (?, ?, ?, ?, ?, ?, 'running', ?, ?)
                """,
                (
                    run_id,
                    domain,
                    stage,
                    parent_run_id,
                    _git_short_sha(),
                    dataset_sha256,
                    json.dumps(config, sort_keys=True),
                    time.time(),
                ),
            )
        return run_id

    def log_step(self, run_id: str, step: int, metrics: dict[str, float]):
        now = time.time()
        rows = [(run_id, int(step), str(k), float(v), now) for k, v in metrics.items()]
        with self._conn() as con:
            con.executemany(
                "INSERT INTO step_metrics (run_id, step, metric_name, value, ts) VALUES (?, ?, ?, ?, ?)",
                rows,
            )

    def finish_run(
        self,
        run_id: str,
        status: str = "completed",
        eval_metrics: dict | None = None,
        artifact_path: str = "",
    ):
        with self._conn() as con:
            con.execute(
                """
                UPDATE runs
                SET status = ?, summary_json = ?, artifact_path = ?, finished_at = ?
                WHERE run_id = ?
                """,
                (status, json.dumps(eval_metrics or {}, sort_keys=True), artifact_path, time.time(), run_id),
            )

    def register_model_variant(
        self,
        run_id: str,
        domain: str,
        stage: str,
        artifact_path: str,
        eval_metrics: dict,
        parent_run_id: str = "",
        extra: dict | None = None,
    ):
        if self.catalog_path.is_file():
            cat = json.loads(self.catalog_path.read_text(encoding="utf-8"))
        else:
            cat = {"schema_version": 1, "variants": [], "latest_by_domain_stage": {}}

        entry = {
            "run_id": run_id,
            "domain": domain,
            "stage": stage,
            "parent_run_id": parent_run_id,
            "git_sha": _git_short_sha(),
            "artifact_path": artifact_path,
            "metrics": eval_metrics,
            "extra": extra or {},
            "registered_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }
        cat["variants"].append(entry)
        cat.setdefault("latest_by_domain_stage", {})[f"{domain}:{stage}"] = entry
        self.catalog_path.parent.mkdir(parents=True, exist_ok=True)
        self.catalog_path.write_text(json.dumps(cat, indent=2), encoding="utf-8")

    def log_rag_eval(self, domain: str, method: str, m: dict):
        with self._conn() as con:
            con.execute(
                """
                INSERT INTO rag_evals (domain, method, recall_at_1, recall_at_3, mrr_at_5, ndcg_at_5, latency_ms, num_queries, ts)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    domain,
                    method,
                    float(m["recall_at_1"]),
                    float(m["recall_at_3"]),
                    float(m["mrr_at_5"]),
                    float(m["ndcg_at_5"]),
                    float(m["latency_ms"]),
                    int(m["num_queries"]),
                    time.time(),
                ),
            )

    def get_latest_by_stage(self, domain: str) -> dict[str, dict]:
        out = {}
        with self._conn() as con:
            rows = con.execute(
                "SELECT * FROM runs WHERE domain = ? AND status = 'completed' ORDER BY started_at ASC",
                (domain,),
            ).fetchall()
            for r in rows:
                d = dict(r)
                d["config"] = json.loads(d["config_json"])
                d["summary"] = json.loads(d["summary_json"] or "{}")
                out[d["stage"]] = d
        return out

    def get_step_history(self, run_id: str, metric_name: str | None = None) -> list[tuple[int, str, float]]:
        with self._conn() as con:
            if metric_name:
                rows = con.execute(
                    "SELECT step, metric_name, value FROM step_metrics WHERE run_id = ? AND metric_name = ? ORDER BY step ASC",
                    (run_id, metric_name),
                ).fetchall()
            else:
                rows = con.execute(
                    "SELECT step, metric_name, value FROM step_metrics WHERE run_id = ? ORDER BY step ASC",
                    (run_id,),
                ).fetchall()
            return [(int(r[0]), str(r[1]), float(r[2])) for r in rows]

    def get_latest_rag_evals(self, domain: str) -> dict[str, dict]:
        out = {}
        with self._conn() as con:
            rows = con.execute(
                "SELECT * FROM rag_evals WHERE domain = ? ORDER BY ts ASC",
                (domain,),
            ).fetchall()
            for r in rows:
                out[r["method"]] = dict(r)
        return out
