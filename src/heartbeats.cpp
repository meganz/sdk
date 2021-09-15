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

HeartBeatBackupInfo::HeartBeatBackupInfo()
{
}

int HeartBeatBackupInfo::status() const
{
    return mStatus;
}

double HeartBeatBackupInfo::progress(m_off_t inflightProgress) const
{
    return mProgress + inflightProgress;
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

void HeartBeatBackupInfo::setStatus(const int &status)
{
    if (mStatus != status)
    {
        mStatus = status;
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

#ifdef ENABLE_SYNC
////////////// HeartBeatSyncInfo ////////////////
HeartBeatSyncInfo::HeartBeatSyncInfo()
{
    mStatus = HeartBeatSyncInfo::Status::UNKNOWN;
}

void HeartBeatSyncInfo::updateStatus(UnifiedSync& us)
{
    HeartBeatSyncInfo::Status status = HeartBeatSyncInfo::Status::INACTIVE;

    if (us.mSync)
    {
        switch(us.mSync->localroot->ts)
        {
        case TREESTATE_SYNCED:
            status = HeartBeatSyncInfo::Status::UPTODATE;
            break;
        case TREESTATE_PENDING:
            status = HeartBeatSyncInfo::Status::PENDING;
            break;
        case TREESTATE_SYNCING:
            status = HeartBeatSyncInfo::Status::SYNCING;
            break;
        default:
            status = HeartBeatSyncInfo::Status::UNKNOWN;
            break;
        }
    }

    setStatus(status);
}

#endif


#ifdef ENABLE_SYNC
BackupInfoSync::BackupInfoSync(const SyncConfig& config, const string& device, handle drive, int calculatedState)
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

BackupInfoSync::BackupInfoSync(const UnifiedSync &us)
{
    backupId = us.mConfig.mBackupId;
    type = getSyncType(us.mConfig);
    backupName = us.mConfig.mName,
    nodeHandle = us.mConfig.getRemoteNode();
    localFolder = us.mConfig.getLocalPath();
    state = BackupInfoSync::getSyncState(us);
    subState = us.mConfig.getError();
    deviceId = us.mClient.getDeviceidHash();
    driveId = BackupInfoSync::getDriveId(us);
    assert(!(us.mConfig.isBackup() && us.mConfig.isExternal())  // not an external backup...
           || !ISUNDEF(driveId));  // ... or it must have a valid drive-id
}

int BackupInfoSync::calculatePauseActiveState(MegaClient *client)
{
    auto pauseDown = client->xferpaused[GET];
    auto pauseUp = client->xferpaused[PUT];
    if (pauseDown && pauseUp)
    {
        return State::PAUSE_FULL;
    }
    else if (pauseDown)
    {
        return State::PAUSE_DOWN;
    }
    else if (pauseUp)
    {
        return State::PAUSE_UP;
    }

    return State::ACTIVE;
}


int BackupInfoSync::getSyncState(const UnifiedSync& us)
{
    SyncError error = us.mConfig.getError();
    syncstate_t state = us.mSync ? us.mSync->state() : SYNC_FAILED;

    return getSyncState(error, state, &us.mClient);
}

int BackupInfoSync::getSyncState(SyncError error, syncstate_t state, MegaClient *client)
{
    if (state == SYNC_DISABLED && error != NO_SYNC_ERROR)
    {
        return State::TEMPORARY_DISABLED;
    }
    else if (state != SYNC_FAILED && state != SYNC_CANCELED && state != SYNC_DISABLED)
    {
        return calculatePauseActiveState(client);
    }
    else if (!(state != SYNC_CANCELED && (state != SYNC_DISABLED || error != NO_SYNC_ERROR)))
    {
        return State::DISABLED;
    }
    else
    {
        return State::FAILED;
    }
}

int BackupInfoSync::getSyncState(const SyncConfig& config, MegaClient *client)
{
    auto error = config.getError();
    if (!error)
    {
        if (config.getEnabled())
        {
            return calculatePauseActiveState(client);
        }
        else
        {
            return State::DISABLED;
        }
    }
    else //error
    {
        if (config.getEnabled())
        {
            return State::TEMPORARY_DISABLED;
        }
        else
        {
            return State::DISABLED;
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


#endif

////////////// MegaBackupMonitor ////////////////
BackupMonitor::BackupMonitor(MegaClient *client)
    : mClient(client)
{
}

#ifdef ENABLE_SYNC

void BackupMonitor::updateOrRegisterSync(UnifiedSync& us)
{
#ifdef DEBUG
    handle backupId = us.mConfig.getBackupId();
    assert(!ISUNDEF(backupId)); // syncs are registered before adding them
#endif

    auto currentInfo = ::mega::make_unique<BackupInfoSync>(us);
    if (us.mBackupInfo && *currentInfo != *us.mBackupInfo)
    {
        mClient->reqs.add(new CommandBackupPut(mClient, *currentInfo, nullptr));
    }
    us.mBackupInfo = move(currentInfo);
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

void BackupMonitor::onSyncConfigChanged()
{
    mClient->syncs.forEachUnifiedSync([&](UnifiedSync& us) {
        updateOrRegisterSync(us);
    });
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
        hbs->updateStatus(us);  //we asume this is costly: only do it when beating
        hbs->setLastBeat(m_time(nullptr));

        m_off_t inflightProgress = 0;
        if (us.mSync)
        {
            inflightProgress = us.mSync->getInflightProgress();
        }

        int8_t progress = (hbs->progress(inflightProgress) < 0) ? -1 : static_cast<int8_t>(std::lround(hbs->progress(inflightProgress)*100.0));

        hbs->mSending = true;
        auto newCommand = new CommandBackupPutHeartBeat(mClient, us.mConfig.getBackupId(),  static_cast<uint8_t>(hbs->status()),
                          progress, hbs->mPendingUps, hbs->mPendingDowns,
                          hbs->lastAction(), hbs->lastItemUpdated(),
                          [hbs](Error){
                               hbs->mSending = false;
                          });

#ifdef ENABLE_SYNC
        if (hbs->status() == HeartBeatSyncInfo::Status::UPTODATE && progress >= 100)
        {
            hbs->invalidateProgress(); // we invalidate progress, so as not to keep on reporting 100% progress after reached up to date
            // note: new transfer updates will modify the progress and make it valid again
        }
#endif

        mClient->reqs.add(newCommand);
    }
}

#endif

void BackupMonitor::beat()
{
#ifdef ENABLE_SYNC
    // Only send heartbeats for enabled active syncs.
    mClient->syncs.forEachUnifiedSync([&](UnifiedSync& us){
        if (us.mSync && us.mConfig.getEnabled())
        {
            beatBackupInfo(us);
        }
    });
#endif
}

}
