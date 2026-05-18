# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when
working with code in this repository.

## Project Overview

<!-- TODO: 1-2 paragraphs on what the project is, the languages /
runtimes / OSes supported, the public API surface vs internal
implementation, and any notable architectural axes (encryption,
crypto, distributed system, etc.). -->

## Build Commands

<!-- TODO: list the canonical build commands. Mention out-of-source /
in-source policy, dependency manager (vcpkg / pip / npm / cargo / etc.),
key presets or feature flags. Example: -->

```bash
# Configure (default preset)
<configure command>

# Build
<build command>

# Build a specific target
<build command with target>
```

Key presets / build flavours: `<list>`. Options reference: `<file>`.

## Testing

<!-- TODO: testing framework, where unit vs integration tests live,
how to filter, key env vars required. Example: -->

```bash
# Unit tests (fast, no I/O)
<unit test command>

# Filtered unit tests
<filtered command>

# Integration tests (require env setup)
<integration test command>
```

Always filter integration tests to the cells relevant to your changes.

### Benchmarks and stress-test catalog

See **`<path>/BENCHMARKS.md`** for the curated list of benchmark and
stress cells, grouped into core regression targets, throughput
benchmarks, and broader sweeps, with wall-clock budgets and
known-bad / infra quirks.

### Integration test procedure

**Mandatory testing procedure after writing tests or fixes:**

1. **Run each new test individually.** Analyze the full log even if
   the test passes.
2. **Run the full related test suite(s).** Verify no regressions.
3. **Stability loop.** Run the full suite at least 2-3 times with
   `--gtest_repeat=2` (or framework equivalent) to catch flakiness.
4. **Log analysis is mandatory at every step**, not just on failure.

## Code Style

<!-- TODO: link to .clang-format / .editorconfig / linter config.
Specify language version (C++17/20, Python 3.x, TypeScript 5.x).
Document formatter version pins. -->

## Architecture

<!-- TODO: a brief tour of the main modules / layers. Highlight any
"don't modify without diagnostic logging first" hot paths. -->

## Development Methodology

### Spec-Driven Development (SDD) + Test-Driven Development (TDD)

Follow this order for any bug fix or feature:

1. **Understand the spec / reproduce the problem.** Read the relevant
   code, analyze logs, confirm the scenario. Do not guess.
2. **Write the test first (TDD).** The test must encode the expected
   behavior and **fail** before the fix is applied. If it passes
   immediately, the test is not testing the right thing — revisit.
3. **Implement the fix / feature.** Only the minimal change needed.
4. **Run the test until it passes.** If it still fails, fix the code,
   not the test.
5. **Verify stability.** Run the test (and related tests) **at least
   2-3 consecutive times** to rule out timing-dependent flakiness.

### Test Discipline

These rules are non-negotiable:

- **Never relax assertions just to make a test pass.**
- **A test that fails once is a signal, not noise.**
- **Review test logs even when tests pass.**
- **Use `--gtest_repeat=N` (or framework equivalent) to stress-test
  flaky scenarios.**
- **Add diagnostic logging when investigating.**
- **Tests must be deterministic** (or document their timing dependency).

### Code Fix Discipline

- **Don't fix tests to match broken behavior.**
- **Don't introduce workarounds.** Go deeper if the straightforward
  fix doesn't work.
- **Don't modify <core hot-path functions>** without tracing the exact
  state transitions through diagnostic logging first.
- **When a fix has unintended side effects on other tests**, revert
  it, re-analyze, and find a more targeted approach.

## Git Workflow

- Main branch: `<main / develop / trunk>`
- Branch naming: `<convention>`
- Commit prefixes: `fix:`, `test:`, `refactor:`, `feat:`, etc.
- Include issue key (e.g., `PROJ-1234`) when applicable.
- Pre-commit hooks: <list, if any>. Never commit credentials.
- No force pushes to `<main branch>`.

## Methodology + workflow reference

The operational methodology (followup discipline, auto-memory protocol,
regression-sweep cadence, hard rules catalog) is documented under
[docs/methodology/](docs/methodology/README.md). Future agent sessions
and contributors should consult it.

The portable template subtree
[docs/methodology/portable_template/](docs/methodology/portable_template/)
is the version reusable in other repos.
