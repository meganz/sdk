#!/usr/bin/env python3
"""Diff two ``bench_report_*.json`` files emitted by the bench framework.

The JSON schema (see ``tests/integration/BENCHMARKS.md``)::

    {
      "schema_version": 1, "session_pid": N,
      "cells": [
        {"name": "...", "file_size_mib": N, "connections": N,
         "duration_ms": N, "aggregate_kbps": N,
         "first_byte_ms": N, "last_byte_ms": N,
         "rss_delta_kb": N, "user_cpu_ms": N, "sys_cpu_ms": N,
         "chunk_ms_min": N, "chunk_ms_max": N,
         "chunk_ms_mean": N, "chunk_ms_median": N, "chunk_ms_p95": N,
         "chunk_n": N}, ...]
    }

Output is a markdown table per cell, sorted by ``Δ aggregate_kbps``
descending, with cells where ``|Δ throughput| > 5%`` prefixed
``⚠️``. Cells present in only one file are listed in a
separate section.

Usage::

    scripts/ci/compare_bench.py bench_report_A.json bench_report_B.json
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def load_cells(path: Path) -> dict[str, dict[str, Any]]:
    """Return ``{cell_name: cell_dict}`` from a bench report file."""
    try:
        data = json.loads(path.read_text())
    except FileNotFoundError:
        print(f"error: file not found: {path}", file=sys.stderr)
        sys.exit(2)
    except json.JSONDecodeError as exc:
        print(f"error: invalid JSON in {path}: {exc}", file=sys.stderr)
        sys.exit(2)
    cells = data.get("cells", [])
    return {c["name"]: c for c in cells if "name" in c}


def pct_delta(a: float | int | None, b: float | int | None) -> float | None:
    """Return percent change from ``a`` to ``b``, or ``None`` if undefined."""
    if a is None or b is None:
        return None
    try:
        a = float(a)
        b = float(b)
    except (TypeError, ValueError):
        return None
    if a == 0:
        return None
    return (b - a) / a * 100.0


def fmt_pct(d: float | None) -> str:
    return "n/a" if d is None else f"{d:+.1f}%"


def fmt_val(v: Any) -> str:
    if v is None:
        return "n/a"
    if isinstance(v, float):
        return f"{v:.1f}"
    return str(v)


def row(name: str, a: dict[str, Any], b: dict[str, Any]) -> tuple[float, str]:
    """Return ``(throughput_delta, markdown_row)`` for one cell."""
    fields = [
        ("aggregate_kbps", "throughput"),
        ("duration_ms", "duration"),
        ("rss_delta_kb", "rss_delta_kb"),
        ("user_cpu_ms", "user_cpu_ms"),
    ]
    deltas: dict[str, float | None] = {}
    for key, _ in fields:
        deltas[key] = pct_delta(a.get(key), b.get(key))
    thru_delta = deltas["aggregate_kbps"] or 0.0
    flag = "⚠️ " if abs(thru_delta) > 5.0 else ""
    parts = [f"| {flag}{name}"]
    for key, _ in fields:
        parts.append(f"| {fmt_val(a.get(key))} | {fmt_val(b.get(key))} | {fmt_pct(deltas[key])}")
    parts.append("|")
    return thru_delta, " ".join(parts)


def emit_table(common: list[str], a_cells: dict, b_cells: dict) -> None:
    rows: list[tuple[float, str]] = []
    for name in common:
        rows.append(row(name, a_cells[name], b_cells[name]))
    rows.sort(key=lambda x: x[0], reverse=True)
    header = (
        "| cell | A throughput | B throughput | Δ % "
        "| A duration | B duration | Δ % "
        "| A rss_delta_kb | B rss_delta_kb | Δ % "
        "| A user_cpu_ms | B user_cpu_ms | Δ % |"
    )
    sep = "|---|" + "|".join(["---"] * 12) + "|"
    print(header)
    print(sep)
    for _, line in rows:
        print(line)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Diff two bench_report_*.json files; emit a markdown table.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("a", type=Path, help="baseline bench_report JSON")
    parser.add_argument("b", type=Path, help="candidate bench_report JSON")
    args = parser.parse_args()

    a_cells = load_cells(args.a)
    b_cells = load_cells(args.b)

    common = sorted(set(a_cells) & set(b_cells))
    only_a = sorted(set(a_cells) - set(b_cells))
    only_b = sorted(set(b_cells) - set(a_cells))

    print(f"# Bench comparison: A=`{args.a.name}` vs B=`{args.b.name}`")
    print()
    print(f"- common cells: {len(common)}")
    print(f"- only in A:    {len(only_a)}")
    print(f"- only in B:    {len(only_b)}")
    print()

    if common:
        print("## Per-cell deltas (sorted by Δ throughput desc; ⚠️ marks |Δ| > 5%)")
        print()
        emit_table(common, a_cells, b_cells)
        print()

    if only_a:
        print("## Cells only in A")
        print()
        for n in only_a:
            print(f"- {n}")
        print()

    if only_b:
        print("## Cells only in B")
        print()
        for n in only_b:
            print(f"- {n}")
        print()


if __name__ == "__main__":
    main()
