#include "BenchSession.h"

namespace mega::bench
{

BenchSession::BenchSession(std::string name): mName(std::move(name)) {}

void BenchSession::start()
{
    mStartTp = std::chrono::steady_clock::now();
    mStarted = true;
    mEnded = false;
}

void BenchSession::end()
{
    mEndTp = std::chrono::steady_clock::now();
    mEnded = true;
}

std::int64_t BenchSession::durationMs() const
{
    if (!mStarted)
        return 0;
    const auto endTp = mEnded ? mEndTp : std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(endTp - mStartTp).count();
}

std::int64_t BenchSession::durationNs() const
{
    if (!mStarted)
        return 0;
    const auto endTp = mEnded ? mEndTp : std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(endTp - mStartTp).count();
}

} // namespace mega::bench
