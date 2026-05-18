# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MEGA SDK — a cross-platform C++ library providing encrypted cloud storage client functionality with end-to-end encryption (User Controlled Encryption). Supports Windows, Linux, macOS, iOS, and Android. The public API is in `include/megaapi.h`; low-level internals are under `include/mega/`.

## Build Commands

Out-of-source builds only (in-source builds are rejected). VCPKG is required for dependency management; defaults to `../vcpkg`.

```bash
# Configure (dev preset)
cmake --preset dev-unix
# for wsupload branch:
cmake --preset dev-unix-wsupload

# Build
cmake --build ../build-sdk-dev-unix-wsupload -j16

# Build a specific target
cmake --build ../build-sdk-dev-unix-wsupload -j16 --target=SDKlib
```

Key presets: `dev-unix-wsupload`, `dev-windows`, `mega-ios`, `mega-android`, `megasync-unix`, `megasync-windows`, `megacmd-unix`, `megacmd-windows`. Build options are in `cmake/modules/sdklib_options.cmake`.

## Testing

Framework: GoogleTest/GoogleMock. One test suite per file; filename matches suite name (e.g., `Crypto_test.cpp` contains `TEST(Crypto, ...)`). Testing framework code lives in the `mt` namespace.

```bash
# Run all unit tests (fast, no I/O)
../build-sdk-dev-unix-wsupload/tests/unit/test_unit

# Run filtered unit tests
../build-sdk-dev-unix-wsupload/tests/unit/test_unit --gtest_filter=Crypto*

# Run integration tests (requires env vars from environment2.txt)
# First: source env vars from ../build-sdk-dev-unix-wsupload/tests/integration/environment2.txt
# Run examples: ../build-sdk-dev-unix-wsupload/tests/integration/run_test.sh
../build-sdk-dev-unix-wsupload/tests/integration/test_integration \
  --CI --COUT --USERAGENT:JenkinsCanSpam-SDK \
  --gtest_filter="SdkTest.SdkTestUploads"
```

Always use `--gtest_filter` for integration tests — run only tests specific/related to your changes.

### Benchmarks and stress-test catalog

See **`tests/integration/BENCHMARKS.md`** for the curated list of tests used to validate WS upload, sync, and transfer-scheduling changes — grouped into core regression targets, throughput benchmarks, and broader sweeps, with wall-clock budgets and known-bad / infra quirks. Extend that file as new scenarios appear so future sessions don't need to re-derive them from session reports.

### Integration Test Procedure

**Always follow `AGENTS.md`** for detailed test-running instructions, environment setup, and log locations.

**Test logs:** Integration test logs are written to `/home/vmga/mega_tests/pid_<PID>/test_integration.log`, where `<PID>` is the process ID. To find the log for the latest run, sort the `pid_*` directories by modification time:
```bash
ls -td /home/vmga/mega_tests/pid_* | head -1
```

**Mandatory testing procedure after writing tests or fixes:**

1. **Run each new test individually.** Analyze the full log (`test_integration.log`) even if the test passes — look for `NODE_COMP_DIFFERS_MTIME`, `setattr`, unexpected `local file addition`, spurious transfers, or stalls.
2. **Run the full related test suite(s).** Verify no regressions in existing tests.
3. **Stability loop.** Run the full suite at least 2–3 times with `--gtest_repeat=2` (or more) to catch timing-dependent flakiness.
4. **Log analysis is mandatory at every step**, not just when tests fail. A passing test with wrong intermediate behavior is hiding a bug.

## Code Style

- Follow `.clang-format` (4-space indent, no tabs, ~100 column limit, clang-format v18). **Do NOT run `clang-format -i`** — the system has v19 installed but the project targets v18. Only apply formatting to new code, not existing lines.
- C++17 (moving to C++20). Use modern C++ practices.
- Test files: `*_test.cpp` naming, one suite per file.

## Architecture

**Core layers:**
- **Public API** (`include/megaapi.h`, `src/megaapi_impl.cpp`) — intermediate layer with listener-based async patterns. This is the only header consumers include.
- **Low-level SDK** (`include/mega/*.h`, `src/`) — MegaClient, filesystem, crypto, networking, database, JSON parsing, logging.

**Sync engine** (`include/mega/sync.h`, `src/sync.cpp`, `src/syncinternals/`, `include/mega/syncinternals/`):
- Types: `TYPE_TWOWAY`, `TYPE_UP`, `TYPE_DOWN`, `TYPE_BACKUP`
- Change detection: `CDM_NOTIFICATIONS` (filesystem events) or `CDM_PERIODIC_SCANNING`
- File identification via FileFingerprint (mtime + size + CRC/MAC-based checksums)
- mtime normalization handles filesystems that can't set mtime (Android, Synology SMB, network mounts)

**Platform-specific code:** `src/posix/`, `src/win32/`, `src/osx/`, `src/android/` — filesystem, networking, and OS-specific implementations.

**Bindings:** `bindings/ios/` (Objective-C), `bindings/java/` (SWIG-based Android), `bindings/qt/`.

**Dependencies (VCPKG, see `vcpkg.json`):** cryptopp, curl, icu, libsodium, sqlite3. Optional: openssl, freeimage, ffmpeg, pdfium, libuv, libmediainfo, readline, c-ares.

## Development Methodology

### Spec-Driven Development (SDD) + Test-Driven Development (TDD)

Follow this order for any bug fix or feature:

1. **Understand the spec / reproduce the problem.** Read the relevant code, analyze logs, confirm the scenario. Do not guess.
2. **Write the test first (TDD).** The test must encode the expected behavior and **fail** before the fix is applied. If it passes immediately, the test is not testing the right thing — revisit.
3. **Implement the fix / feature.** Only the minimal change needed.
4. **Run the test until it passes.** If it still fails, fix the code, not the test (see below).
5. **Verify stability.** Run the test (and related tests) **at least 2–3 consecutive times** to rule out timing-dependent flakiness.

### Test Discipline

These rules are non-negotiable:

- **Never relax assertions or convert strict checks to soft checks just to make a test pass.** If a test fails, the code is wrong — investigate and fix the code. The only exception is when a known, pre-existing issue is explicitly acknowledged and documented (with a clear explanation of the race/limitation and a note that it will be addressed separately). Even then, this requires explicit agreement.
- **A test that fails once is a signal, not noise.** Even if subsequent runs pass, investigate the failure by reading the full log. Timing-dependent races are real bugs. "It passed the next time" is never an acceptable resolution.
- **Review test logs even when tests pass.** Look for warnings, unexpected transfers, mtime changes, or state transitions that shouldn't happen. A passing test with wrong intermediate behavior is hiding a bug.
- **Use `--gtest_repeat=N` to stress-test flaky scenarios.** When investigating timing-dependent behavior, repeat the test 5–10 times rather than running it once and hoping.
- **Add diagnostic logging when investigating.** Instrument the code with temporary `LOG_debug` lines at key state transitions. Run the test until the failure is reproduced, analyze the log, then remove the diagnostics before finalizing. Don't guess at race conditions — prove them with log evidence.
- **Tests must be deterministic.** If a test depends on network timing or action-packet ordering, either make the timing explicit (use hooks, suspend/resume sync, wait for specific events) or document the timing dependency and make the assertion reflect what is guaranteed vs. what is timing-dependent.

### Code Fix Discipline

- **Don't fix tests to match broken behavior.** Fix the code to match expected behavior.
- **Don't introduce workarounds.** If the straightforward fix doesn't work, the analysis is incomplete — go deeper.
- **Don't modify `reassignFingerprints`, `checkMoves`, or other core sync-engine functions** without tracing the exact state transitions through diagnostic logging first. These functions have subtle invariants and interactions.
- **When a fix has unintended side effects on other tests**, revert it, re-analyze, and find a more targeted approach. Fixing one test at the expense of breaking others means the fix is wrong.

## Git Workflow

- Main branch: `develop`
- Branch naming: `task/SDK-XXXX-description` or `fix/SDK-XXXX-description`
- Commit prefixes: `fix:`, `test:`, `refactor:`, `feat:`, etc. Include issue key (e.g., `SDK-1234`) when applicable.
- Pre-commit hooks run secret scanning via gitleaks. Never commit credentials/tokens.
- No force pushes to `main`/`develop`.

## Methodology + workflow reference

The operational methodology developed during SDK-5360 (followup discipline,
auto-memory protocol, regression-sweep cadence, hard rules catalog) is
documented under [docs/methodology/](docs/methodology/README.md). Future
agent sessions and contributors should consult it; the
[docs/methodology/portable_template/](docs/methodology/portable_template/)
subtree is also reusable in other repos.

Single guides entry point: [docs/guides/](docs/guides/README.md).
