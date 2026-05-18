# fu7-N session template — `FollowupRequest.md` skeleton

Copy this file as the starting `FollowupRequest.md` for the next
followup session and fill in the `<!-- TODO -->` placeholders.

```
cp docs/methodology/fu7_N_session_template.md \
   ~/investigationTests/.../followup7-<N>/FollowupRequest.md
```

Edit until each TODO is resolved, then enter plan mode.

---

# SDK-<ticket> followup<n> request — <one-line summary>

## Purpose

Input request for the `followup<n>` plan-mode session. Continuation
of **followup<n-1>** (closed <date> `FOLLOWUP<n-1>_<label>`, HEAD
`<hash>` pre-user-resign).

<!-- TODO: 1-2 sentences on what this session must achieve. -->

---

## followup<n-1> outcome — concise summary

**`FOLLOWUP<n-1>_<label>`** (verdict at `…/followup<n-1>/FollowupVerdict.md`).

### Commits landed in followup<n-1>

| Hash | Subject |
|---|---|
| `<hash>` | <subject> |

### followup<n-1> highlights

<!-- TODO: bullet list of key outcomes — wins, regressions, deferrals. -->

### followup<n-1> deferred items (carry to followup<n>)

<!-- TODO: items the previous session explicitly deferred. -->

---

## Open issue #0 — Audit followup<n-1>'s work (TOP PRIORITY)

Before any new work, audit the previous session's deliverables and
identify overlooked observations, minimised claims, or shortcuts taken
under wall pressure.

### 0.a Audit followup<n-1> bench numbers

<!-- TODO: which bench cells to re-verify, which trajectory to extend. -->

### 0.b Negative observations to re-audit

<!-- TODO: any "passed but had high variance" / "envelope drifted" notes. -->

### 0.c Was anything minimised in the verdict?

<!-- TODO: scan for soft "PASS" claims without quantitative backing. -->

### Goal 0 deliverables

`Goal0_audit/{fu<n-1>_audit.md, fu_N_bench_trajectory.md,
develop_comparison_re-check.md, negative_observations.md}`

### Agent: `Explore` for memory + log scans; main thread for write-up.

---

## Goal 1 — <name>

### 1.a <sub-task>

<!-- TODO: what to do, why, expected outcome. -->

### 1.b <sub-task>

<!-- TODO. -->

### 1.c Acceptance

- All builds green: dev-unix-wsupload + dev-unix-strict + dev-unix-hooks-off.
- Per-commit bench gate per HR14.
- HR24 unchanged.

### Agent: `investigator` for decisions; `implementer` for changes.

### Wall budget: <range> h

---

<!-- TODO: add Goal 2, 3, ... as needed. -->

---

## Hard rules carried

HR1, HR4, HR8, HR9, HR13, HR14, HR15, HR16, HR21, HR22, HR23, HR24,
HR27, HR30, HR31, HR33, HR34, HR36, HR38, HR39, HR40, HR41, HR42,
HR43, HR44, HR45, HR46, HR47, HR48, HR49.

See [docs/methodology/hard_rules_catalog.md](hard_rules_catalog.md).

---

## Stop conditions

- <!-- TODO: per-Goal stop conditions. -->
- HR24 >700 → halt and audit.
- Goal N TSAN deep-dive surfaces a NEW WS-upload surface race that can't be
  fixed within session wall → halt + escalate per HR21.
- Final sweep MN HUNG (>90 min single iter) → escalate.
- Any cell <80% PASS rate in final sweep → escalate.

---

## Wall budget

| Goal | Est wall |
|---|---|
| 0 (audit) | 2-3 h |
| 1 (<name>) | <range> h |
| ... | ... |
| Per-commit bench gates + inter-Goal sweeps | 4-6 h (amortised) |
| Verdict + memory + push prep | 1 h |
| **Total** | **<range> h** |

**IRREDUCIBLE**: Goal 0 audit + Goal N TSAN fix + final sweep with
MN n=5 ONE batch + develop comparison incl. RSS+CPU.

**Cut order** if overrunning: Goal X catalog-only, defer fixes.

---

## Final verdict format

```
Goal:                 [0 / 1 / 2 / ...]
Status:               [DONE / PARTIAL / DEFERRED (with HR-compatible justification)]
Binaries tested:      [list with md5; include macOS + Windows compile checks]
n samples per cell:   [≥3 / ≥15 timing / ≥5 MN]
Multi-session bench delta:
  develop vs fu<n> throughput Δ + RSS Δ + CPU Δ: [...]
  fu<n-1> vs fu<n> throughput Δ + RSS Δ + CPU Δ: [...]
HR24 final value:     [target ≤700 maintained]
TSAN races NEW vs develop: [delta + FIXED/ACCEPTABLE]
bench_report_*.json artifacts produced: [yes/no + path examples]
Skipped tests at close: [count + classification]
MN sweep result:      [n + PASS rate + wall envelope (one batch, no stitching)]
B9 sweep result:      [n=15 regular + n=15 taskset + PASS rate]
Force-push executed:  [yes/no, post-push hash, MR# if updated]
Conclusion / next action:
```

### Overall verdict labels

- `FOLLOWUP<n>_VALIDATED_TECHQA_READY` — all Goals DONE, sweep ≥80%
  PASS, no NEW TSAN races.
- `FOLLOWUP<n>_PARTIAL_DEFER` — explicit defer with HR21-compatible
  justification.
- `FOLLOWUP<n>_REGRESSION_DETECTED` — a fix introduced a regression;
  revert + escalate.
- `FOLLOWUP<n>_BLOCKED_ON_USER` — decision requires user input.

---

## Mandatory reading order

1. `…/followup<n-1>/FollowupRequest.md`
2. `…/followup<n-1>/FollowupVerdict.md`
3. `…/followup<n-1>/Goal0_audit/{…}`
4. `~/.claude/projects/.../memory/project_<ticket>_followup<n-1>_results.md`
5. `~/.claude/projects/.../memory/MEMORY.md` (index)
6. `tests/integration/BENCHMARKS.md`
7. `docs/methodology/regression_sweep_cadence.md`
8. `docs/methodology/hard_rules_catalog.md`

---

## Final note

Stop after writing the plan file (via plan mode) and ask for approval
before any SDK source change, branch switch, force-push, or BENCHMARKS
sweep kickoff. Use `ExitPlanMode` to request approval.

**USERAGENT reminder**: ALWAYS pass `--USERAGENT:JenkinsCanSpam-SDK`
on any `test_integration` invocation (see
[docs/methodology/benchmark_discipline.md](benchmark_discipline.md)).
