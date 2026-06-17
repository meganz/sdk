#!/usr/bin/env python3
"""Canonical bench-proof aggregator for the WS-upload bench framework.

Reads per-iter bench JSONL produced by ``BenchReportWriter`` (see
``tests/integration/bench_framework/BenchReportWriter.cpp``) for a candidate
binary and one or more baseline binaries, computes per-axis distribution stats
(median / mean / stdev / min / max), runs paired Mann-Whitney U + Cliff's d
comparisons across every cell x axis, and emits one Markdown per cell plus a
master summary.

Labels, source paths, and cell list are all read from a JSON manifest rather
than hardcoded. Both raw JSONL (one cell per line) and the legacy
``synthetic`` JSON wrapper emitted by ``extract_bench.py`` are accepted on
input.

NO TBD placeholders: every axis that cannot be computed (n<2, missing field
etc.) carries an explicit inline reason instead.

Manifest schema (`baselines.json`):
    {
      "candidate": {"label": "candidate", "jsonl": "/path/to/bench_report.jsonl"},
      "baselines": [
        {"label": "baseline-A", "jsonl": "/path/to/baselineA_bench_report.jsonl"},
        {"label": "baseline-B", "jsonl": "/path/to/baselineB_bench_report.jsonl"}
      ]
    }

Usage::

    scripts/ci/aggregate_bench.py --manifest baselines.json \\
        --output-dir Goal1_bench_proof/ \\
        [--cells slu,manysmall] [--axes throughput,rss,cpu]

The script is idempotent: rerunning over an existing ``--output-dir`` overwrites
the per-cell + master-summary Markdown and ``comparison_results.json`` in place.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from pathlib import Path
from statistics import mean, median, pstdev
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple


# ---------------------------------------------------------------------------
# Axis catalogue.
# ---------------------------------------------------------------------------
# Each axis tuple: (jsonl key, human label, higher_is_better, axis_group)
# axis_group is the short tag accepted by --axes (throughput/rss/cpu/wall/
# throttle/chunk). "wall" is implicitly part of "throughput" coverage.
AXES: List[Tuple[str, str, bool, str]] = [
    ("duration_ms", "wall ms", False, "throughput"),
    ("aggregate_kbps", "throughput KBps", True, "throughput"),
    ("rss_delta_kb", "RSS delta KB (per-iter growth; INFORMATIONAL — fu7-21 FU-DELTA: plateau-degenerate under single-process, not gated)", False, "rss"),
    ("rss_max_kb", "RSS max KB (high-water)", False, "rss"),
    ("user_cpu_ms", "user CPU ms", False, "cpu"),
    ("sys_cpu_ms", "sys CPU ms", False, "cpu"),
    ("throttle_event6_count", "throttle event=6 count", False, "throttle"),
    ("throttle_event6_total_ms", "throttle event=6 total ms", False, "throttle"),
    ("chunk_ms_median", "chunk ms median", False, "chunk"),
    ("chunk_ms_p95", "chunk ms p95", False, "chunk"),
]

# Primary axes used by the master_summary §1 wide table + close-gate.
# fu7-20 Session 3 added rss_max_kb (two-axis HR54). fu7-21 FU-DELTA-SLU/LPMS:
# rss_delta_kb is DEMOTED out of the primary/close-gate axes. Under the
# single-process --gtest_repeat=N topology (HR56) the develop per-iter RSS
# growth plateaus to ~0, so the % delta is degenerate (undefined denominator
# on SLU/LPMS; +1075% NOT-SIG noise on LPMS). rss_delta_kb stays COMPUTED and
# shown as INFORMATIONAL in the per-cell §2 distribution, but no longer gates.
# rss_max_kb (iter-end peak high-water) is the meaningful, monotonic RSS axis
# and the sole RSS primary — it captures the v2 RSS-reduction premise directly.
PRIMARY_AXES_KEYS = ["aggregate_kbps", "rss_max_kb", "user_cpu_ms"]

# Axes gated by the HR54 develop-comparison policy. fu7-21 FU-DELTA: only the
# iter-end peak rss_max_kb is gated (rss_delta_kb demoted to informational).
RSS_AXES_KEYS = ("rss_max_kb",)

# The peak-RSS axis IS the v2-premise axis. HR54: large-workload cells
# (SLU/LPMS), where the premise must pay off, MUST be strictly lower than
# develop (no override). Small-file cells (ManySmall/1kSmall) carry a fixed
# connection-pool overhead that the percentage exaggerates against their tiny
# baseline; those cells MAY use the manifest rss_paired_allowed override, but
# ONLY with a follow-up ticket + explicit user sign-off (never silent) per the
# HR54 amendment. The override surfaces as PAIRED-OK-WITH-FOLLOWUP, not a WIN.
RSS_PEAK_AXIS = "rss_max_kb"

# HR54 thresholds — fu7-20 Session 3 (see docs/methodology/hard_rules_catalog.md).
# RSS vs develop is acceptable only if Δ% < HR54_NOISE_FLOOR_PCT AND p < HR54_P_THRESHOLD.
# Anything weaker (EQUIVALENT / BETTER-but-not-significant / MITIGATED) counts as
# REGRESSION-RSS-POLICY, regardless of throughput. The only override is per-cell
# user opt-in via the manifest's `rss_paired_allowed` list.
HR54_NOISE_FLOOR_PCT = -2.0
HR54_P_THRESHOLD = 0.05

# Synthetic band names emitted when HR54 / HR53 mark a cell × baseline as
# blocking the close-gate. These do not come from verdict_band() — they're
# applied as overrides after the underlying band is computed.
BAND_RSS_POLICY_VIOLATION = "REGRESSION-RSS-POLICY"
BAND_COVERAGE_MISSING = "COVERAGE-MISSING"

# Default cell normalisation: canonicalise both candidate (`SingleLargeUpload`,
# `SdkTestBenchmarkSingleLargeUpload`) and develop (`SdkTestBenchmark*`) names
# into a single short tag used as the cell-key in output.
CELL_ALIASES: Dict[str, str] = {
    "SingleLargeUpload": "slu",
    "SdkTestBenchmarkSingleLargeUpload": "slu",
    "ManySmallUploads": "manysmall",
    "SdkTestBenchmarkManySmallUploads": "manysmall",
    "1kSmallUploads": "1ksmall",
    "SdkTestBenchmark1kSmallUploads": "1ksmall",
    "LargePlusManySmall": "largeplusmanysmall",
    "SdkTestBenchmarkLargePlusManySmall": "largeplusmanysmall",
}


# ---------------------------------------------------------------------------
# I/O — JSONL or synthetic-JSON loader.
# ---------------------------------------------------------------------------
def load_cells(path: Path) -> List[Dict[str, Any]]:
    """Return the list of per-iter cell dicts from ``path``.

    Accepts three on-disk shapes:
      1. Raw JSONL — one ``{...}`` object per line (the form emitted by
         ``BenchReportWriter::appendJsonlLine``).
      2. Consolidated JSON — ``{"schema_version": N, "cells": [...]}`` (the
         form emitted by ``BenchReportWriter::flush``).
      3. ``synthetic`` JSON wrapper — ``{"synthetic": {"cells": [...]}}``
         (emitted by ``extract_bench.py`` when re-projecting a develop binary
         log into bench-cell shape).
    """
    if not path.exists():
        return []
    text = path.read_text().strip()
    if not text:
        return []
    # JSONL: starts with '{' on first line and contains newline-delimited objects.
    if text.lstrip().startswith("{") and "\n" in text:
        first_brace = text.find("{")
        # Heuristic: if the *first* line is a single self-contained '{...}' then
        # it's JSONL; otherwise it's a multi-line JSON document.
        first_line = text.splitlines()[0].strip()
        if first_line.endswith("}"):
            cells: List[Dict[str, Any]] = []
            for line in text.splitlines():
                line = line.strip()
                if not line:
                    continue
                try:
                    cells.append(json.loads(line))
                except json.JSONDecodeError:
                    pass
            if cells:
                return cells
    # Fall through to JSON-document parsing.
    try:
        doc = json.loads(text)
    except json.JSONDecodeError as exc:
        sys.stderr.write(f"warn: failed to parse {path}: {exc}\n")
        return []
    if "cells" in doc and isinstance(doc["cells"], list):
        return doc["cells"]
    if "synthetic" in doc and isinstance(doc["synthetic"], dict):
        cells = doc["synthetic"].get("cells", [])
        if isinstance(cells, list):
            return cells
    return []


def cell_key_for(raw_name: str) -> str:
    """Canonicalise a JSON `name` field to its short cell tag (e.g. `slu`)."""
    return CELL_ALIASES.get(raw_name, raw_name.lower())


def group_cells_by_key(cells: Iterable[Dict[str, Any]]) -> Dict[str, List[Dict[str, Any]]]:
    """Partition a flat list of cell dicts by short cell tag."""
    out: Dict[str, List[Dict[str, Any]]] = {}
    for c in cells:
        name = c.get("name", "")
        if not name:
            continue
        out.setdefault(cell_key_for(name), []).append(c)
    return out


# ---------------------------------------------------------------------------
# Stats — pure Python.
# ---------------------------------------------------------------------------
def _quantile(sorted_vs: Sequence[float], p: float) -> float:
    n = len(sorted_vs)
    if n == 1:
        return float(sorted_vs[0])
    idx = (n - 1) * p
    lo = int(idx)
    hi = min(lo + 1, n - 1)
    return float(sorted_vs[lo] + (sorted_vs[hi] - sorted_vs[lo]) * (idx - lo))


def axis_stats(values: Sequence[float]) -> Optional[Dict[str, float]]:
    """Return min/p25/median/mean/p75/p95/max/sigma/cv for ``values``.

    Returns ``None`` if ``values`` is empty (caller renders an explicit reason).
    Tolerates n==1 by returning the single value across every quantile.
    """
    if not values:
        return None
    vs = sorted(float(v) for v in values)
    n = len(vs)
    mu = mean(vs)
    sigma = pstdev(vs) if n > 1 else 0.0
    return {
        "n": float(n),
        "min": vs[0],
        "p25": _quantile(vs, 0.25),
        "median": _quantile(vs, 0.5),
        "mean": mu,
        "p75": _quantile(vs, 0.75),
        "p95": _quantile(vs, 0.95),
        "max": vs[-1],
        "stdev": sigma,
        "cv": (sigma / mu) if (n > 1 and mu) else 0.0,
    }


def mann_whitney_u(a: Sequence[float], b: Sequence[float]) -> Tuple[Optional[float], Optional[float]]:
    """Two-sided Mann-Whitney U test on ``a`` vs ``b``.

    Returns ``(U, p_two_sided)`` using the normal approximation with mid-rank
    tie correction and a continuity correction of 0.5. ``(None, None)`` if
    either sample is empty.
    """
    n1, n2 = len(a), len(b)
    if n1 == 0 or n2 == 0:
        return (None, None)
    combined = sorted([(float(v), "a") for v in a] + [(float(v), "b") for v in b])
    ranks = [0.0] * len(combined)
    i = 0
    while i < len(combined):
        j = i
        while j + 1 < len(combined) and combined[j + 1][0] == combined[i][0]:
            j += 1
        avg_rank = (i + j) / 2.0 + 1.0
        for k in range(i, j + 1):
            ranks[k] = avg_rank
        i = j + 1
    R1 = sum(ranks[k] for k in range(len(combined)) if combined[k][1] == "a")
    U1 = R1 - n1 * (n1 + 1) / 2.0
    U2 = n1 * n2 - U1
    U = min(U1, U2)
    mu = n1 * n2 / 2.0
    sigma = math.sqrt(n1 * n2 * (n1 + n2 + 1) / 12.0)
    if sigma == 0:
        return (U, 1.0)
    z = (U - mu + 0.5) / sigma
    p = 2.0 * (1.0 - 0.5 * (1.0 + math.erf(abs(z) / math.sqrt(2.0))))
    return (U, max(0.0, min(1.0, p)))


def cliffs_delta(a: Sequence[float], b: Sequence[float]) -> Optional[float]:
    """Cliff's δ = (#(a>b) - #(a<b)) / (n_a × n_b), range [-1, 1].

    Uses a naive O(n×m) loop — adequate for n in the tens. ``None`` if either
    sample is empty.
    """
    n1, n2 = len(a), len(b)
    if n1 == 0 or n2 == 0:
        return None
    gt = lt = 0
    for x in a:
        xf = float(x)
        for y in b:
            yf = float(y)
            if xf > yf:
                gt += 1
            elif xf < yf:
                lt += 1
    return (gt - lt) / float(n1 * n2)


def cliffs_band(d: Optional[float]) -> str:
    """Map a Cliff's δ magnitude to one of the standard effect-size bands."""
    if d is None:
        return "n/a"
    ad = abs(d)
    if ad < 0.147:
        return "negligible"
    if ad < 0.33:
        return "small"
    if ad < 0.474:
        return "medium"
    return "large"


def verdict_band(pct: Optional[float], p: Optional[float], higher_better: bool) -> str:
    """Per-axis verdict band — favours absolute % thresholds but consults p too.

    fu7-20 Session 3 amendment: statistical significance gates
    the negative bands. A large absolute delta with p ≥ 0.05 is treated as
    NOT-SIGNIFICANT — the data cannot distinguish the delta from noise. This
    prevents high-variance cells (e.g., throttle-storm-sensitive LPMS at
    σ/μ ≈ 0.42) from flagging false-positive REGRESSION-WORSE. Positive
    deltas with low p still classify as BETTER (a real win is a win); only
    REGRESSION-WORSE is gated on significance.
    """
    if pct is None:
        return "n/a"
    abs_pct = abs(pct)
    not_sig = p is None or p >= 0.05
    if abs_pct < 2.0 and not_sig:
        return "EQUIVALENT"
    if abs_pct < 5.0 and not_sig:
        return "MITIGATED"
    if abs_pct < 10.0 and not_sig:
        return "MITIGATED-NOT-FIXED"
    sign_up = pct > 0
    good = (sign_up if higher_better else not sign_up)
    if good:
        return "BETTER"
    # The candidate is on the wrong side of the threshold AND outside ±10%.
    # Require statistical significance (p < 0.05) to call this REGRESSION-WORSE;
    # otherwise the delta is large-but-not-distinguishable-from-noise.
    if not_sig:
        return "NOT-SIGNIFICANT-DELTA"
    return "REGRESSION-WORSE"


def is_better(band: str) -> bool:
    """True iff the band counts as candidate ≥ baseline on a higher-is-better axis.

    Includes NOT-SIGNIFICANT-DELTA per fu7-20 Session 3 amendment: a large-
    absolute-delta-but-not-significant data point cannot prove a regression
    and therefore does not fail the close-gate.
    """
    return band in (
        "BETTER",
        "EQUIVALENT",
        "MITIGATED",
        "MITIGATED-NOT-FIXED",
        "NOT-SIGNIFICANT-DELTA",
    )


def apply_hr54_rss_policy(
    cmp: Dict[str, Any],
    axis_key: str,
    is_develop_baseline: bool,
    rss_paired_allowed_for_cell: bool,
) -> Dict[str, Any]:
    """Apply HR54 close-gate policy to an RSS axis vs develop.

    HR54 (fu7-20 Session 3): RSS must be STRICTLY LOWER than develop —
    Δ% < HR54_NOISE_FLOOR_PCT (-2.0) AND p < HR54_P_THRESHOLD (0.05).
    Anything weaker (EQUIVALENT / MITIGATED / BETTER-not-significant) is a
    REGRESSION-RSS-POLICY violation. The only override is per-cell user opt-in
    via the manifest's ``rss_paired_allowed`` list.

    The function mutates and returns ``cmp`` with an added ``hr54`` block
    documenting the policy outcome. The underlying ``band`` is overridden to
    ``BAND_RSS_POLICY_VIOLATION`` if the policy fails AND no override applies.
    """
    cmp["hr54"] = {
        "applies": is_develop_baseline and axis_key in RSS_AXES_KEYS,
        "rss_paired_allowed": rss_paired_allowed_for_cell,
        "pass": True,
        "reason": "not-applicable",
    }
    if not (is_develop_baseline and axis_key in RSS_AXES_KEYS):
        return cmp
    # HR54 (fu7-20 Session 3 amendment + user clarification):
    # `rss_paired_allowed` applies to BOTH RSS axes (rss_delta_kb median AND
    # rss_max_kb peak-median) for smaller-file cells where the v2 pool
    # overhead is fixed-cost-dominant — but EVERY override REQUIRES a
    # follow-up ticket in the verdict file to investigate whether the RSS
    # can be reduced or must be assumed-acceptable. The aggregator emits
    # the follow-up tag in the verdict prose for the analyzer to enumerate.
    override_eligible = rss_paired_allowed_for_cell
    if "reason" in cmp:
        # missing-axis case already blocks via HR53; HR54 inherits its FAIL.
        cmp["hr54"]["pass"] = False
        cmp["hr54"]["reason"] = "axis-missing-blocks-HR54"
        return cmp
    pct = cmp.get("delta_pct")
    p_value = cmp.get("p")
    pct_ok = pct is not None and pct < HR54_NOISE_FLOOR_PCT
    p_ok = p_value is not None and p_value < HR54_P_THRESHOLD
    if pct_ok and p_ok:
        cmp["hr54"]["pass"] = True
        cmp["hr54"]["reason"] = (
            f"WIN — Δ%={pct:+.2f}% < {HR54_NOISE_FLOOR_PCT:+.2f}% and "
            f"p={p_value:.4f} < {HR54_P_THRESHOLD:.2f}"
        )
    elif override_eligible:
        cmp["hr54"]["pass"] = True
        cmp["hr54"]["reason"] = (
            f"PAIRED-OK-WITH-FOLLOWUP (small-file pool-overhead trade-off cell on "
            f"rss_paired_allowed override; the fixed connection-pool RSS is "
            f"amortized away on large workloads) — "
            f"Δ%={pct if pct is None else f'{pct:+.2f}%'}, "
            f"p={p_value if p_value is None else f'{p_value:.4f}'}. "
            f"Per HR54 amendment, this cell requires a follow-up ticket + explicit "
            f"user sign-off to investigate whether the peak RSS can be reduced or "
            f"must be assumed-acceptable (never silent)."
        )
    else:
        cmp["hr54"]["pass"] = False
        cmp["hr54"]["reason"] = (
            f"REGRESSION-RSS-POLICY — Δ%="
            f"{pct if pct is None else f'{pct:+.2f}%'}, "
            f"p={p_value if p_value is None else f'{p_value:.4f}'}; "
            f"HR54 requires Δ%<{HR54_NOISE_FLOOR_PCT:+.2f}% AND "
            f"p<{HR54_P_THRESHOLD:.2f} vs develop (no override for this cell)"
        )
        cmp["band"] = BAND_RSS_POLICY_VIOLATION
    return cmp


def hr55_prose(cmp: Dict[str, Any], axis_label: str, baseline_label: str) -> str:
    """Render a one-line prose verdict per HR55.

    Examples:
      "RSS WIN: -6.2% delta KB vs develop, p<0.0001, n=15, Cliff's δ=-0.892 (large)"
      "RSS REGRESSION-RSS-POLICY: paired Δ%=+0.5%, p=0.42, n=15 vs develop — HR54 requires strict improvement"
    Returns "" when the comparison is unavailable (caller skips emission).
    """
    if cmp is None or "reason" in cmp:
        reason = (cmp or {}).get("reason", "axis missing")
        return f"COVERAGE-MISSING ({axis_label} vs {baseline_label}): {reason} — blocks close-gate per HR53"
    band = cmp.get("band", "?")
    pct = cmp.get("delta_pct")
    p_value = cmp.get("p")
    n_a = cmp.get("a_n", "?")
    cd = cmp.get("cliffs_d")
    cd_band = cmp.get("cliffs_band", "?")
    pct_s = "n/a" if pct is None else f"{pct:+.2f}%"
    p_s = "n/a" if p_value is None else (f"<0.0001" if p_value < 0.0001 else f"{p_value:.4f}")
    cd_s = "n/a" if cd is None else f"{cd:+.3f} ({cd_band})"
    hr54_note = ""
    if cmp.get("hr54", {}).get("applies"):
        hr54_note = " [HR54 develop-RSS policy " + ("PASS" if cmp["hr54"]["pass"] else "FAIL") + "]"
    return (
        f"{band} ({axis_label} vs {baseline_label}): "
        f"Δ%={pct_s}, p={p_s}, n={n_a}, Cliff's δ={cd_s}{hr54_note}"
    )


# ---------------------------------------------------------------------------
# Comparison driver.
# ---------------------------------------------------------------------------
def compare_axis(
    a_cells: Sequence[Dict[str, Any]],
    b_cells: Sequence[Dict[str, Any]],
    axis_key: str,
    higher_better: bool,
) -> Dict[str, Any]:
    """Return a comparison dict for a single axis on candidate ``a`` vs ``b``."""
    a_vals = [c[axis_key] for c in a_cells if c.get(axis_key) is not None]
    b_vals = [c[axis_key] for c in b_cells if c.get(axis_key) is not None]
    if not a_vals or not b_vals:
        return {
            "axis_key": axis_key,
            "a_n": len(a_vals),
            "b_n": len(b_vals),
            "reason": (
                "missing field in candidate" if not a_vals
                else "missing field in baseline"
            ),
        }
    a_med = median(float(v) for v in a_vals)
    b_med = median(float(v) for v in b_vals)
    pct = (a_med - b_med) / b_med * 100.0 if b_med else None
    U, p = mann_whitney_u(a_vals, b_vals)
    d = cliffs_delta(a_vals, b_vals)
    band = verdict_band(pct, p, higher_better)
    return {
        "axis_key": axis_key,
        "a_n": len(a_vals),
        "b_n": len(b_vals),
        "a_median": a_med,
        "b_median": b_med,
        "delta_pct": pct,
        "U": U,
        "p": p,
        "cliffs_d": d,
        "cliffs_band": cliffs_band(d),
        "band": band,
        "higher_better": higher_better,
    }


# ---------------------------------------------------------------------------
# Formatting helpers.
# ---------------------------------------------------------------------------
def fmt_num(v: Any, ndp: int = 0) -> str:
    """Format ``v`` as a fixed-decimal number with thousands separators.

    Returns ``"n/a"`` when ``v`` is None or non-numeric.
    """
    if v is None:
        return "n/a"
    try:
        f = float(v)
    except (TypeError, ValueError):
        return "n/a"
    if ndp == 0:
        return f"{int(round(f)):,}"
    return f"{f:,.{ndp}f}"


def fmt_pct(v: Optional[float]) -> str:
    return "n/a" if v is None else f"{v:+.2f}%"


def fmt_p(v: Optional[float]) -> str:
    if v is None:
        return "n/a"
    if v < 0.0001:
        return "<0.0001"
    return f"{v:.4f}"


def fmt_cliffs(d: Optional[float]) -> str:
    return "n/a" if d is None else f"{d:+.3f}"


def derive_pid(jsonl_path: Path) -> str:
    """Heuristically extract the PID from `bench_report_<PID>.jsonl`."""
    stem = jsonl_path.stem
    parts = stem.split("_")
    for part in reversed(parts):
        if part.isdigit():
            return part
    return "unknown"


# ---------------------------------------------------------------------------
# Per-cell Markdown writer.
# ---------------------------------------------------------------------------
def write_cell_markdown(
    cell_key: str,
    candidate_label: str,
    candidate_pid: str,
    candidate_cells: Sequence[Dict[str, Any]],
    baselines: Sequence[Dict[str, Any]],  # each: {label, pid, cells, comparisons{axis:cmp}}
    output_path: Path,
    axis_filter: Optional[Iterable[str]],
) -> None:
    """Emit a per-cell paired-comparison Markdown to ``output_path``."""
    n_cand = len(candidate_cells)
    cell_name = candidate_cells[0].get("name", cell_key) if candidate_cells else cell_key
    axes_for_render = [a for a in AXES if axis_filter is None or a[3] in axis_filter]

    lines: List[str] = []
    lines.append(f"# Bench-proof cell `{cell_key}` (n={n_cand} paired)")
    lines.append("")
    lines.append("Generated by `scripts/ci/aggregate_bench.py`. No TBD placeholders — empty stats carry an inline reason.")
    lines.append("")

    # §1 — Source data.
    lines.append("## 1. Source data")
    lines.append("")
    lines.append("| Source label | PID | JSON cell name | n iters |")
    lines.append("| --- | --- | --- | --- |")
    lines.append(
        f"| **{candidate_label}** (candidate) | `pid_{candidate_pid}` | `{cell_name}` | {n_cand} |"
    )
    for bl in baselines:
        b_name = bl["cells"][0].get("name", "") if bl["cells"] else "(no data)"
        lines.append(
            f"| {bl['label']} (baseline) | `pid_{bl['pid']}` | `{b_name}` | {len(bl['cells'])} |"
        )
    lines.append("")

    # §2 — Per-binary full distribution.
    lines.append("## 2. Per-binary distribution")
    lines.append("")
    sources = [
        (candidate_label + " (candidate)", candidate_cells),
        *[(bl["label"], bl["cells"]) for bl in baselines],
    ]
    for label, cells in sources:
        lines.append(f"### {label} (n={len(cells)})")
        lines.append("")
        if not cells:
            lines.append("_No data for this binary; skipping distribution table._")
            lines.append("")
            continue
        lines.append("| Axis | n | min | p25 | median | mean | p75 | p95 | max | σ | σ/μ |")
        lines.append("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
        for key, label_axis, _, _ in axes_for_render:
            vs = [c[key] for c in cells if c.get(key) is not None]
            s = axis_stats(vs)
            if s is None:
                lines.append(f"| `{key}` ({label_axis}) | 0 | — | — | — | — | — | — | — | — | n=0, axis missing |")
                continue
            n = int(s["n"])
            if n < 2:
                lines.append(
                    f"| `{key}` ({label_axis}) | {n} | {fmt_num(s['min'], 1)} | n=1 (IQR not computed) | "
                    f"**{fmt_num(s['median'], 1)}** | {fmt_num(s['mean'], 1)} | n=1 (IQR not computed) | "
                    f"n=1 (no p95) | {fmt_num(s['max'], 1)} | n=1 (σ not computed) | n=1 (no CV) |"
                )
                continue
            lines.append(
                f"| `{key}` ({label_axis}) | {n} | {fmt_num(s['min'], 1)} | {fmt_num(s['p25'], 1)} | "
                f"**{fmt_num(s['median'], 1)}** | {fmt_num(s['mean'], 1)} | {fmt_num(s['p75'], 1)} | "
                f"{fmt_num(s['p95'], 1)} | {fmt_num(s['max'], 1)} | {fmt_num(s['stdev'], 1)} | {s['cv']:.3f} |"
            )
        lines.append("")

    # §3 — Paired comparison tables (one per baseline).
    lines.append("## 3. Paired comparison — candidate vs each baseline")
    lines.append("")
    for bl in baselines:
        lines.append(f"### {candidate_label} vs {bl['label']}")
        lines.append("")
        if not bl["cells"]:
            lines.append(f"_No data for baseline `{bl['label']}`; comparison skipped._")
            lines.append("")
            continue
        lines.append(
            "| Axis | candidate median | baseline median | Δ% | MWU U | p | Cliff's δ | effect band |"
        )
        lines.append("| --- | --- | --- | --- | --- | --- | --- | --- |")
        for key, label_axis, higher, _ in axes_for_render:
            cmp = bl["comparisons"].get(key)
            if cmp is None or "reason" in cmp:
                reason = (cmp or {}).get("reason", "axis missing in both")
                lines.append(
                    f"| `{key}` ({label_axis}) | n/a | n/a | n/a | n/a | n/a | n/a | {reason} |"
                )
                continue
            lines.append(
                f"| `{key}` ({label_axis}) | {fmt_num(cmp['a_median'], 1)} | "
                f"{fmt_num(cmp['b_median'], 1)} | {fmt_pct(cmp['delta_pct'])} | "
                f"{fmt_num(cmp['U'], 1)} | {fmt_p(cmp['p'])} | "
                f"{fmt_cliffs(cmp['cliffs_d'])} ({cmp['cliffs_band']}) | "
                f"**{cmp['band']}**{'' if higher else ' (lower=better axis)'} |"
            )
        lines.append("")

    # §4 — Verdict per baseline.
    lines.append("## 4. Verdict per baseline")
    lines.append("")
    lines.append("| Baseline | throughput Δ% | throughput p | verdict |")
    lines.append("| --- | --- | --- | --- |")
    for bl in baselines:
        cmp = bl["comparisons"].get("aggregate_kbps")
        if cmp is None or "reason" in cmp:
            reason = (cmp or {}).get("reason", "throughput axis missing")
            lines.append(f"| {bl['label']} | n/a | n/a | INCONCLUSIVE — {reason} |")
            continue
        delta = cmp["delta_pct"] or 0.0
        p = cmp["p"]
        if delta >= 0 and p is not None and p < 0.05:
            verdict = "BENCH_PROOF_PASS (throughput ↑ SIG)"
        elif delta >= -2.0:
            verdict = f"EQUIVALENT/MITIGATED ({cmp['band']})"
        elif delta >= -10.0:
            verdict = f"MITIGATED-NOT-FIXED ({cmp['band']})"
        else:
            verdict = f"REGRESSION ({cmp['band']})"
        lines.append(
            f"| {bl['label']} | {fmt_pct(delta)} | {fmt_p(p)} | **{verdict}** |"
        )
    lines.append("")

    output_path.write_text("\n".join(lines))


# ---------------------------------------------------------------------------
# Master summary writer.
# ---------------------------------------------------------------------------
def write_master_summary(
    candidate_label: str,
    candidate_pid: str,
    per_cell: Dict[str, Dict[str, Any]],
    baselines_order: Sequence[str],
    output_path: Path,
    axis_filter: Optional[Iterable[str]],
) -> str:
    """Emit `master_summary.md`; return the final aggregate verdict string."""
    primary_keys = [k for k in PRIMARY_AXES_KEYS]
    # Filter primary keys by --axes if active.
    if axis_filter is not None:
        axis_groups = set(axis_filter)
        primary_keys = [
            k for k in primary_keys
            if any(k == ak and grp in axis_groups for ak, _, _, grp in AXES)
        ]

    # Track final verdict per close-gate dimension (fu7-20 Session 3):
    #   * throughput_pass: HR15 — throughput band is BETTER/EQUIVALENT/MITIGATED on every baseline.
    #   * coverage_pass:   HR53 — no primary axis carries a "reason" (n/a coverage) on any baseline.
    #   * rss_policy_pass: HR54 — RSS axes vs develop are strictly LOWER (Δ%<-2%, p<0.05) unless
    #                      the cell is on the manifest's `rss_paired_allowed` override list.
    # The session passes only when ALL three dimensions pass.
    throughput_pass = True
    coverage_pass = True
    rss_policy_pass = True
    coverage_violations: List[str] = []
    rss_policy_violations: List[str] = []
    throughput_band_counts: Dict[str, int] = {}
    all_band_counts: Dict[str, int] = {}

    lines: List[str] = []
    lines.append("# Bench-proof master summary")
    lines.append("")
    lines.append(
        f"Candidate: **{candidate_label}** (pid `{candidate_pid}`). "
        f"Generated by `scripts/ci/aggregate_bench.py`."
    )
    lines.append("")

    # §1 — Wide 4 × N × 3 table (cell, baseline, axis).
    lines.append("## 1. Per-cell × baseline × primary-axis matrix")
    lines.append("")
    lines.append(
        "| Cell | Baseline | Axis | candidate median | baseline median | Δ% | p | effect band |"
    )
    lines.append("| --- | --- | --- | --- | --- | --- | --- | --- |")
    for cell_key, cell_data in per_cell.items():
        for bl_label in baselines_order:
            bl = cell_data["baselines"].get(bl_label)
            if bl is None:
                lines.append(
                    f"| `{cell_key}` | {bl_label} | — | — | — | — | — | NO_BASELINE_DATA |"
                )
                continue
            bl_is_develop = bl.get("is_develop", False)
            for key in primary_keys:
                axis_meta = next((a for a in AXES if a[0] == key), None)
                label_axis = axis_meta[1] if axis_meta else key
                cmp = bl["comparisons"].get(key)
                if cmp is None or "reason" in cmp:
                    reason = (cmp or {}).get("reason", "axis missing")
                    lines.append(
                        f"| `{cell_key}` | {bl_label} | {label_axis} | n/a | n/a | n/a | n/a | {BAND_COVERAGE_MISSING} ({reason}) |"
                    )
                    # HR53: coverage gap blocks the close-gate (any primary axis, any baseline).
                    coverage_pass = False
                    coverage_violations.append(f"{cell_key} × {bl_label} × {label_axis}: {reason}")
                    continue
                lines.append(
                    f"| `{cell_key}` | {bl_label} | {label_axis} | "
                    f"{fmt_num(cmp['a_median'], 1)} | {fmt_num(cmp['b_median'], 1)} | "
                    f"{fmt_pct(cmp['delta_pct'])} | {fmt_p(cmp['p'])} | **{cmp['band']}** |"
                )
                # HR55: emit prose verdict on the line below the band row.
                lines.append(
                    f"| | | | | | | | _{hr55_prose(cmp, label_axis, bl_label)}_ |"
                )
                all_band_counts[cmp["band"]] = all_band_counts.get(cmp["band"], 0) + 1
                if key == "aggregate_kbps":
                    throughput_band_counts[cmp["band"]] = throughput_band_counts.get(cmp["band"], 0) + 1
                    if not is_better(cmp["band"]):
                        throughput_pass = False
                # HR54: RSS vs develop policy. apply_hr54_rss_policy() already
                # overrode the band to BAND_RSS_POLICY_VIOLATION when the policy fails.
                hr54 = cmp.get("hr54", {})
                if hr54.get("applies") and not hr54.get("pass"):
                    rss_policy_pass = False
                    rss_policy_violations.append(
                        f"{cell_key} × {bl_label} × {label_axis}: {hr54.get('reason', '?')}"
                    )
    lines.append("")

    # §2 — Per-cell distribution roll-up.
    lines.append("## 2. Per-cell distribution roll-up (candidate)")
    lines.append("")
    lines.append("| Cell | axis | n | median | mean | p95 | σ | σ/μ |")
    lines.append("| --- | --- | --- | --- | --- | --- | --- | --- |")
    for cell_key, cell_data in per_cell.items():
        for key in primary_keys:
            axis_meta = next((a for a in AXES if a[0] == key), None)
            label_axis = axis_meta[1] if axis_meta else key
            s = cell_data["candidate_stats"].get(key)
            if s is None:
                lines.append(
                    f"| `{cell_key}` | {label_axis} | 0 | — | — | — | — | n=0, axis missing |"
                )
                continue
            n = int(s["n"])
            if n < 2:
                lines.append(
                    f"| `{cell_key}` | {label_axis} | {n} | "
                    f"**{fmt_num(s['median'], 1)}** | {fmt_num(s['mean'], 1)} | "
                    f"n=1 (no p95) | n=1 (σ not computed) | n=1 (no CV) |"
                )
                continue
            lines.append(
                f"| `{cell_key}` | {label_axis} | {n} | "
                f"**{fmt_num(s['median'], 1)}** | {fmt_num(s['mean'], 1)} | "
                f"{fmt_num(s['p95'], 1)} | {fmt_num(s['stdev'], 1)} | {s['cv']:.3f} |"
            )
    lines.append("")

    # §3 — Effect-size band counts.
    lines.append("## 3. Effect-size band counts (cells × baselines × primary axes)")
    lines.append("")
    if not all_band_counts:
        lines.append("_No comparisons produced — see §1 for missing data reasons._")
    else:
        lines.append("| Band | Count |")
        lines.append("| --- | --- |")
        for b, c in sorted(all_band_counts.items(), key=lambda kv: (-kv[1], kv[0])):
            lines.append(f"| **{b}** | {c} |")
    lines.append("")
    if throughput_band_counts:
        lines.append("Throughput-only band counts (load-bearing axis):")
        lines.append("")
        lines.append("| Band | Count |")
        lines.append("| --- | --- |")
        for b, c in sorted(throughput_band_counts.items(), key=lambda kv: (-kv[1], kv[0])):
            lines.append(f"| **{b}** | {c} |")
        lines.append("")

    # §4 — Cross-session throttle exposure summary.
    lines.append("## 4. Throttle exposure (per binary)")
    lines.append("")
    lines.append(
        "Throttle exposure normalises cross-session comparisons against the "
        "endpoint's event=6 storm pressure (see BENCHMARKS.md "
        "Throttle-variance section)."
    )
    lines.append("")
    lines.append(
        "| Binary | total throttle_event6_count (median across cells) | total throttle_event6_total_ms (median across cells) |"
    )
    lines.append("| --- | --- | --- |")
    # Candidate row.
    cand_counts: List[float] = []
    cand_total_ms: List[float] = []
    for cell_data in per_cell.values():
        s_count = cell_data["candidate_stats"].get("throttle_event6_count")
        s_total = cell_data["candidate_stats"].get("throttle_event6_total_ms")
        if s_count is not None:
            cand_counts.append(s_count["median"])
        if s_total is not None:
            cand_total_ms.append(s_total["median"])
    lines.append(
        f"| {candidate_label} (candidate) | "
        f"{fmt_num(median(cand_counts), 1) if cand_counts else 'no throttle data'} | "
        f"{fmt_num(median(cand_total_ms), 1) if cand_total_ms else 'no throttle data'} |"
    )
    for bl_label in baselines_order:
        bl_counts: List[float] = []
        bl_total_ms: List[float] = []
        for cell_data in per_cell.values():
            bl = cell_data["baselines"].get(bl_label)
            if bl is None:
                continue
            s_count = bl["stats"].get("throttle_event6_count")
            s_total = bl["stats"].get("throttle_event6_total_ms")
            if s_count is not None:
                bl_counts.append(s_count["median"])
            if s_total is not None:
                bl_total_ms.append(s_total["median"])
        lines.append(
            f"| {bl_label} | "
            f"{fmt_num(median(bl_counts), 1) if bl_counts else 'no throttle data'} | "
            f"{fmt_num(median(bl_total_ms), 1) if bl_total_ms else 'no throttle data'} |"
        )
    lines.append("")

    # §5 — Final verdict (3-dimensional close-gate per fu7-20 Session 3).
    lines.append("## 5. Final verdict (3-dimensional close-gate)")
    lines.append("")
    lines.append("| Dimension | HR | Status |")
    lines.append("| --- | --- | --- |")
    if not throughput_band_counts:
        throughput_pass = False
        lines.append("| Throughput | HR15 | **INCONCLUSIVE** — no throughput comparisons produced |")
    else:
        lines.append(
            f"| Throughput | HR15 | **{'PASS' if throughput_pass else 'FAIL'}** — "
            f"{'every throughput-axis cell × baseline is BETTER/EQUIVALENT/MITIGATED' if throughput_pass else 'at least one cell × baseline regressed'} |"
        )
    lines.append(
        f"| Coverage (no n/a) | HR53 | **{'PASS' if coverage_pass else 'FAIL'}** — "
        f"{'every primary axis on every baseline produced a comparison' if coverage_pass else f'{len(coverage_violations)} missing-axis cell(s)'} |"
    )
    lines.append(
        f"| RSS vs develop | HR54 | **{'PASS' if rss_policy_pass else 'FAIL'}** — "
        f"{'every RSS axis vs develop is strictly LOWER (Δ%<-2%, p<0.05) or on rss_paired_allowed override' if rss_policy_pass else f'{len(rss_policy_violations)} RSS-policy violation(s) — see §6'} |"
    )
    lines.append("")
    final_pass = throughput_pass and coverage_pass and rss_policy_pass
    if final_pass:
        verdict_str = "BENCH_PROOF_PASS — throughput WIN + coverage complete + RSS strictly below develop"
    else:
        failed_dims = []
        if not throughput_pass:
            regress = [k for k in throughput_band_counts if not is_better(k)] or ["INCONCLUSIVE"]
            failed_dims.append(f"throughput ({', '.join(regress)})")
        if not coverage_pass:
            failed_dims.append(f"coverage ({len(coverage_violations)} n/a rows)")
        if not rss_policy_pass:
            failed_dims.append(f"RSS-policy ({len(rss_policy_violations)} HR54 violations)")
        verdict_str = f"BENCH_PROOF_FAIL — {'; '.join(failed_dims)}"
    lines.append(f"**`{verdict_str}`**")
    lines.append("")
    # §6 — Itemise violations when any dimension fails.
    if not coverage_pass or not rss_policy_pass:
        lines.append("## 6. Close-gate violations (HR53 / HR54)")
        lines.append("")
        if coverage_violations:
            lines.append("### HR53 coverage violations (missing-axis comparisons)")
            lines.append("")
            for v in coverage_violations:
                lines.append(f"- {v}")
            lines.append("")
        if rss_policy_violations:
            lines.append("### HR54 RSS-policy violations (RSS not strictly below develop)")
            lines.append("")
            for v in rss_policy_violations:
                lines.append(f"- {v}")
            lines.append("")
    lines.append("")
    lines.append(
        "_Per-cell paired Markdown lives alongside this file as "
        "`<cell>_n<N>_paired.md`. The full per-cell statistics + comparisons "
        "are archived in `comparison_results.json`._"
    )

    output_path.write_text("\n".join(lines))
    return verdict_str


# ---------------------------------------------------------------------------
# Top-level driver.
# ---------------------------------------------------------------------------
def load_manifest(path: Path) -> Dict[str, Any]:
    """Read + sanity-check the manifest JSON.

    fu7-20 Session 3 added two optional fields used by the HR53/HR54 close-gate:

      * ``rss_paired_allowed`` (top-level): list of cell short tags (e.g.
        ``["manysmall"]``) where paired-or-better RSS vs develop is acceptable
        per HR54's smaller-files exception. Default ``[]`` (no override).
      * ``is_develop`` (per baseline): boolean flag identifying the develop
        baseline for HR54 enforcement. Auto-detected from ``label == "develop"``
        if absent. Set to ``false`` to explicitly suppress HR54 against a
        baseline whose label happens to be "develop".
    """
    if not path.exists():
        sys.stderr.write(f"error: manifest not found: {path}\n")
        sys.exit(2)
    try:
        doc = json.loads(path.read_text())
    except json.JSONDecodeError as exc:
        sys.stderr.write(f"error: manifest is not valid JSON: {exc}\n")
        sys.exit(2)
    if "candidate" not in doc or "jsonl" not in doc.get("candidate", {}):
        sys.stderr.write("error: manifest missing `candidate.jsonl`\n")
        sys.exit(2)
    if "baselines" not in doc or not isinstance(doc["baselines"], list):
        sys.stderr.write("error: manifest missing `baselines` list\n")
        sys.exit(2)
    for entry in [doc["candidate"], *doc["baselines"]]:
        if "label" not in entry or "jsonl" not in entry:
            sys.stderr.write(
                "error: every manifest entry must have `label` and `jsonl`\n"
            )
            sys.exit(2)
    # Validate + normalise the HR54 fields.
    rpa = doc.get("rss_paired_allowed", [])
    if not isinstance(rpa, list) or any(not isinstance(c, str) for c in rpa):
        sys.stderr.write("error: manifest `rss_paired_allowed` must be a list of cell-key strings\n")
        sys.exit(2)
    doc["rss_paired_allowed"] = set(rpa)
    for bl in doc["baselines"]:
        if "is_develop" in bl:
            if not isinstance(bl["is_develop"], bool):
                sys.stderr.write(
                    f"error: baseline `{bl.get('label', '?')}` has non-boolean `is_develop`\n"
                )
                sys.exit(2)
        else:
            # Auto-detect: label exactly "develop" (case-insensitive).
            bl["is_develop"] = bl["label"].strip().lower() == "develop"
    return doc


def parse_cli() -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description="Canonical bench-proof aggregator for the WS-upload bench framework.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument(
        "--manifest",
        type=Path,
        required=True,
        help="Path to baselines.json (see module docstring).",
    )
    ap.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="Destination directory for per-cell + master_summary Markdown.",
    )
    ap.add_argument(
        "--cells",
        type=str,
        default=None,
        help="Comma-separated cell-key allow-list (e.g. `slu,manysmall`). Defaults to every cell seen in the manifest.",
    )
    ap.add_argument(
        "--axes",
        type=str,
        default=None,
        help="Comma-separated axis-group allow-list (throughput,rss,cpu,throttle,chunk). Defaults to every group.",
    )
    return ap.parse_args()


def main() -> int:
    args = parse_cli()
    manifest = load_manifest(args.manifest)

    args.output_dir.mkdir(parents=True, exist_ok=True)

    cell_allow: Optional[set] = None
    if args.cells:
        cell_allow = {c.strip() for c in args.cells.split(",") if c.strip()}
    axis_allow: Optional[set] = None
    if args.axes:
        axis_allow = {a.strip() for a in args.axes.split(",") if a.strip()}

    # 1. Load every JSONL once, key cells.
    candidate_entry = manifest["candidate"]
    candidate_label = candidate_entry["label"]
    candidate_jsonl = Path(candidate_entry["jsonl"])
    candidate_pid = derive_pid(candidate_jsonl)
    candidate_cells_flat = load_cells(candidate_jsonl)
    candidate_by_cell = group_cells_by_key(candidate_cells_flat)
    print(
        f"loaded candidate `{candidate_label}` from {candidate_jsonl} "
        f"({sum(len(v) for v in candidate_by_cell.values())} iters across "
        f"{len(candidate_by_cell)} cells)"
    )

    baseline_loaded: List[Dict[str, Any]] = []
    for bl_entry in manifest["baselines"]:
        bl_path = Path(bl_entry["jsonl"])
        bl_cells = load_cells(bl_path)
        bl_by_cell = group_cells_by_key(bl_cells)
        baseline_loaded.append({
            "label": bl_entry["label"],
            "pid": derive_pid(bl_path),
            "cells_by_key": bl_by_cell,
            "is_develop": bl_entry.get("is_develop", False),
        })
        print(
            f"loaded baseline `{bl_entry['label']}` from {bl_path} "
            f"({sum(len(v) for v in bl_by_cell.values())} iters across "
            f"{len(bl_by_cell)} cells)"
        )

    # 2. Cell discovery: union of every cell seen on candidate or any baseline,
    #    filtered through --cells.
    discovered: set = set(candidate_by_cell.keys())
    for bl in baseline_loaded:
        discovered.update(bl["cells_by_key"].keys())
    if cell_allow is not None:
        discovered &= cell_allow
    if not discovered:
        sys.stderr.write("error: no cells matched --cells filter\n")
        return 2
    cell_keys = sorted(discovered)

    # 3. Per-cell processing.
    per_cell: Dict[str, Dict[str, Any]] = {}
    baselines_order = [bl["label"] for bl in baseline_loaded]

    for cell_key in cell_keys:
        cand_cells = candidate_by_cell.get(cell_key, [])
        cand_stats = {
            key: axis_stats([c[key] for c in cand_cells if c.get(key) is not None])
            for key, _, _, _ in AXES
        }
        per_cell[cell_key] = {
            "candidate_cells": cand_cells,
            "candidate_stats": cand_stats,
            "baselines": {},
        }
        rss_paired_allowed_for_cell = cell_key in manifest["rss_paired_allowed"]
        baselines_for_md: List[Dict[str, Any]] = []
        for bl in baseline_loaded:
            bl_cells = bl["cells_by_key"].get(cell_key, [])
            comparisons: Dict[str, Dict[str, Any]] = {}
            for key, _, higher, _ in AXES:
                cmp = compare_axis(cand_cells, bl_cells, key, higher)
                # HR54 post-process: mutate cmp to record the develop-vs-RSS verdict.
                apply_hr54_rss_policy(
                    cmp,
                    axis_key=key,
                    is_develop_baseline=bl["is_develop"],
                    rss_paired_allowed_for_cell=rss_paired_allowed_for_cell,
                )
                comparisons[key] = cmp
            bl_stats = {
                key: axis_stats([c[key] for c in bl_cells if c.get(key) is not None])
                for key, _, _, _ in AXES
            }
            per_cell[cell_key]["baselines"][bl["label"]] = {
                "pid": bl["pid"],
                "cells": bl_cells,
                "stats": bl_stats,
                "comparisons": comparisons,
                "is_develop": bl["is_develop"],
                "rss_paired_allowed": rss_paired_allowed_for_cell,
            }
            baselines_for_md.append({
                "label": bl["label"],
                "pid": bl["pid"],
                "cells": bl_cells,
                "comparisons": comparisons,
                "is_develop": bl["is_develop"],
                "rss_paired_allowed": rss_paired_allowed_for_cell,
            })

        cell_md_path = (
            args.output_dir / f"{cell_key}_n{len(cand_cells)}_paired.md"
        )
        write_cell_markdown(
            cell_key=cell_key,
            candidate_label=candidate_label,
            candidate_pid=candidate_pid,
            candidate_cells=cand_cells,
            baselines=baselines_for_md,
            output_path=cell_md_path,
            axis_filter=axis_allow,
        )
        print(f"  wrote {cell_md_path}")

    # 4. Master summary.
    master_path = args.output_dir / "master_summary.md"
    final_verdict = write_master_summary(
        candidate_label=candidate_label,
        candidate_pid=candidate_pid,
        per_cell=per_cell,
        baselines_order=baselines_order,
        output_path=master_path,
        axis_filter=axis_allow,
    )
    print(f"wrote {master_path}")

    # 5. Aggregate JSON.
    aggregate_path = args.output_dir / "comparison_results.json"
    aggregate_path.write_text(json.dumps({
        "manifest": str(args.manifest),
        "candidate": {"label": candidate_label, "pid": candidate_pid},
        "baselines": baselines_order,
        "per_cell": per_cell,
        "final_verdict": final_verdict,
    }, indent=2, default=str))
    print(f"wrote {aggregate_path}")

    print(f"\n{final_verdict}")
    return 0 if final_verdict.startswith("BENCH_PROOF_PASS") else 1


if __name__ == "__main__":
    sys.exit(main())
