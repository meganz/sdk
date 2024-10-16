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
#include "mega/transferslot.h"

#include <chrono>
#include <iomanip>
#include <numeric>

namespace mega
{

namespace stats
{

// TransferData
bool TransferStats::TransferData::checkDataStateValidity() const
{
    auto checkTransferFieldValidity = [](const std::string_view& fieldName,
                                         const auto fieldValue) -> bool
    {
        if (fieldValue <= 0)
        {
            LOG_debug << "[TransferStats::checkPreconditions] " << fieldName
                      << " for this transfer (" << fieldValue << ") is not valid";
            assert((fieldValue == 0) &&
                   ("Invalid " + std::string(fieldName) + " for transfer stats")
                       .c_str()); // Fields can be 0 under certain conditions.
            return false;
        }
        return true;
    };

    bool transferFieldsAreValid = true;
    transferFieldsAreValid &= checkTransferFieldValidity("size", mSize);
    transferFieldsAreValid &= checkTransferFieldValidity("speed", mSpeed);
    transferFieldsAreValid &= checkTransferFieldValidity("latency", mLatency);

    return transferFieldsAreValid;
}

// Metrics
std::string TransferStats::Metrics::toString(const std::string_view separator) const
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

    oss << "tm:{t" << mTransferType << ",ml:" << mMedianSize << ",wl:" << mContraharmonicMeanSize
        << ",ms:" << (mMedianSpeed / 1024) << ",ws:" << (mWeightedAverageSpeed / 1024)
        << ",zs:" << (mMaxSpeed / 1024) << ",al:" << mAvgLatency << ",fr:" << mFailedRequestRatio
        << ",rr:" << mRaidedTransferRatio << "}";

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
    TransferStats::Metrics metrics = {};

    if (mTransfersData.empty())
    {
        return metrics;
    }

    // Set transfer type (PUT or GET).
    assert((type == PUT || type == GET) && "Invalid transfer type!");
    metrics.mTransferType = type;

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
    metrics.mMedianSize = utils::calculateMedian(sizes);
    metrics.mContraharmonicMeanSize = utils::calculateWeightedAverage(sizes, sizes);
    metrics.mMedianSpeed = utils::calculateMedian(speeds);
    metrics.mWeightedAverageSpeed = utils::calculateWeightedAverage(speeds, sizes);

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

// TransferStatsManager
bool TransferStatsManager::transferStateIsValid(const Transfer* const transfer)
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

    if (!checkTransferStateCondition(
            transfer != nullptr,
            "[TransferStatsManager::transferStateIsValid] called with a NULL transfer"))
    {
        return false;
    }

    if (!checkTransferStateCondition(
            transfer->type == PUT || transfer->type == GET,
            "[TransferStatsManager::transferStateIsValid] called with an invalid transfer type"))
    {
        return false;
    }

    if (!checkTransferStateCondition(
            transfer->slot != nullptr,
            "[TransferStatsManager::transferStateIsValid] called with a NULL transfer slot"))
    {
        return false;
    }

    if (!checkTransferStateCondition(
            !transfer->tempurls.empty(),
            "[TransferStatsManager::transferStateIsValid] This transfer didn't initialize the "
            "transferbuf, it will be discarded for stats",
            false))
    {
        return false;
    }

    return true;
}

bool TransferStatsManager::addTransferStats(const Transfer* const transfer)
{
    // Check preconditions.
    if (!transferStateIsValid(transfer))
    {
        return false;
    }

    // Add transfer stats.
    TransferStats::TransferData transferData{transfer->size,
                                             transfer->slot->mTransferSpeed.getMeanSpeed(),
                                             transfer->slot->tsStats.averageLatency(),
                                             transfer->slot->tsStats.failedRequestRatio(),
                                             transfer->slot->transferbuf.isRaid() ||
                                                 transfer->slot->transferbuf.isNewRaid()};

    std::lock_guard<std::mutex> guard(mTransferStatsMutex);
    return transfer->type == PUT ? mUploadStatistics.addTransferData(std::move(transferData)) :
                                   mDownloadStatistics.addTransferData(std::move(transferData));
}

std::string TransferStatsManager::metricsToJsonForTransferType(const direction_t type) const
{
    return metricsToJson(collectMetrics(type));
}

TransferStats::Metrics TransferStatsManager::collectMetrics(const direction_t type) const
{
    checkValidTransferType(type);

    std::lock_guard<std::mutex> guard(mTransferStatsMutex);
    return type == PUT ? mUploadStatistics.collectMetrics(type) :
                         mDownloadStatistics.collectMetrics(type);
}

TransferStats::Metrics
    TransferStatsManager::collectAndPrintMetrics(const direction_t type,
                                                 const std::string_view separator) const
{
    LOG_info << (type == PUT ? "[UploadStatistics]" : "[DownloadStatistics]")
             << " Number of transfers: " << size(type) << ". Max entries: " << getMaxEntries(type)
             << ". Max age in seconds: " << getMaxAgeSeconds(type);

    TransferStats::Metrics metrics = collectMetrics(type);
    utils::printMetrics(metrics, separator);
    return metrics;
}

size_t TransferStatsManager::size(const direction_t type) const
{
    checkValidTransferType(type);

    std::lock_guard<std::mutex> guard(mTransferStatsMutex);
    return type == PUT ? mUploadStatistics.size() : mDownloadStatistics.size();
}

size_t TransferStatsManager::getMaxEntries(const direction_t type) const
{
    checkValidTransferType(type);

    std::lock_guard<std::mutex> guard(mTransferStatsMutex);
    return type == PUT ? mUploadStatistics.getMaxEntries() : mDownloadStatistics.getMaxEntries();
}

int64_t TransferStatsManager::getMaxAgeSeconds(const direction_t type) const
{
    checkValidTransferType(type);

    std::lock_guard<std::mutex> guard(mTransferStatsMutex);
    return type == PUT ? mUploadStatistics.getMaxAgeSeconds() :
                         mDownloadStatistics.getMaxAgeSeconds();
}

// Utils
std::string metricsToJson(const TransferStats::Metrics& metrics)
{
    return metrics.toJson();
}

void printMetrics(const TransferStats::Metrics& metrics, const std::string_view separator)
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
    m_off_t weightedSum = 0;
    m_off_t totalWeight = 0;
    for (size_t i = 0; i < values.size(); ++i)
    {
        weightedSum += values[i] * weights[i];
        totalWeight += weights[i];
    }
    if (weightedSum == 0 || totalWeight == 0)
    {
        return 0;
    }
    return static_cast<m_off_t>(
        std::round(static_cast<double>(weightedSum) / static_cast<double>(totalWeight)));
}


} // namespace stats

} // namespace mega
