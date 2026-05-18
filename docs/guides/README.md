# MEGA SDK — Guides index

Single entry point for all team-facing guides. Bookmark this page.

## CI / Jenkins

- [docs/ci/CI_TESTING_GUIDE.md](../ci/CI_TESTING_GUIDE.md) — How to
  trigger Jenkins builds via GitLab MR comments; `--tsan`, `--bench`,
  `--gtest_filter`; preset glossary; cross-OS scope; artifact diff
  recipes.

## Testing & Benchmarks

- [tests/integration/BENCHMARKS.md](../../tests/integration/BENCHMARKS.md) —
  Benchmark cell catalog, throughput-storm caveats, JSON schema,
  Jenkins-side artifact contracts.
- [AGENTS.md](../../AGENTS.md) — Test-running setup, `environment2.txt`,
  log locations.

## Methodology (workflow + discipline)

Single starting point: [docs/methodology/README.md](../methodology/README.md).

- [docs/methodology/followup_workflow.md](../methodology/followup_workflow.md)
  — The `FollowupRequest -> plan -> execute -> Verdict -> memory -> push`
  loop and per-Goal pattern.
- [docs/methodology/memory_discipline.md](../methodology/memory_discipline.md)
  — Auto-memory protocol: types, naming, when to save/update/delete.
- [docs/methodology/regression_sweep_cadence.md](../methodology/regression_sweep_cadence.md)
  — HR14/33/34/40/43/46/47 — per-commit / inter-Goal / final sweep gates.
- [docs/methodology/benchmark_discipline.md](../methodology/benchmark_discipline.md)
  — Bench binary build, JSON artifact, throttle-storm caveats,
  USERAGENT, comparison recipes.
- [docs/methodology/tsan_discipline.md](../methodology/tsan_discipline.md)
  — `dev-unix-tsan` (no Windows), TSAN_OPTIONS, v2-surface filter,
  NEW-vs-develop classification.
- [docs/methodology/hard_rules_catalog.md](../methodology/hard_rules_catalog.md)
  — Single canonical table HR1..HR49.
- [docs/methodology/fu7_N_session_template.md](../methodology/fu7_N_session_template.md)
  — Copy-paste FollowupRequest.md skeleton for the next session.
- [docs/methodology/glossary.md](../methodology/glossary.md) —
  SDK-5360 acronyms (SLU, T1, IP, MN, B9, fu7-N, HR<N>, etc.).
- [docs/methodology/portable_template/](../methodology/portable_template/README.md)
  — Adoption scaffold for OTHER repos.

## CI scripts (under `scripts/ci/`)

- [example_triggers.md](../../scripts/ci/example_triggers.md) —
  copy-paste GitLab comments for common scenarios.
- `compare_bench.py` — diff two `bench_report_*.json` files.
- `compare_tsan.sh` — diff two TSAN log archives.
- `find_bench_reports.sh` — find local `pid_*/bench_report_*.json`.

## Project conventions

- [CLAUDE.md](../../CLAUDE.md) — codebase guidance (build, test, style).
- [README.md](../../README.md) — top-level project README.
