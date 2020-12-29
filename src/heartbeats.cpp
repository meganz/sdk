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
BackupInfoSync::BackupInfoSync(UnifiedSync& us)
    : BackupInfo(getSyncType(us.mConfig),
                     us.mConfig.getName(),
                     us.mConfig.getLocalPath(),
                     us.mConfig.getRemoteNode(),
                     getSyncState(us),
                     getSyncSubstatus(us),
                     getSyncExtraData(us))
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


int BackupInfoSync::getSyncState(UnifiedSync& us)
{
    SyncError error = us.mConfig.getError();
    syncstate_t state = us.mSync ? us.mSync->state : SYNC_FAILED;

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
    case SyncConfig::TYPE_BACKUP:
            return BackupType::BACKUP_UPLOAD;
    default:
            return BackupType::INVALID;
    }
}

int BackupInfoSync::getSyncSubstatus(UnifiedSync& us)
{
    return us.mConfig.getError();
}

string BackupInfoSync::getSyncExtraData(UnifiedSync&)
{
    return string();
}
#endif

////////////// MegaBackupMonitor ////////////////
BackupMonitor::BackupMonitor(MegaClient *client)
    : mClient(client)
{
}

void BackupMonitor::digestPutResult(handle backupId, UnifiedSync* syncPtr)
{
#ifdef ENABLE_SYNC
    mClient->syncs.forEachUnifiedSync([&](UnifiedSync& us){
        if (&us == syncPtr)
        {
            us.mConfig.setBackupId(backupId);
            mClient->syncs.saveSyncConfig(us.mConfig);
        }
    });
#endif
}

void BackupMonitor::updateBackupInfo(handle backupId, const BackupInfo &info)
{
    string localFolderEncrypted(mClient->cypherTLVTextWithMasterKey("lf", info.localFolder()) );
    string deviceIdHash = mClient->getDeviceidHash();

    mClient->reqs.add(new CommandBackupPut(mClient,
                                           backupId,
                                           info.type(),
                                           info.megaHandle(),
                                           localFolderEncrypted.c_str(),
                                           deviceIdHash.c_str(),
                                           info.state(),
                                           info.subState(),
                                           info.extra().c_str(),
                                           nullptr));
}

#ifdef ENABLE_SYNC

void BackupMonitor::registerBackupInfo(const BackupInfo &info, UnifiedSync* syncPtr)
{
    string localFolderEncrypted(mClient->cypherTLVTextWithMasterKey("lf", info.localFolder()) );
    string deviceIdHash = mClient->getDeviceidHash();

    mClient->reqs.add(new CommandBackupPut(mClient, info.type(), info.backupName(), info.megaHandle(),
                                           localFolderEncrypted.c_str(),
                                           deviceIdHash.c_str(),
                                           info.state(), info.subState(), info.extra().c_str(),
                                           [this, syncPtr](Error e, handle h){ if (!e) digestPutResult(h, syncPtr); }));
}


void BackupMonitor::updateOrRegisterSync(UnifiedSync& us)
{
    BackupInfoSync currentInfo(us);

    if (!us.mBackupInfo && ISUNDEF(us.mConfig.getBackupId())) // not registered yet
    {
        us.mBackupInfo = ::mega::make_unique<BackupInfoSync>(us);
        registerBackupInfo(currentInfo, &us);
    }
    else if (!ISUNDEF(us.mConfig.getBackupId()) &&
           (!us.mBackupInfo || !(currentInfo == *us.mBackupInfo)))
    {
        updateBackupInfo(us.mConfig.getBackupId(), currentInfo); //queue update comand
        us.mBackupInfo = ::mega::make_unique<BackupInfoSync>(us);
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
    mClient->syncs.forEachUnifiedSync([&](UnifiedSync& us) {
        updateOrRegisterSync(us);
    });
}

void BackupMonitor::beatBackupInfo(UnifiedSync& us)
{
    // send registration or update in case we missed it
    updateOrRegisterSync(us);

    if (!us.mBackupInfo || ISUNDEF(us.mConfig.getBackupId()))
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
    mClient->syncs.forEachUnifiedSync([&](UnifiedSync& us){
        beatBackupInfo(us);
    });
#endif
}

}
