/**
 * @file mega/transferstats.h
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

#pragma once

#include "mega/transfer.h"

namespace mega
{

namespace stats
{

/**
 * @brief A class to store and manage transfer statistics.
 * This class collects data for multiple transfers (uploads/downloads) and
 * provides methods to calculate and analyze metrics such as median size,
 * speed, latency, and failure ratios.
 */
class TransferStats
{
public:
    /**
     * @brief Structure that stores information about a single transfer.
     */
    struct TransferData
    {
        m_off_t mSize{}; // Size of the transfer (in bytes).
        m_off_t mSpeed{}; // Speed of the transfer (in KBytes per second).
        double mLatency{}; // Latency of the transfer (in milliseconds).
        double mFailedRequestRatio{}; // Number of failed requests during the transfer.
        bool mIsRaided{}; // Flag indicating if the transfer is raided (true) or not (false).
        std::chrono::steady_clock::time_point
            mTimestamp{}; // Timestamp indicating when the transfer was added.

        // Check data fields validity.
        bool checkDataStateValidity() const;
    };

    /**
     * @brief Structure that holds aggregated metrics for a set of transfers.
     * Metrics include statistical values like median and weighted average
     * size/speed, maximum speed, average latency, failed request ratio, and
     * raided transfer ratio.
     */
    struct Metrics
    {
        direction_t mTransferType{}; // Type of transfer (upload or download).
        m_off_t mMedianSize{}; // Median size of transfers in the set (in bytes).
        m_off_t mContraharmonicMeanSize{}; // Contraharmonic mean (sizes weighted by size) of the
                                           // transfers (in bytes).
        m_off_t mMedianSpeed{}; // Median speed of the transfers (in KBytes per second).
        m_off_t mWeightedAverageSpeed{}; // Weighted average speed of the transfers (in KBytes per
                                         // second).
        m_off_t
            mMaxSpeed{}; // Maximum speed observed in the set of transfers (in KBytes per second).
        m_off_t mAvgLatency{}; // Average latency of the transfers (in milliseconds).
        double
            mFailedRequestRatio{}; // Ratio of failed requests to total requests, between 0 and 1.
        double mRaidedTransferRatio{}; // Ratio of raided transfers in the set, between 0 and 1.
        size_t mNumTransfers{}; // Number of transfers used to calculate these metrics. Informative,
                                // not used for stats analysis.

        // Create a string to be used for debug and analyze contents.
        // The separator is meant for different lines with metrics data.
        std::string toString(const std::string& separator = "\n") const;

        // Create a string in JSON format with keys and values.
        std::string toJson() const;
    };

    /**
     * @brief Constructs a new TransferStats object.
     *
     * @param mMaxEntries Maximum number of transfer entries to store.
     * @param mMaxAgeSeconds Maximum age (in seconds) before a transfer entry is discarded.
     */
    TransferStats(const size_t maxEntries, const int64_t maxAgeSeconds):
        mMaxEntries(maxEntries),
        mMaxAgeSeconds(maxAgeSeconds)
    {}

    /**
     * @brief Adds a new transfer to the statistics collection.
     *
     * Removes the oldest transfers if the collection exceeds the maximum entries or age.
     *
     * Size, speed and latency must be positive values.
     *
     * There can be a speed 0 value for uploads whose nodes are cloned
     * due to the same file existing in the cloud (i.e., there is no real transfer).
     * Similarly, there can be a latency 0 if all the connections were reused.
     * None of them (transfers with speed 0 or latency 0) can be taken into account,
     * and they are discarded (not added) for transfer stats.
     *
     * These are the TransferData parameter fields that must be included.
     *
     * @param TransferData::mSize Size of the transfer (in bytes).
     * @param TransferData::mSpeed Speed of the transfer (in bytes per second).
     * @param TransferData::mLatency Latency of the transfer (in milliseconds).
     * @param TransferData::mFailedRequestRatio Number of failed requests
     *     divided by the number of total requests.
     * @param TransferData::mIsRaided Boolean indicating whether the transfer was raided.
     *
     * The TransferData::timestamp field will be assigned during the operation.
     *
     * @return true if the params are valid as stated above, false otherwise.
     */
    bool addTransferData(TransferData&& transferData);

    /**
     * @brief Collects metrics from the stored transfer data.
     *
     * @param type The type of transfer (upload or download).
     * @return Metrics A structure containing the aggregated metrics.
     */
    Metrics collectMetrics(const direction_t type) const;

    /**
     * @brief Check the number of transfers included in the collection.
     *
     * @return The current number of transfers included.
     */
    size_t size() const
    {
        return mTransfersData.size();
    }

    /**
     * @brief Check the maximum number of transfers to store.
     *
     * @return The maximum number of transfers to store.
     */
    size_t getMaxEntries() const
    {
        return mMaxEntries;
    }

    /**
     * @brief Check the max age of a transfer before it's removed (in seconds).
     *
     * Transfers above this age will only be removed when adding new transfers
     * to the collection.
     *
     * @return The max number of seconds for a transfer to be kept in the collection.
     */
    int64_t getMaxAgeSeconds() const
    {
        return mMaxAgeSeconds;
    }

private:
    std::deque<TransferData> mTransfersData; // Stores the recent transfer data.
    size_t mMaxEntries; // Maximum number of transfers to store.
    int64_t mMaxAgeSeconds; // Maximum age of a transfer before it's removed (in seconds).
};

/**
 * @brief TransferStatsManager handles separate statistics for uploads and downloads.
 *
 * It manages two instances of TransferStats—one for upload transfers and one for download
 * transfers.
 */
class TransferStatsManager
{
public:
    /**
     * @brief Constructs a new TransferStatsManager object.
     *
     * Initializes statistics collections for uploads and downloads.
     *
     * @param maxEntries Maximum number of entries for uploads and downloads.
     * @param maxAgeSeconds Maximum age of transfers before being discarded.
     */
    TransferStatsManager(const size_t maxEntries = MAX_ENTRIES,
                         const int64_t maxAgeSeconds = MAX_AGE_SECONDS):
        mUploadStatistics(maxEntries, maxAgeSeconds),
        mDownloadStatistics(maxEntries, maxAgeSeconds)
    {}

    /**
     * @brief Adds a transfer to the appropriate statistics collection (upload or download).
     *
     * @param transfer Pointer to a Transfer object
     * @return true if the stats have been added, false if the transfer or slot are not valid.
     */
    bool addTransferStats(const Transfer* const transfer);

    /**
     * @brief Create a string in JSON format for the type of transfer.
     *
     * @param type Specify whether to compress upload or download metrics.
     * @return A string containing the metrics in JSON format.
     */
    std::string metricsToJsonForTransferType(const direction_t type) const;

    /**
     * @brief Collect and calculate metrics for either uploads or downloads.
     *
     * @param type Specify whether to collect upload or download metrics.
     * @return Metrics structure containing calculated statistics.
     */
    TransferStats::Metrics collectMetrics(const direction_t type) const;

    /**
     * @brief Collect and print the metrics for either uploads or downloads.
     *
     * @param type Specify whether to print upload or download metrics.
     * @param separator for each line of metrics values.
     */
    TransferStats::Metrics collectAndPrintMetrics(const direction_t type,
                                                  const std::string& separator = "\n") const;

    /**
     * @brief Check the number of transfers included in the collection.
     *
     * @param type PUT for uploads, GET for downloads.
     * @return The current number of transfers included.
     */
    size_t size(const direction_t type) const;

    /**
     * @brief Check the maximum number of transfers to store.
     *
     * @param type PUT for uploads, GET for downloads.
     * @return The maximum number of transfers to store.
     */
    size_t getMaxEntries(const direction_t type) const;

    /**
     * @brief Check the max age of a transfer before it's removed (in seconds).
     *
     * Transfers above this age will only be removed when adding new transfers
     * to the collection.
     *
     * @param type PUT for uploads, GET for downloads.
     * @return The max number of seconds for a transfer to be kept in the collection.
     */
    int64_t getMaxAgeSeconds(const direction_t type) const;

private:
    static constexpr size_t MAX_ENTRIES =
        50; // Default maximum number of entries in each stats collection.
    static constexpr int64_t MAX_AGE_SECONDS =
        3600; // Default maximum age (in seconds) before transfers are removed.

    mutable std::mutex mTransferStatsMutex;
    TransferStats mUploadStatistics; // Transfer statistics for uploads.
    TransferStats mDownloadStatistics; // Transfer statistics for downloads.
};

// UTILS
/**
 * @brief LOG the collected metrics.
 *
 * @param metrics The metrics to log.
 * @param separator Separator for each line of metrics values.
 */
void printMetrics(const TransferStats::Metrics& metrics, const std::string& separator = "\n");

/**
 * @brief Calculates the median of a vector of sorted values.
 *
 * @param sortedValues Vector of sorted values for which the median needs to be calculated.
 * @return m_off_t The median value.
 */
m_off_t calculateMedian(const std::vector<m_off_t>& sortedValues);

/**
 * @brief Calculates the weighted average of a vector of values.
 *
 * @param values Vector of values to average.
 * @param weights Corresponding vector of weights for each value.
 * @return m_off_t The weighted average value in bytes.
 */
m_off_t calculateWeightedAverage(const std::vector<m_off_t>& values,
                                 const std::vector<m_off_t>& weights);

/**
 * @brief Check/assert valid transfer type.
 *
 * Only PUT (uploads) and GET (downloads) are allowed.
 */
void checkTransferTypeValidity(const direction_t type);

/**
 * @brief Checks transfer state validity to add transfer stats.
 *
 * Error conditions are logged and asserts are triggered.
 *
 * The transfer object must be valid.
 * There must be a valid TransferSlot.
 * TempURLs must be defined and raidKnown (whether or not is raid) defined.
 *
 * @param transfer The transfer object to get the stats from.
 *
 * @returm true if the transfer state is valid and stats can be retrieved from it, false otherwise.
 */
bool checkTransferStateValidity(const Transfer* const transfer);

} // namespace stats

} // namespace mega
