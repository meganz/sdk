/**
 * @file BenchSession.h
 * @brief Lightweight wall-clock + name lifecycle wrapper for a single benchmark cell.
 *
 * Part of the `bench_framework` reusable module gated by `MEGA_BENCH_FRAMEWORK_ENABLED`.
 * Designed so that an integration-test benchmark (existing `SdkTestBenchmark*` or future
 * `SdkBenchmarkDownload*`) can record its wall-clock duration and identifying metadata in
 * a uniform way, without each test re-inventing the timing scaffolding.
 *
 * Usage:
 *     mega::bench::BenchSession sess{"SdkTestBenchmarkSingleLargeUpload"};
 *     sess.start();
 *     // ... run benchmark ...
 *     sess.end();
 *     const auto durationMs = sess.durationMs();
 *
 * Construction does NOT auto-start the timer — call `start()` explicitly at the
 * measurement boundary.
 */
#pragma once

#include <chrono>
#include <string>

namespace mega::bench
{

class BenchSession
{
public:
    explicit BenchSession(std::string name);

    void start();
    void end();

    const std::string& name() const noexcept
    {
        return mName;
    }

    std::int64_t durationMs() const;
    std::int64_t durationNs() const;

    bool isRunning() const noexcept
    {
        return mStarted && !mEnded;
    }

private:
    std::string mName;
    std::chrono::steady_clock::time_point mStartTp{};
    std::chrono::steady_clock::time_point mEndTp{};
    bool mStarted = false;
    bool mEnded = false;
};

} // namespace mega::bench
