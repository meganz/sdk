/**
 * @file heartbeats.cpp
 * @brief Classes for heartbeating Sync configuration and status
 *
 * (c) 2013 by Mega Limited, Auckland, New Zealand
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

#include "mega/heartbeats.h"

#include "assert.h"
#include "mega.h"
#include "mega/command.h"
#include "mega/testhooks.h"

namespace mega {

#ifdef ENABLE_SYNC

static constexpr int FREQUENCY_HEARTBEAT_DS = 300;

HeartBeatBackupInfo::HeartBeatBackupInfo()
{
}

m_time_t HeartBeatBackupInfo::lastAction() const
{
    return mLastAction;
}

handle HeartBeatBackupInfo::lastItemUpdated() const
{
    return mLastItemUpdated;
}

void HeartBeatBackupInfo::setLastSyncedItem(const handle &lastSyncedItem)
{
    if (mLastItemUpdated != lastSyncedItem)
    {
        mLastItemUpdated = lastSyncedItem;
        updateLastActionTime();
    }
}

void HeartBeatBackupInfo::setLastAction(const m_time_t &lastAction)
{
    mLastAction = lastAction;
}

void HeartBeatBackupInfo::updateLastActionTime()
{
    setLastAction(m_time(nullptr));
    mModified = true;
}

void HeartBeatBackupInfo::setLastBeat(const m_time_t &lastBeat)
{
    mLastBeat = lastBeat;
    mModified = false;
}

m_time_t HeartBeatBackupInfo::lastBeat() const
{
    return mLastBeat;
}

void HeartBeatSyncInfo::updateSPHBStatus(UnifiedSync& us)
{
    SPHBStatus status = CommandBackupPutHeartBeat::INACTIVE;

    if (us.mSync)
    {
        if (!us.mConfig.mError)
        {
            if (us.syncs.isSyncStalled(us.mConfig.mBackupId) ||
                us.mSync->localroot->conflicts != TREE_RESOLVED)
            {
                status = CommandBackupPutHeartBeat::STALLED;
            }
            else if (!us.mConfig.mFinishedInitialScanning)
            {
                // only consider it "scanning" until it first completes scanning.  Later scanning (even though we do it) interferes with the % display in Backup Centre
                status = CommandBackupPutHeartBeat::PENDING; // = scanning
            }
            else if (us.mSync->localroot->mightHaveMoves() ||
                     us.mSync->localroot->syncRequired())
            {
                status = CommandBackupPutHeartBeat::SYNCING;
            }
            else
            {
                status = CommandBackupPutHeartBeat::UPTODATE;
            }
        }
    }

    if (mSPHBStatus != status)
    {
        mSPHBStatus = status;
        updateLastActionTime();
    }
}

BackupInfoSync::BackupInfoSync(const SyncConfig& config, const string& device, handle drive, CommandBackupPut::SPState calculatedState)
{
    backupId = config.mBackupId;
    type = getSyncType(config);
    backupName = config.mName,
    nodeHandle = config.mRemoteNode;
    localFolder = config.getLocalPath();
    state = calculatedState;
    subState = config.mError;
    deviceId = device;
    driveId = drive;
}

BackupInfoSync::BackupInfoSync(const UnifiedSync &us, bool pauseDown, bool pauseUp)
{
    backupId = us.mConfig.mBackupId;
    type = getSyncType(us.mConfig);
    backupName = us.mConfig.mName,
    nodeHandle = us.mConfig.mRemoteNode;
    localFolder = us.mConfig.getLocalPath();
    state = BackupInfoSync::getSyncState(us, pauseDown, pauseUp);
    subState = us.mConfig.mError;
    deviceId = us.syncs.mClient.getDeviceidHash();
    driveId = BackupInfoSync::getDriveId(us);
    assert(!(us.mConfig.isBackup() && us.mConfig.isExternal())  // not an external backup...
           || !ISUNDEF(driveId));  // ... or it must have a valid drive-id
}

CommandBackupPut::SPState BackupInfoSync::calculatePauseActiveState(bool pauseDown, bool pauseUp)
{
    if (pauseDown && pauseUp)
    {
        return CommandBackupPut::PAUSE_FULL;
    }
    else if (pauseDown)
    {
        return CommandBackupPut::PAUSE_DOWN;
    }
    else if (pauseUp)
    {
        return CommandBackupPut::PAUSE_UP;
    }

    return CommandBackupPut::ACTIVE;
}

CommandBackupPut::SPState BackupInfoSync::getSyncState(const UnifiedSync& us, bool pauseDown, bool pauseUp)
{
    return getSyncState(us.mConfig.mError,
                        us.mConfig.mRunState,
                        pauseDown, pauseUp);
}

CommandBackupPut::SPState BackupInfoSync::getSyncState(SyncError error, SyncRunState s, bool pauseDown, bool pauseUp)
{
    switch (s)
    {
        case SyncRunState::Pending:
        case SyncRunState::Loading:
        case SyncRunState::Run:
            return calculatePauseActiveState(pauseDown, pauseUp);

        case SyncRunState::Pause:
            return CommandBackupPut::TEMPORARY_DISABLED;

        case SyncRunState::Suspend:
            return error > NO_SYNC_ERROR ? CommandBackupPut::FAILED : CommandBackupPut::TEMPORARY_DISABLED;

        case SyncRunState::Disable:
            return error > NO_SYNC_ERROR ? CommandBackupPut::FAILED : CommandBackupPut::DISABLED;
    }

    return CommandBackupPut::DISABLED;
}

CommandBackupPut::SPState BackupInfoSync::getSyncState(const SyncConfig& config, bool pauseDown, bool pauseUp)
{
    auto error = config.mError;
    if (!error)
    {
        if (config.getEnabled())
        {
            return calculatePauseActiveState(pauseDown, pauseUp);
        }
        else
        {
            return CommandBackupPut::DISABLED;
        }
    }
    else //error
    {
        if (config.getEnabled())
        {
            return CommandBackupPut::TEMPORARY_DISABLED;
        }
        else
        {
            return CommandBackupPut::DISABLED;
        }
    }
}

handle BackupInfoSync::getDriveId(const UnifiedSync &us)
{
    const auto& drivePath = us.mConfig.mExternalDrivePath;

    // Only external drives have drive IDs.
    if (drivePath.empty())
        return UNDEF;

    // Get our hands on the config store.
    const auto store = us.syncs.syncConfigStore();

    // It should always be available.
    assert(store);

    // Drive should be known.
    assert(store->driveKnown(drivePath));

    // Ask the store for the drive's backup ID.
    auto id = store->driveID(drivePath);

    // It should never be undefined.
    assert(id != UNDEF);

    return id;
}

BackupType BackupInfoSync::getSyncType(const SyncConfig& config)
{
    switch (config.getType())
    {
    case SyncConfig::TYPE_UP:
            return BackupType::UP_SYNC;
    case SyncConfig::TYPE_DOWN:
            return BackupType::DOWN_SYNC;
    case SyncConfig::TYPE_TWOWAY:
            return BackupType::TWO_WAY;
    case SyncConfig::TYPE_BACKUP:
            return BackupType::BACKUP_UPLOAD;
    default:
            return BackupType::INVALID;
    }
}

BackupMonitor::BackupMonitor(Syncs& s)
    : syncs(s)
{
}

void BackupMonitor::updateOrRegisterSync(UnifiedSync& us)
{
    assert(syncs.onSyncThread());

    if (us.mConfig.mSyncDeregisterSent) return;

#ifndef NDEBUG
    handle backupId = us.mConfig.mBackupId;
    assert(!ISUNDEF(backupId)); // syncs are registered before adding them
#endif

    auto currentInfo = BackupInfoSync(us, syncs.mDownloadsPaused, syncs.mUploadsPaused);
    if (!us.mBackupInfo || currentInfo != *us.mBackupInfo)
    {
        syncs.queueClient(
            [currentInfo](MegaClient& mc, DBTableTransactionCommitter&)
            {
                mc.reqs.add(new CommandBackupPut(&mc, currentInfo, nullptr));
            });
    }
    us.mBackupInfo = std::make_unique<BackupInfoSync>(currentInfo);
}

bool BackupInfoSync::operator==(const BackupInfoSync& o) const
{
    return  backupId == o.backupId &&
            driveId == o.driveId &&
            type == o.type &&
            backupName == o.backupName &&
            nodeHandle == o.nodeHandle &&
            localFolder == o.localFolder &&
            deviceId == o.deviceId &&
            state == o.state &&
            subState == o.subState;
}

bool BackupInfoSync::operator!=(const BackupInfoSync &o) const
{
    return !(*this == o);
}

void BackupMonitor::beatBackupInfo(UnifiedSync& us)
{
    assert(syncs.onSyncThread());

    if (us.mConfig.mSyncDeregisterSent) return;

    // send registration or update in case we missed it
    updateOrRegisterSync(us);

    if (ISUNDEF(us.mConfig.mBackupId))
    {
        LOG_warn << "Backup not registered yet. Skipping heartbeat...";
        return;
    }

    std::shared_ptr<HeartBeatSyncInfo> hbs = us.mNextHeartbeat;

    if (us.mSync)
    {
        auto counts = us.mSync->threadSafeState->transferCounts();

        if (hbs->mSnapshotTransferCounts != counts)
        {
            hbs->mSnapshotTransferCounts = counts;
            hbs->updateLastActionTime();
        }
    }

    hbs->updateSPHBStatus(us);

    auto elapsedSec = m_time(nullptr) - hbs->lastBeat();

    if ( !hbs->mSending &&
         (elapsedSec >= MAX_HEARBEAT_SECS_DELAY ||
         (elapsedSec*10 >= FREQUENCY_HEARTBEAT_DS && hbs->mModified)))
    {

        hbs->setLastBeat(m_time(nullptr));

        m_off_t inflightProgress = 0;
        if (us.mSync)
        {
            // to be figured out for sync rework
            //inflightProgress = us.mSync->getInflightProgress();
        }

        auto reportCounts = hbs->mSnapshotTransferCounts;
        reportCounts -= hbs->mResolvedTransferCounts;

        auto progress = reportCounts.progress(inflightProgress);
        DEBUG_TEST_HOOK_ON_TRANSFER_REPORT_PROGRESS(progress,
                                                    inflightProgress,
                                                    reportCounts.pendingTransferBytes());
        if (progress > 1.0)
        {
            const std::string errMsg =
                "BackupMonitor::beatBackupInfo: Invalid reportCounts progress value";
            LOG_err << errMsg;
            assert(false && errMsg.c_str());
            progress = static_cast<uint8_t>(100.0 * progress);
        }

        hbs->mSending = true;

        auto backupId = us.mConfig.mBackupId;
        auto status = hbs->sphbStatus();
        auto pendingUps = static_cast<uint32_t>(reportCounts.mUploads.mPending);
        auto pendingDowns = static_cast<uint32_t>(reportCounts.mUploads.mPending);
        auto lastAction = hbs->lastAction();
        auto lastItemUpdated = hbs->lastItemUpdated();

        syncs.queueClient(
            [=](MegaClient& mc, DBTableTransactionCommitter&)
            {
                mc.reqs.add(new CommandBackupPutHeartBeat(&mc,
                                                          backupId,
                                                          status,
                                                          static_cast<int8_t>(progress),
                                                          pendingUps,
                                                          pendingDowns,
                                                          lastAction,
                                                          lastItemUpdated,
                                                          [hbs](Error)
                                                          {
                                                              hbs->mSending = false;
                                                          }));
            });

        if (progress >= 100)
        {
            // once we reach 100%, start counting again from 0 for any later sync activity.
            hbs->mResolvedTransferCounts = hbs->mSnapshotTransferCounts;
            // Clean pending values from mResolvedTransferCounts, as values corresponding to pending
            // transfers (uploads and downloads) are constantly being updated, with larger or
            // smaller values depending on the current state, and new transfers being added and
            // other ones finishing, so subtracting any previously saved value is wrong and leading
            // to an overflow when the saved values are greater.
            hbs->mResolvedTransferCounts.clearPendingValues();
        }
    }
}

void BackupMonitor::beat()
{
    assert(syncs.onSyncThread());

    // Only send heartbeats for enabled active syncs.
    for (auto& us : syncs.mSyncVec)
    {
        if (us->mSync && us->mConfig.getEnabled())
        {
            beatBackupInfo(*us);
        }
    };
}

#endif

}
