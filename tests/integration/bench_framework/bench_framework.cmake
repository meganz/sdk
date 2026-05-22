# bench_framework module — reusable benchmark scaffolding for test_integration.
#
# Gated by MEGA_BENCH_FRAMEWORK_ENABLED; default OFF in Release, ON in Debug presets.
# Provides BenchSession / BenchProcessStats / BenchTransferTimings / BenchSummary /
# BenchReportWriter — designed to absorb the inline bench helpers in SdkTest_test.cpp
# (G4) and to support future SdkBenchmarkDownload* cells without per-cell scaffolding.
#
# See `tests/integration/BENCHMARKS.md` for the JSON bench-report contract.

target_sources_conditional(test_integration
    FLAG MEGA_BENCH_FRAMEWORK_ENABLED
    PRIVATE
    bench_framework/headers/BenchSession.h
    bench_framework/BenchSession.cpp
    bench_framework/headers/BenchProcessStats.h
    bench_framework/BenchProcessStats.cpp
    bench_framework/headers/BenchTransferTimings.h
    bench_framework/headers/BenchSummary.h
    bench_framework/BenchSummary.cpp
    bench_framework/headers/BenchThrottleStats.h
    bench_framework/headers/BenchReportWriter.h
    bench_framework/BenchReportWriter.cpp
)

target_include_directories(test_integration PRIVATE
    $<$<BOOL:${MEGA_BENCH_FRAMEWORK_ENABLED}>:${CMAKE_CURRENT_SOURCE_DIR}/bench_framework/headers>
)

target_compile_definitions(test_integration PRIVATE
    $<$<BOOL:${MEGA_BENCH_FRAMEWORK_ENABLED}>:MEGA_BENCH_FRAMEWORK_ENABLED=1>
)
