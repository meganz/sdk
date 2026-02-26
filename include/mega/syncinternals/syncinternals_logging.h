/**
 * @file syncinternals_logging.h
 * @brief Class for internal logging operations of the sync engine.
 */

#ifndef MEGA_SYNCINTERNALS_LOGGING_H
#define MEGA_SYNCINTERNALS_LOGGING H 1

#ifdef ENABLE_SYNC

#include "mega/logging.h"

#include <chrono>

namespace mega
{

// Constants
using namespace std::chrono_literals;
const std::chrono::milliseconds MIN_DELAY_BETWEEN_SYNC_VERBOSE_TIMED{20s};
const std::chrono::milliseconds TIME_WINDOW_FOR_SYNC_VERBOSE_TIMED{1s};

// Macros
#define SYNC_verbose \
    if (syncs.mDetailedSyncLogging) \
    LOG_verbose
#define SYNC_verbose_timed \
    if (syncs.mDetailedSyncLogging) \
    SYNCS_verbose_timed
#define SYNCS_verbose_timed \
    LOG_verbose_timed(MIN_DELAY_BETWEEN_SYNC_VERBOSE_TIMED, TIME_WINDOW_FOR_SYNC_VERBOSE_TIMED)

} // namespace mega

#endif // ENABLE_SYNC
#endif // MEGA_SYNCINTERNALS_LOGGING_H
