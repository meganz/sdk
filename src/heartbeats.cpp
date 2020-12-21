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

double HeartBeatBackupInfo::progress() const
{
    return mProgress;
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
double HeartBeatTransferProgressedInfo::progress() const
{
    return mProgressInvalid ? -1.0 : std::max(0., std::min(1., static_cast<double>(mTransferredBytes) / static_cast<double>(mTotalBytes)));
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

void HeartBeatSyncInfo::updateStatus(SyncManager& sm)
{
    HeartBeatSyncInfo::Status status = HeartBeatSyncInfo::Status::INACTIVE;

    if (sm.mSync)
    {
        switch(sm.mSync->localroot->ts)
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

////////////// BackupInfo ////////////////

BackupInfo::BackupInfo(BackupType type, string backupName, string localFolder, handle megaHandle, int state, int substate, std::string extra)
    : mType(type)
    , mBackupName(backupName)
    , mLocalFolder(localFolder)
    , mMegaHandle(megaHandle)
    , mState(state)
    , mSubState(substate)
    , mExtra(extra)
{

}

BackupType BackupInfo::type() const
{
    return mType;
}

string BackupInfo::backupName() const
{
    return mBackupName;
}

string BackupInfo::localFolder() const
{
    return mLocalFolder;
}

handle BackupInfo::megaHandle() const
{
    return mMegaHandle;
}

int BackupInfo::state() const
{
    return mState;
}

int BackupInfo::subState() const
{
    return mSubState;
}

string BackupInfo::extra() const
{
    return mExtra;
}

#ifdef ENABLE_SYNC
BackupInfoSync::BackupInfoSync(SyncManager& syncManager)
    : BackupInfo(getSyncType(syncManager.mConfig),
                     syncManager.mConfig.getName(),
                     syncManager.mConfig.getLocalPath(),
                     syncManager.mConfig.getRemoteNode(),
                     getSyncState(syncManager),
                     getSyncSubstatus(syncManager),
                     getSyncExtraData(syncManager))
{
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


int BackupInfoSync::getSyncState(SyncManager& syncManager)
{
    SyncError error = syncManager.mConfig.getError();
    syncstate_t state = syncManager.mSync ? syncManager.mSync->state : SYNC_FAILED;

    if (state == SYNC_DISABLED && error != NO_SYNC_ERROR)
    {
        return State::TEMPORARY_DISABLED;
    }
    else if (state != SYNC_FAILED && state != SYNC_CANCELED && state != SYNC_DISABLED)
    {
        return calculatePauseActiveState(&syncManager.mClient);
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

BackupType BackupInfoSync::getSyncType(const SyncConfig& config)
{
    switch (config.getType())
    {
    case SyncConfig::Type::TYPE_UP:
            return BackupType::UP_SYNC;
    case SyncConfig::Type::TYPE_DOWN:
            return BackupType::DOWN_SYNC;
    case SyncConfig::Type::TYPE_TWOWAY:
            return BackupType::TWO_WAY;
    default:
            return BackupType::INVALID;
    }
}

int BackupInfoSync::getSyncSubstatus(SyncManager& sm)
{
    return sm.mConfig.getError();
}

string BackupInfoSync::getSyncExtraData(SyncManager&)
{
    return string();
}
#endif

////////////// MegaBackupMonitor ////////////////
BackupMonitor::BackupMonitor(MegaClient *client)
    : mClient(client)
{
}

void BackupMonitor::digestPutResult(handle backupId, SyncManager* syncPtr)
{
#ifdef ENABLE_SYNC
    mClient->syncs.forEachSyncManager([&](SyncManager& sm){
        if (&sm == syncPtr)
        {
            sm.mConfig.setBackupId(backupId);
            mClient->syncs.saveSyncConfig(sm.mConfig);
        }
    });
#endif
}

void BackupMonitor::updateBackupInfo(handle backupId, const BackupInfo &info)
{
    string localFolderEncrypted(mClient->cypherTLVTextWithMasterKey("lf", info.localFolder()) );
    string deviceIdHash = mClient->getDeviceidHash();

    mClient->reqs.add(new CommandBackupPut(mClient,
                                           info.type(),
                                           info.backupName(),
                                           info.megaHandle(),
                                           localFolderEncrypted.c_str(),
                                           deviceIdHash.c_str(),
                                           info.state(),
                                           info.subState(),
                                           info.extra().c_str(),
                                           nullptr));
}

#ifdef ENABLE_SYNC

void BackupMonitor::registerBackupInfo(const BackupInfo &info, SyncManager* syncPtr)
{
    string localFolderEncrypted(mClient->cypherTLVTextWithMasterKey("lf", info.localFolder()) );
    string deviceIdHash = mClient->getDeviceidHash();

    mClient->reqs.add(new CommandBackupPut(mClient, info.type(), info.backupName(), info.megaHandle(),
                                           localFolderEncrypted.c_str(),
                                           deviceIdHash.c_str(),
                                           info.state(), info.subState(), info.extra().c_str(),
                                           [this, syncPtr](Error e, handle h){ if (!e) digestPutResult(h, syncPtr); }));
}


void BackupMonitor::updateOrRegisterSync(SyncManager& syncManager)
{
    BackupInfoSync currentInfo(syncManager);

    if (!syncManager.mBackupInfo && syncManager.mConfig.getBackupId() == UNDEF) // not registered yet
    {
        syncManager.mBackupInfo = ::mega::make_unique<BackupInfoSync>(syncManager);
        registerBackupInfo(currentInfo, &syncManager);
    }
    else if (syncManager.mConfig.getBackupId() != UNDEF  &&
           (!syncManager.mBackupInfo || !(currentInfo == *syncManager.mBackupInfo)))
    {
        updateBackupInfo(syncManager.mConfig.getBackupId(), currentInfo); //queue update comand
        syncManager.mBackupInfo = ::mega::make_unique<BackupInfoSync>(syncManager);
    }
}

bool  BackupInfoSync::operator==(const BackupInfoSync& o) const
{
    return  mType == o.mType &&
            mLocalFolder == o.mLocalFolder &&
            mMegaHandle == o.mMegaHandle &&
            mState == o.mState &&
            mSubState == o.mSubState &&
            mExtra == o.mExtra;
}

void BackupMonitor::onSyncConfigChanged()
{
    mClient->syncs.forEachSyncManager([&](SyncManager& sm) {
        updateOrRegisterSync(sm);
    });
}

void BackupMonitor::calculateStatus(HeartBeatBackupInfo *hbs, SyncManager& sm)
{
   hbs->updateStatus(sm);
}

void BackupMonitor::beatBackupInfo(SyncManager& syncManager)
{
    // send registration or update in case we missed it
    updateOrRegisterSync(syncManager);

    if (!syncManager.mBackupInfo || syncManager.mConfig.getBackupId() == UNDEF)
    {
        LOG_warn << "Backup not registered yet. Skipping heartbeat...";
        return;
    }

    std::shared_ptr<HeartBeatSyncInfo> hbs = syncManager.mNextHeartbeat;

    if ( !hbs->mSending && (hbs->mModified
         || m_time(nullptr) - hbs->lastBeat() > MAX_HEARBEAT_SECS_DELAY))
    {
        calculateStatus(hbs.get(), syncManager); //we asume this is costly: only do it when beating

        hbs->setLastBeat(m_time(nullptr));

        int8_t progress = (hbs->progress() < 0) ? -1 : static_cast<int8_t>(std::lround(hbs->progress()*100.0));

        hbs->mSending = true;
        auto newCommand = new CommandBackupPutHeartBeat(mClient, syncManager.mConfig.getBackupId(),  static_cast<uint8_t>(hbs->status()),
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
    mClient->syncs.forEachSyncManager([&](SyncManager& sm){
        beatBackupInfo(sm);
    });
#endif
}

}
