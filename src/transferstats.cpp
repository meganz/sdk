/**
 * @file transferstats.cpp
 * @brief Calculate and collect transfer metrics
 *
 * (c) 2024 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "mega/transferstats.h"

#include "mega/logging.h"
#include "mega/megaclient.h"
#include "mega/transferslot.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>

namespace mega::stats
{

// TransferData
bool TransferStats::TransferData::checkDataStateValidity() const
{
    std::string notValidAndErroneousValuesErrMsg;
    auto checkTransferFieldValidity =
        [&notValidAndErroneousValuesErrMsg](const std::string& fieldName,
                                            const auto fieldValue) -> bool
    {
        if (fieldValue <= 0)
        {
            LOG_debug << "[TransferStats::checkPreconditions] " << fieldName
                      << " for this transfer (" << fieldValue << ") is not valid";
            if (fieldValue < 0)
            {
                // Fields can be 0 under certain conditions (even when we need to discard the
                // metrics) but, if they are lower than 0, then there is an error somewhere.
                notValidAndErroneousValuesErrMsg +=
                    ("Invalid " + std::string(fieldName) + " value (" + std::to_string(fieldValue) +
                     "). ");
            }
            return false;
        }
        return true;
    };

    bool transferFieldsAreValid = true;
    transferFieldsAreValid &= checkTransferFieldValidity("size", mSize);
    transferFieldsAreValid &= checkTransferFieldValidity("speed", mSpeed);
    transferFieldsAreValid &= checkTransferFieldValidity("latency", mLatency);

    assert(notValidAndErroneousValuesErrMsg.empty() && notValidAndErroneousValuesErrMsg.c_str());

    return transferFieldsAreValid;
}

// Metrics
std::string TransferStats::Metrics::toString(const std::string& separator) const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2); // Two decimal precision.

    // Use the endOfLine for each field to ensure proper formatting
    oss << "Transfer type: " << mTransferType << separator;
    oss << "Median Size: " << mMedianSize << separator;
    oss << "Contraharmonic Mean Size: " << mContraharmonicMeanSize << separator;
    oss << "Median Speed: " << mMedianSpeed << separator;
    oss << "Weighted Avg Speed: " << mWeightedAverageSpeed << separator;
    oss << "Max Speed: " << mMaxSpeed << separator;
    oss << "Avg Latency: " << mAvgLatency << separator;
    oss << "Failed Request Ratio: " << mFailedRequestRatio << separator;
    oss << "Raided Transfer Ratio: " << mRaidedTransferRatio;

    return oss.str();
}

std::string TransferStats::Metrics::toJson() const
{
    std::ostringstream oss;
    oss << std::fixed
        << std::setprecision(2); // Ensure two decimal precision for floating-point values

    const auto addField = [&oss](auto&& fieldName, auto&& value, const bool last = false)
    {
        oss << "\\\"" << fieldName << "\\\":\\\"" << value << "\\\"";
        if (!last)
            oss << ",";
    };

    oss << R"(\"tm\":{)";
    addField("t", mTransferType);
    addField("ml", mMedianSize);
    addField("wl", mContraharmonicMeanSize);
    addField("ms", (mMedianSpeed / 1024));
    addField("ws", (mWeightedAverageSpeed / 1024));
    addField("zs", (mMaxSpeed / 1024));
    addField("al", mAvgLatency);
    addField("fr", mFailedRequestRatio);
    addField("rr", mRaidedTransferRatio, true);
    oss << R"(})";
    return oss.str();
}

// TransferStats
bool TransferStats::addTransferData(TransferData&& transferData)
{
    // Check all preconditions.
    if (!transferData.checkDataStateValidity())
    {
        LOG_debug << "[TransferStats::addTransferStats] Some fields are not valid. Stats skipped "
                     "for this transfer";
        return false;
    }

    // Remove old transfers.
    const auto now = std::chrono::steady_clock::now();
    while (!mTransfersData.empty() && (std::chrono::duration_cast<std::chrono::seconds>(
                                           now - mTransfersData.front().mTimestamp)
                                           .count() > mMaxAgeSeconds))
    {
        mTransfersData.pop_front();
    }

    mUncollectedAndPrintedTransferData.first += 1;
    mUncollectedAndPrintedTransferData.second += transferData.mSize;
    LOG_debug << "[TransferStats::addTransferData] uncollected=("
              << mUncollectedAndPrintedTransferData.first << ", "
              << mUncollectedAndPrintedTransferData.second << ")";

    // Update the timestamp and move the transferData to the collection.
    transferData.mTimestamp = now;
    mTransfersData.push_back(std::move(transferData));

    // Remove excessive entries if necessary.
    if (mTransfersData.size() > mMaxEntries)
    {
        mTransfersData.pop_front();
    }

    return true;
}

TransferStats::Metrics TransferStats::collectMetrics(const direction_t type) const
{
    checkTransferTypeValidity(type);

    TransferStats::Metrics metrics = {};

    if (mTransfersData.empty())
    {
        return metrics;
    }

    // Set transfer type (PUT or GET).
    metrics.mTransferType = type;

    // Set the number of transfers used to calculate these metrics.
    metrics.mNumTransfers = mTransfersData.size();

    // Declare sizes & speeds vectors and accumulated values.
    std::vector<m_off_t> sizes;
    std::vector<m_off_t> speeds;
    sizes.reserve(mTransfersData.size());
    speeds.reserve(mTransfersData.size());
    double totalLatency = 0.0;
    double totalFailedRequestRatios = 0.0;
    size_t totalRaidedTransfers = 0;

    for (const auto& transferData: mTransfersData)
    {
        sizes.push_back(transferData.mSize);
        speeds.push_back(transferData.mSpeed);

        // Directly accumulate values for latencies, failed request ratios, and raided transfers.
        totalLatency += transferData.mLatency;
        totalFailedRequestRatios += transferData.mFailedRequestRatio;
        totalRaidedTransfers += transferData.mIsRaided ? 1 : 0;
    }

    // Sort the vectors in place.
    std::sort(sizes.begin(), sizes.end());
    std::sort(speeds.begin(), speeds.end());

    // Calculate median and weighted averages.
    metrics.mMedianSize = calculateMedian(sizes);
    metrics.mContraharmonicMeanSize = calculateWeightedAverage(sizes, sizes);
    metrics.mMedianSpeed = calculateMedian(speeds);
    metrics.mWeightedAverageSpeed = calculateWeightedAverage(speeds, sizes);

    // Calculate max speed from the sorted speeds vector (last element is the max).
    metrics.mMaxSpeed = speeds.back();

    // Calculate average latency.
    metrics.mAvgLatency =
        static_cast<m_off_t>(std::round(totalLatency / static_cast<double>(mTransfersData.size())));

    // Calculate failed request ratio (with precision to 2 decimals).
    metrics.mFailedRequestRatio =
        std::round((totalFailedRequestRatios / static_cast<double>(mTransfersData.size())) *
                   100.0) /
        100.0;

    // Calculate raided transfer ratio (with precision to 2 decimals).
    if (type == GET)
    {
        metrics.mRaidedTransferRatio = std::round((static_cast<double>(totalRaidedTransfers) /
                                                   static_cast<double>(mTransfersData.size())) *
                                                  100.0) /
                                       100.0;
    }

    return metrics;
}

TransferStats::Metrics TransferStats::collectAndPrintMetrics(const direction_t type,
                                                             const std::string& separator)
{
    const auto metrics = collectMetrics(type);
    printMetrics(metrics, separator);
    mUncollectedAndPrintedTransferData = {};
    return metrics;
}

// TransferStatsManager
bool TransferStatsManager::addTransferStats(const Transfer* const transfer)
{
    // Check preconditions.
    if (!checkTransferStateValidity(transfer))
    {
        return false;
    }

#ifdef MEGA_USE_WSUPLOAD
    if (transfer->channel == Transfer::Channel::WebSocket)
    {
        if (!transfer->client || !transfer->client->wsEngine())
        {
            LOG_debug << "[TransferStatsManager::addTransferStats] Missing WS engine for "
                         "WebSocket transfer";
            return false;
        }

        ws::UploadEngine::WsTransferStats wsStats;
        if (!transfer->client->wsEngine()->getTransferStats(*transfer, wsStats))
        {
            if (transfer->ws_latched_mean_speed > 0 && transfer->ws_latched_avg_latency_ms > 0)
            {
                wsStats.meanSpeedBytesPerSecond = transfer->ws_latched_mean_speed;
                wsStats.avgStartTransferTime =
                    std::chrono::milliseconds(transfer->ws_latched_avg_latency_ms);
                wsStats.failedRequestRatio = transfer->ws_latched_failed_request_ratio;
                LOG_debug << "[TransferStatsManager::addTransferStats] Using latched WS stats "
                             "after engine detach [size="
                          << transfer->size << "]";
            }
            else
            {
                LOG_debug << "[TransferStatsManager::addTransferStats] WS stats unavailable and "
                             "no latched fallback [size="
                          << transfer->size << "]";
                return false;
            }
        }

        const m_off_t meanSpeed = std::max<m_off_t>(1, wsStats.meanSpeedBytesPerSecond);
        const m_off_t avgLatencyMs =
            std::max<m_off_t>(1, static_cast<m_off_t>(wsStats.avgStartTransferTime.count()));
        TransferStats::TransferData transferData{
            transfer->size,
            meanSpeed,
            static_cast<double>(avgLatencyMs),
            wsStats.failedRequestRatio,
            false};

        std::lock_guard<std::mutex> guard(mTransferStatsMutex);
        const bool added = mUploadStatistics.addTransferData(std::move(transferData));
        LOG_debug << "[TransferStatsManager::addTransferStats] WS stats "
                  << (added ? "added" : "discarded") << " [size=" << transfer->size
                  << " meanSpeed=" << meanSpeed << " B/s"
                  << " avgLatencyMs=" << wsStats.avgStartTransferTime.count()
                  << " failedRatio=" << wsStats.failedRequestRatio << "]";
        return added;
    }
#endif

    // Add transfer stats.
    TransferStats::TransferData transferData{transfer->size,
                                             transfer->slot->mTransferSpeed.getMeanSpeed(),
                                             transfer->slot->tsStats.averageStartTransferTime(),
                                             transfer->slot->tsStats.failedRequestRatio(),
                                             transfer->slot->transferbuf.isRaid() ||
                                                 transfer->slot->transferbuf.isNewRaid()};

    std::lock_guard<std::mutex> guard(mTransferStatsMutex);
    return transfer->type == PUT ? mUploadStatistics.addTransferData(std::move(transferData)) :
                                   mDownloadStatistics.addTransferData(std::move(transferData));
}

std::string TransferStatsManager::metricsToJsonForTransferType(const direction_t type) const
{
    const TransferStats::Metrics metrics = collectMetrics(type);
    return metrics.toJson();
}

TransferStats::Metrics TransferStatsManager::collectMetrics(const direction_t type) const
{
    checkTransferTypeValidity(type);

    std::lock_guard<std::mutex> guard(mTransferStatsMutex);
    return type == PUT ? mUploadStatistics.collectMetrics(type) :
                         mDownloadStatistics.collectMetrics(type);
}

TransferStats::Metrics TransferStatsManager::collectAndPrintMetrics(const direction_t type,
                                                                    const std::string& separator)
{
    checkTransferTypeValidity(type);

    LOG_info << (type == PUT ? "[UploadStatistics]" : "[DownloadStatistics]")
             << " Number of transfers: " << size(type) << ". Max entries: " << getMaxEntries(type)
             << ". Max age in seconds: " << getMaxAgeSeconds(type);

    std::lock_guard<std::mutex> guard(mTransferStatsMutex);
    return type == PUT ? mUploadStatistics.collectAndPrintMetrics(type, separator) :
                         mDownloadStatistics.collectAndPrintMetrics(type, separator);
}

std::optional<TransferStats::Metrics>
    TransferStatsManager::collectAndPrintTransferStatsIfLimitReached(const direction_t type,
                                                                     const std::string& separator)
{
    if (const auto [numTransfers, totalSize] = getUncollectedAndPrintedTransferData(type);
        (numTransfers < NUM_ENTRIES_FOR_LOGGING && totalSize < TOTAL_SIZE_FOR_LOGGING))
        return {};

    const auto metrics = collectAndPrintMetrics(type, separator);
    assert(getUncollectedAndPrintedTransferData(type) ==
           TransferStats::UncollectedTransfersCounters{});

    return metrics;
}

size_t TransferStatsManager::size(const direction_t type) const
{
    checkTransferTypeValidity(type);

    std::lock_guard<std::mutex> guard(mTransferStatsMutex);
    return type == PUT ? mUploadStatistics.size() : mDownloadStatistics.size();
}

size_t TransferStatsManager::getMaxEntries(const direction_t type) const
{
    checkTransferTypeValidity(type);

    std::lock_guard<std::mutex> guard(mTransferStatsMutex);
    return type == PUT ? mUploadStatistics.getMaxEntries() : mDownloadStatistics.getMaxEntries();
}

int64_t TransferStatsManager::getMaxAgeSeconds(const direction_t type) const
{
    checkTransferTypeValidity(type);

    std::lock_guard<std::mutex> guard(mTransferStatsMutex);
    return type == PUT ? mUploadStatistics.getMaxAgeSeconds() :
                         mDownloadStatistics.getMaxAgeSeconds();
}

TransferStats::UncollectedTransfersCounters
    TransferStatsManager::getUncollectedAndPrintedTransferData(const direction_t type) const
{
    std::lock_guard<std::mutex> guard(mTransferStatsMutex);
    const auto counters = type == PUT ? mUploadStatistics.getUncollectedAndPrintedTransferData() :
                                        mDownloadStatistics.getUncollectedAndPrintedTransferData();
    return counters;
}

// Utils
void printMetrics(const TransferStats::Metrics& metrics, const std::string& separator)
{
    LOG_info << metrics.toString(separator);
}

m_off_t calculateMedian(const vector<m_off_t>& sortedValues)
{
    const size_t n = sortedValues.size();
    if (n == 0)
    {
        return 0;
    }

    // If the number of elements is even, return the average of the two middle elements.
    if (n % 2 == 0)
    {
        return static_cast<m_off_t>(
            std::round(static_cast<double>(sortedValues[n / 2 - 1] + sortedValues[n / 2]) / 2.0));
    }

    // If the number of elements is odd, return the middle element.
    return sortedValues[n / 2];
}

m_off_t calculateWeightedAverage(const vector<m_off_t>& values, const vector<m_off_t>& weights)
{
    if (values.size() != weights.size())
    {
        LOG_warn << "[calculateWeightedAverage] Values size (" << values.size()
                 << ") != weights size (" << weights.size() << "). Skipping";
        assert(false && "[calculateWeightedAverage] Vector of values and vector of weights have "
                        "different sizes");
        return 0;
    }

    long double weightedSum = 0.0L;
    long double totalWeight = 0.0L;
    for (size_t i = 0; i < values.size(); ++i)
    {
        weightedSum += static_cast<long double>(values[i]) *
                       static_cast<long double>(weights[i]);
        totalWeight += static_cast<long double>(weights[i]);
    }
    if (weightedSum == 0.0L || totalWeight == 0.0L)
    {
        return 0;
    }

    const long double weightedAverage = weightedSum / totalWeight;
    if (!std::isfinite(weightedAverage))
    {
        LOG_warn << "[calculateWeightedAverage] Non-finite weighted average calculated. Skipping";
        return 0;
    }

    constexpr auto minValue = static_cast<long double>(std::numeric_limits<m_off_t>::min());
    constexpr auto maxValue = static_cast<long double>(std::numeric_limits<m_off_t>::max());
    const long double clampedAverage = std::clamp(weightedAverage, minValue, maxValue);
    return static_cast<m_off_t>(std::llround(clampedAverage));
}

void checkTransferTypeValidity([[maybe_unused]] const direction_t type)
{
    assert((type == PUT || type == GET) && "Only uploads (PUT) or downloads (GET) are allowed");
}

bool checkTransferStateValidity(const Transfer* const transfer)
{
    auto checkTransferStateCondition = [](bool transferStateCondition,
                                          const std::string& errorMsg,
                                          bool triggerAssert = true) -> bool
    {
        if (!transferStateCondition)
        {
            LOG_err << errorMsg;
            if (triggerAssert)
                assert(false && errorMsg.c_str());
            return false;
        }
        return true;
    };

    if (!checkTransferStateCondition(transfer != nullptr,
                                     "[checkTransferStateValidity] It is a NULL transfer"))
    {
        return false;
    }

    if (!checkTransferStateCondition(
            transfer->type == PUT || transfer->type == GET,
            "[checkTransferStateValidity] called with an invalid transfer type"))
    {
        return false;
    }

    if (transfer->channel == Transfer::Channel::WebSocket)
    {
        return checkTransferStateCondition(
            transfer->type == PUT,
            "[checkTransferStateValidity] WebSocket channel used for non-PUT transfer");
    }

    if (!checkTransferStateCondition(
            transfer->slot != nullptr,
            "[checkTransferStateValidity] called with a NULL transfer slot"))
    {
        return false;
    }

    if (!checkTransferStateCondition(
            !transfer->tempurls.empty(),
            "[checkTransferStateValidity] This transfer didn't initialize the "
            "transferbuf, it will be discarded for stats",
            false))
    {
        return false;
    }

    return true;
}

} // namespace mega::stats
