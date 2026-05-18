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

_TODO — added in fu7-17 Goal 7; will populate after the methodology
tree is committed:_

- `docs/methodology/README.md`
- `docs/methodology/followup_workflow.md`
- `docs/methodology/regression_sweep_cadence.md`
- `docs/methodology/benchmark_discipline.md`
- `docs/methodology/tsan_discipline.md`
- `docs/methodology/hard_rules_catalog.md`
- `docs/methodology/glossary.md`
- `docs/methodology/portable_template/` (for adopting in other repos)

## CI scripts (under `scripts/ci/`)

- [example_triggers.md](../../scripts/ci/example_triggers.md) —
  copy-paste GitLab comments for common scenarios.
- `compare_bench.py` — diff two `bench_report_*.json` files.
- `compare_tsan.sh` — diff two TSAN log archives.
- `find_bench_reports.sh` — find local `pid_*/bench_report_*.json`.

## Project conventions

- [CLAUDE.md](../../CLAUDE.md) — codebase guidance (build, test, style).
- [README.md](../../README.md) — top-level project README.
