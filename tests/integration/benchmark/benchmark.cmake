# benchmark module — fixture-class wiring for SdkBenchmarkTest cells.
#
# The 4 SdkBenchmark* test bodies (ManySmallUploads / 1kSmallUploads /
# SingleLargeUpload / LargePlusManySmall) live in SdkTest_test.cpp; this
# module owns the fixture-class declaration so future extraction has a
# clean target (mirrors wsupload/wsupload.cmake).
#
# Unconditional — the 4 cells themselves compile in every test_integration
# build (no #ifdef on their TEST_F sites); the bench_framework output is
# the part gated by MEGA_BENCH_FRAMEWORK_ENABLED.

target_sources(test_integration
    PRIVATE
    benchmark/BenchmarkRunners.h
    benchmark/BenchmarkRunners.cpp
    benchmark/SdkBenchmarkTest.h
    benchmark/SdkBenchmarkTest.cpp
)
