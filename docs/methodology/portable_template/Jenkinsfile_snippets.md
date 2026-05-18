# Jenkinsfile snippets — trigger phrase parsing + opt-in stages

Copy-paste blocks from the SDK-5360 Linux Jenkinsfile. Adapt the
variable names, presets, and build dirs to your project. These cover:

1. **Trigger phrase parsing**: read flags out of the GitLab MR comment.
2. **Opt-in bench stage** (`--bench`): build with bench framework ON,
   run the bench filter, archive JSON + per-cell logs.
3. **Opt-in TSAN stage** (`--tsan`): build with TSAN ON, run the
   v2-surface filter, archive logs.
4. **Per-cell log split**: split a combined `[ RUN ]`-delimited log
   into per-cell `<TestName>.log` files for easier review.

The full reference lives in
`jenkinsfile/Jenkinsfile_MR_linux_cmake` in the SDK repo.

## 1. Trigger phrase parsing

Reads `$gitlabTriggerPhrase` (the GitLab plugin sets this) and extracts:

- `--gtest_filter=<pattern>` (forwarded to all stages).
- `--gtest_repeat=<n>` (forwarded; ideal for HR43-style stress).
- `--bench` toggle (enables the opt-in bench stage).
- `--tsan` toggle (enables the opt-in TSAN stage).
- `--sequence` toggle (forces sequential test runs).
- `--timeout=<min>` override.

```groovy
stage('Get parameters') {
    parallel {
        stage('Get build and run parameters') {
            steps {
                script {
                    BUILD_OPTIONS = sh(
                        script: 'echo "$gitlabTriggerPhrase" | grep BUILD_OPTIONS | awk -F "BUILD_OPTIONS=" \'{print $2}\' | cut -d"\\"" -f2 || :',
                        returnStdout: true).trim()
                    TESTS_PARALLEL = sh(
                        script: 'echo "$gitlabTriggerPhrase" | grep "\\-\\-sequence" >/dev/null 2>&1 && echo "" || echo "--INSTANCES:10"',
                        returnStdout: true).trim()
                    GTEST_REPEAT = sh(
                        script: 'echo "$gitlabTriggerPhrase" | grep --only-matching "\\-\\-gtest_repeat=[^ ]*" | awk -F "gtest_repeat=" \'{print "--gtest_repeat="$2}\'|| :',
                        returnStdout: true).trim()
                    GTEST_FILTER = sh(
                        script: 'echo "$gitlabTriggerPhrase" | grep --only-matching "\\-\\-gtest_filter=[^ ]*" | awk -F "gtest_filter=" \'{print "--gtest_filter="$2}\'|| :',
                        returnStdout: true).trim()
                    CUSTOM_TIMEOUT = sh(
                        script: 'echo "$gitlabTriggerPhrase" | grep --only-matching "\\-\\-timeout=[^ ]*" | awk -F "timeout=" \'{print $2}\'|| :',
                        returnStdout: true).trim()
                    CUSTOM_TIMEOUT = CUSTOM_TIMEOUT ?: "250"
                    RUN_TSAN_SWEEP = sh(
                        script: 'echo "$gitlabTriggerPhrase" | grep "\\-\\-tsan" >/dev/null 2>&1 && echo "true" || echo "false"',
                        returnStdout: true).trim()
                    RUN_BENCH_SWEEP = sh(
                        script: 'echo "$gitlabTriggerPhrase" | grep "\\-\\-bench" >/dev/null 2>&1 && echo "true" || echo "false"',
                        returnStdout: true).trim()
                    println "BUILD_OPTIONS=${BUILD_OPTIONS}"
                    println "TESTS_PARALLEL=${TESTS_PARALLEL}"
                    println "GTEST_REPEAT=${GTEST_REPEAT}"
                    println "GTEST_FILTER=${GTEST_FILTER}"
                    println "CUSTOM_TIMEOUT=${CUSTOM_TIMEOUT}"
                    println "RUN_TSAN_SWEEP=${RUN_TSAN_SWEEP}"
                    println "RUN_BENCH_SWEEP=${RUN_BENCH_SWEEP}"
                }
            }
        }
    }
}
```

## 2. Opt-in bench stage (`--bench`)

The bench stage uses a **separate build dir** with `MEGA_BENCH_FRAMEWORK_ENABLED=ON`
so the regular MR build artifacts are not polluted with bench output.

```groovy
stage('Run Benchmarks') {
    when {
        expression { return RUN_BENCH_SWEEP == 'true' }
    }
    environment {
        BUILD_DIR = "build_dir"
        BENCH_FILTER = "SdkBenchmarkTest.*"  // fallback when no --gtest_filter
    }
    steps {
        timeout(time: 120, unit: 'MINUTES') {
            script {
                dir("${workspace}") {
                    sh """#!/bin/bash
                    set -x
                    ulimit -c unlimited
                    rm -rf ${env.BUILD_DIR}_bench
                    mkdir ${env.BUILD_DIR}_bench
                    cmake -DCMAKE_BUILD_TYPE=Debug \\
                        -DMEGA_BENCH_FRAMEWORK_ENABLED=ON \\
                        -S ${workspace} -B ${workspace}/${env.BUILD_DIR}_bench
                    cmake --build ${workspace}/${env.BUILD_DIR}_bench -j5 --target test_integration

                    if [ -n "${GTEST_FILTER}" ]; then
                        BENCH_FILTER_ARG="${GTEST_FILTER}"
                    else
                        BENCH_FILTER_ARG="--gtest_filter=${env.BENCH_FILTER}"
                    fi
                    ${env.BUILD_DIR}_bench/tests/integration/test_integration \\
                        --CI \\
                        --USERAGENT:${env.USER_AGENT_TESTS_SDK} \\
                        "\$BENCH_FILTER_ARG" ${GTEST_REPEAT} \\
                        2>&1 | tee bench_sweep.log || true

                    # Archive bench_report JSONs (HR41)
                    for benchReport in ${env.BUILD_DIR}_bench/pid_*/bench_report_*.json; do
                        [ -f "\$benchReport" ] && cp "\$benchReport" .
                    done
                    """
                }
            }
        }
    }
    post {
        always {
            archiveArtifacts artifacts: 'bench_report_*.json,bench_sweep_*.log.gz,bench_logs_*.tar.gz',
                             allowEmptyArchive: true
        }
    }
}
```

## 3. Opt-in TSAN stage (`--tsan`)

Builds a separate dir with `ENABLE_TSAN=ON`. Note: Windows MSVC has no
TSAN runtime — skip this stage on Windows pipelines.

```groovy
stage('TSAN Build') {
    when {
        expression { return RUN_TSAN_SWEEP == 'true' }
    }
    environment {
        TSAN_BUILD_DIR = "build_dir_tsan"
    }
    steps {
        dir("${workspace}") {
            sh "rm -rf ${TSAN_BUILD_DIR}; mkdir ${TSAN_BUILD_DIR}"
            sh """cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON \\
                -S ${workspace} -B ${workspace}/${TSAN_BUILD_DIR}"""
            sh "cmake --build ${workspace}/${TSAN_BUILD_DIR} -j5 --target test_integration"
        }
    }
}

stage('TSAN Sweep') {
    when {
        expression { return RUN_TSAN_SWEEP == 'true' }
    }
    environment {
        TSAN_BUILD_DIR = "build_dir_tsan"
        TSAN_OPTIONS = 'halt_on_error=0:second_deadlock_stack=1:history_size=7:report_thread_leaks=0'
        // V2-surface filter (your project: customise the cell list)
        TSAN_V2_SURFACE_FILTER = "SdkWsUploadTest.ActivePoolUsesParallelConnections:SdkWsUploadTest.B9ClosedThrottleReconnectPacing:SdkWsUploadTest.InvalidPinnedSessionFallsBackToFreshSession:SdkWsUploadTest.OverquotaDuringTransfer"
    }
    steps {
        timeout(time: 120, unit: 'MINUTES') {
            dir("${workspace}") {
                sh """
                if [ -n "${GTEST_FILTER}" ]; then
                    TSAN_FILTER_ARG="${GTEST_FILTER}"
                else
                    TSAN_FILTER_ARG="--gtest_filter=${env.TSAN_V2_SURFACE_FILTER}"
                fi
                ${TSAN_BUILD_DIR}/tests/integration/test_integration \\
                    --CI --USERAGENT:JenkinsCanSpam-SDK \\
                    "\$TSAN_FILTER_ARG" ${GTEST_REPEAT} \\
                    2>&1 | tee tsan_sweep.log || true
                gzip -c tsan_sweep.log > tsan_sweep_${BUILD_ID}.log.gz
                rm tsan_sweep.log
                """
            }
        }
    }
    post {
        always {
            archiveArtifacts artifacts: 'tsan_sweep_*.log.gz', allowEmptyArchive: true
        }
    }
}
```

## 4. Per-cell log split

Splits a `[ RUN ]`-delimited gtest log into per-cell files and packs
into `tar.gz`. Useful for both bench and TSAN sweeps so reviewers can
fetch one cell directly.

```bash
mkdir -p bench_logs_${BUILD_ID}
if [ -s bench_sweep.log ]; then
    csplit -z -s -b '%04d' -f bench_logs_${BUILD_ID}/_split_ bench_sweep.log '/\[ RUN      \]/' '{*}' || :
    for sf in bench_logs_${BUILD_ID}/_split_*; do
        [ -s "$sf" ] || { rm -f "$sf"; continue; }
        cellName=$(grep -m1 -oE '\[ RUN      \] [A-Za-z0-9_.]+' "$sf" | awk '{print $3}' | tr '.' '_' || echo 'preamble')
        [ -n "$cellName" ] || cellName="preamble"
        mv "$sf" "bench_logs_${BUILD_ID}/${cellName}.log" || :
    done
fi
tar -czf bench_logs_${BUILD_ID}.tar.gz bench_logs_${BUILD_ID} || :
rm -rf bench_logs_${BUILD_ID} || :
gzip -c bench_sweep.log > bench_sweep_${BUILD_ID}.log.gz || :
rm bench_sweep.log || :
```

## Conventions to honour when adapting

- Bench stage uses a **separate build dir** (`_bench` suffix). Do not
  contaminate the regular build with the bench framework.
- The opt-in stages default to a sensible fallback filter when no
  `--gtest_filter=` is supplied. Keep that fallback narrow (the
  v2-surface filter, not the whole TSAN test suite).
- Honour `--gtest_repeat=` from the trigger phrase; HR43 calls for
  n≥15 on timing cells.
- USERAGENT must be the team's whitelisted value (in SDK-5360:
  `JenkinsCanSpam-SDK`).
- Archive both the per-cell tarball AND the full gzipped log; the
  full log is needed for cross-cell context.
- Windows TSAN is unsupported — `when { not { branch '*windows*' } }`
  or equivalent guard.
