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
#include "mega/command.h"
#include "assert.h"
#include "mega.h"

namespace mega {

#ifdef ENABLE_SYNC

HeartBeatBackupInfo::HeartBeatBackupInfo()
{
}

double HeartBeatBackupInfo::progress(m_off_t inflightProgress) const
{
    return mProgress + static_cast<double>(inflightProgress);
}

void HeartBeatBackupInfo::invalidateProgress()
{
    mProgressInvalid = true;
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

void HeartBeatBackupInfo::setSPState(SPState state)
{
    if (mSPState != state)
    {
        mSPState = state;
        updateLastActionTime();
    }
}

void HeartBeatBackupInfo::setProgress(const double &progress)
{
    mProgressInvalid = false;
    mProgress = progress;
    updateLastActionTime();
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

////////// HeartBeatTransferProgressedInfo ////////
double HeartBeatTransferProgressedInfo::progress(m_off_t inflightProgress) const
{
    return mProgressInvalid ? -1.0 : std::max(0., std::min(1., static_cast<double>((mTransferredBytes + inflightProgress)) / static_cast<double>(mTotalBytes)));
}

void HeartBeatTransferProgressedInfo::adjustTransferCounts(int32_t upcount, int32_t downcount, long long totalBytes, long long transferBytes)
{
    if (mProgressInvalid &&
        !mPendingUps && !mPendingDowns &&
        mTotalBytes == mTransferredBytes)
    {
        mProgressInvalid = false;
        mPendingUps = 0;
        mPendingDowns = 0;
        mTotalBytes = 0;
        mTransferredBytes = 0;
    }

    mPendingUps += upcount;
    mPendingDowns += downcount;
    mTotalBytes += totalBytes;
    mTransferredBytes += transferBytes;
    updateLastActionTime();
}

void HeartBeatSyncInfo::updateSPHBStatus(UnifiedSync& us)
{
    SPHBStatus status = CommandBackupPutHeartBeat::INACTIVE;

    if (us.mSync)
    {
        switch(us.mSync->localroot->ts)
        {
        case TREESTATE_SYNCED:
            status = CommandBackupPutHeartBeat::UPTODATE;
            break;
        case TREESTATE_PENDING:
            status = CommandBackupPutHeartBeat::PENDING;
            break;
        case TREESTATE_SYNCING:
            status = CommandBackupPutHeartBeat::SYNCING;
            break;
        default:
            status = CommandBackupPutHeartBeat::UNKNOWN;
            break;
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
    nodeHandle = config.getRemoteNode();
    localFolder = config.getLocalPath();
    state = calculatedState;
    subState = config.getError();
    deviceId = device;
    driveId = drive;
}

BackupInfoSync::BackupInfoSync(const UnifiedSync &us, bool pauseDown, bool pauseUp)
{
    backupId = us.mConfig.mBackupId;
    type = getSyncType(us.mConfig);
    backupName = us.mConfig.mName,
    nodeHandle = us.mConfig.getRemoteNode();
    localFolder = us.mConfig.getLocalPath();
    state = BackupInfoSync::getSyncState(us, pauseDown, pauseUp);
    subState = us.mConfig.getError();
    deviceId = us.syncs.mClient.getDeviceidHash();
    driveId = BackupInfoSync::getDriveId(us);
    assert(!(us.mConfig.isBackup() && us.mConfig.isExternal())  // not an external backup...
           || !ISUNDEF(driveId));  // ... or it must have a valid drive-id
}

HeartBeatBackupInfo::SPState BackupInfoSync::calculatePauseActiveState(bool pauseDown, bool pauseUp)
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

HeartBeatBackupInfo::SPState BackupInfoSync::getSyncState(const UnifiedSync& us, bool pauseDown, bool pauseUp)
{
    SyncError error = us.mConfig.getError();
    syncstate_t state = us.mSync ? us.mSync->state() : SYNC_FAILED;

    return getSyncState(error, state, pauseDown, pauseUp);
}

HeartBeatBackupInfo::SPState BackupInfoSync::getSyncState(SyncError error, syncstate_t state, bool pauseDown, bool pauseUp)
{
    if (state == SYNC_DISABLED && error != NO_SYNC_ERROR)
    {
        return CommandBackupPut::TEMPORARY_DISABLED;
    }
    else if (state != SYNC_FAILED && state != SYNC_CANCELED && state != SYNC_DISABLED)
    {
        return calculatePauseActiveState(pauseDown, pauseUp);
    }
    else if (!(state != SYNC_CANCELED && (state != SYNC_DISABLED || error != NO_SYNC_ERROR)))
    {
        return CommandBackupPut::DISABLED;
    }
    else
    {
        return CommandBackupPut::FAILED;
    }
}

HeartBeatBackupInfo::SPState BackupInfoSync::getSyncState(const SyncConfig& config, bool pauseDown, bool pauseUp)
{
    auto error = config.getError();
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
    const LocalPath& drivePath = us.mConfig.mExternalDrivePath;
    const auto& fsAccess = *us.mClient.fsaccess;
    const string& drivePathUtf8 = drivePath.toPath(fsAccess);
    handle driveId;
    us.mClient.readDriveId(drivePathUtf8.c_str(), driveId); // It shouldn't happen very often

    return driveId;
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
    : syncs(s), mClient(&syncs.mClient)
{
}

void BackupMonitor::updateOrRegisterSync(UnifiedSync& us)
{
#ifdef DEBUG
    handle backupId = us.mConfig.getBackupId();
    assert(!ISUNDEF(backupId)); // syncs are registered before adding them
#endif

    auto currentInfo = BackupInfoSync(us, syncs.mDownloadsPaused, syncs.mUploadsPaused);
    if (!us.mBackupInfo || currentInfo != *us.mBackupInfo)
    {
        // Send if anything changed, or it's the first time we're considering for this sync (gets around Backup Centre continuing to show Disabled even though we sent sphb)
        syncs.mClient.reqs.add(new CommandBackupPut(&syncs.mClient, currentInfo, nullptr));
    }
    us.mBackupInfo = ::mega::make_unique<BackupInfoSync>(currentInfo);
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
    // send registration or update in case we missed it
    updateOrRegisterSync(us);

    if (ISUNDEF(us.mConfig.getBackupId()))
    {
        LOG_warn << "Backup not registered yet. Skipping heartbeat...";
        return;
    }

    std::shared_ptr<HeartBeatSyncInfo> hbs = us.mNextHeartbeat;

    if ( !hbs->mSending && (hbs->mModified
         || m_time(nullptr) - hbs->lastBeat() > MAX_HEARBEAT_SECS_DELAY))
    {
        hbs->updateSPHBStatus(us);
        hbs->setLastBeat(m_time(nullptr));

        m_off_t inflightProgress = 0;
        if (us.mSync)
        {
            inflightProgress = us.mSync->getInflightProgress();
        }

        int8_t progress = (hbs->progress(inflightProgress) < 0) ? -1 : static_cast<int8_t>(std::lround(hbs->progress(inflightProgress)*100.0));

        hbs->mSending = true;
        auto newCommand = new CommandBackupPutHeartBeat(mClient, us.mConfig.getBackupId(),  hbs->sphbStatus(),
                          progress, hbs->mPendingUps, hbs->mPendingDowns,
                          hbs->lastAction(), hbs->lastItemUpdated(),
                          [hbs](Error){
                               hbs->mSending = false;
                          });

#ifdef ENABLE_SYNC
        if (hbs->sphbStatus() == CommandBackupPutHeartBeat::UPTODATE && progress >= 100)
        {
            hbs->invalidateProgress(); // we invalidate progress, so as not to keep on reporting 100% progress after reached up to date
            // note: new transfer updates will modify the progress and make it valid again
        }
#endif

        mClient->reqs.add(newCommand);
    }
}

void BackupMonitor::beat()
{
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
