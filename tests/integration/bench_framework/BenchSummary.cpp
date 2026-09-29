#include "headers/BenchSummary.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mega::bench
{

BenchSummaryDistribution computeDistribution(std::vector<double>& samplesInOut)
{
    BenchSummaryDistribution out;
    if (samplesInOut.empty())
        return out;

    std::sort(samplesInOut.begin(), samplesInOut.end());
    out.n = samplesInOut.size();
    out.min = samplesInOut.front();
    out.max = samplesInOut.back();
    out.mean = std::accumulate(samplesInOut.begin(), samplesInOut.end(), 0.0) /
               static_cast<double>(out.n);

    const auto median_idx = out.n / 2;
    if (out.n % 2 == 1)
    {
        out.median = samplesInOut[median_idx];
    }
    else
    {
        out.median = (samplesInOut[median_idx - 1] + samplesInOut[median_idx]) * 0.5;
    }

    // p95: index = floor(0.95 * (n - 1)).
    const auto p95_idx = static_cast<std::size_t>(0.95 * static_cast<double>(out.n - 1));
    out.p95 = samplesInOut[p95_idx];
    return out;
}

double computeAggregateKbps(const std::vector<BenchTransferTiming>& timings)
{
    if (timings.empty())
        return 0.0;
    std::int64_t totalBytes = 0;
    std::int64_t totalMs = 0;
    for (const auto& t: timings)
    {
        totalBytes += t.bytes;
        totalMs += t.totalMs();
    }
    if (totalMs <= 0)
        return 0.0;
    // KB/s = bytes / 1024 / (totalMs / 1000) = bytes * 1000 / 1024 / totalMs.
    return static_cast<double>(totalBytes) * 1000.0 / 1024.0 /
           static_cast<double>(totalMs);
}

std::vector<double> totalMsSamples(const std::vector<BenchTransferTiming>& timings)
{
    std::vector<double> out;
    out.reserve(timings.size());
    for (const auto& t: timings)
    {
        out.push_back(static_cast<double>(t.totalMs()));
    }
    return out;
}

} // namespace mega::bench
