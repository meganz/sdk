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

Command *HeartBeatBackupInfo::runningCommand() const
{
    return mRunningCommand;
}

void HeartBeatBackupInfo::setRunningCommand(Command *runningCommand)
{
    mRunningCommand = runningCommand;
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
}

void HeartBeatBackupInfo::setLastBeat(const m_time_t &lastBeat)
{
    mLastBeat = lastBeat;
}

m_time_t HeartBeatBackupInfo::lastBeat() const
{
    return mLastBeat;
}

void HeartBeatBackupInfo::onCommandToBeDeleted(Command *command)
{
    if (mRunningCommand == command)
    {
        mRunningCommand = nullptr;
    }
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

MegaBackupInfo::MegaBackupInfo(BackupType type, string backupName, string localFolder, handle megaHandle, int state, int substate, std::string extra)
    : mType(type)
    , mBackupName(backupName)
    , mLocalFolder(localFolder)
    , mMegaHandle(megaHandle)
    , mState(state)
    , mSubState(substate)
    , mExtra(extra)
{

}

BackupType MegaBackupInfo::type() const
{
    return mType;
}

string MegaBackupInfo::backupName() const
{
    return mBackupName;
}

string MegaBackupInfo::localFolder() const
{
    return mLocalFolder;
}

handle MegaBackupInfo::megaHandle() const
{
    return mMegaHandle;
}

int MegaBackupInfo::state() const
{
    return mState;
}

int MegaBackupInfo::subState() const
{
    return mSubState;
}

string MegaBackupInfo::extra() const
{
    return mExtra;
}

#ifdef ENABLE_SYNC
MegaBackupInfoSync::MegaBackupInfoSync(UnifiedSync& us)
    : MegaBackupInfo(getSyncType(us.mConfig),
                     us.mConfig.getName(),
                     us.mConfig.getLocalPath(),
                     us.mConfig.getRemoteNode(),
                     getSyncState(us),
                     getSyncSubstatus(us),
                     getSyncExtraData(us))
{
}

int MegaBackupInfoSync::calculatePauseActiveState(MegaClient *client)
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


int MegaBackupInfoSync::getSyncState(UnifiedSync& us)
{
    SyncError error = us.mConfig.getError();
    syncstate_t state = us.mSync ? us.mSync->state : SYNC_FAILED;

    if (state == SYNC_DISABLED && error != NO_SYNC_ERROR)
    {
        return State::TEMPORARY_DISABLED;
    }
    else if (state != SYNC_FAILED && state != SYNC_CANCELED && state != SYNC_DISABLED)
    {
        return calculatePauseActiveState(&us.mClient);
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

BackupType MegaBackupInfoSync::getSyncType(const SyncConfig& config)
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

int MegaBackupInfoSync::getSyncSubstatus(UnifiedSync& us)
{
    return us.mConfig.getError();
}

string MegaBackupInfoSync::getSyncExtraData(UnifiedSync&)
{
    return string();
}
#endif

////////////// MegaBackupMonitor ////////////////
MegaBackupMonitor::MegaBackupMonitor(MegaClient *client)
    : mClient(client)
{
}

void MegaBackupMonitor::digestPutResult(handle backupId, UnifiedSync* syncPtr)
{
    mClient->syncs.forEachUnifiedSync([&](UnifiedSync& us){
        if (&us == syncPtr)
        {
            us.mConfig.setBackupId(backupId);
            mClient->syncs.saveSyncConfig(&us.mConfig);
        }
    });
}

void MegaBackupMonitor::updateBackupInfo(handle backupId, const MegaBackupInfo &info)
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


void MegaBackupMonitor::registerBackupInfo(const MegaBackupInfo &info, UnifiedSync* syncPtr)
{
    string localFolderEncrypted(mClient->cypherTLVTextWithMasterKey("lf", info.localFolder()) );
    string deviceIdHash = mClient->getDeviceidHash();

    mClient->reqs.add(new CommandBackupPut(mClient, info.type(), info.backupName(), info.megaHandle(),
                                           localFolderEncrypted.c_str(),
                                           deviceIdHash.c_str(),
                                           info.state(), info.subState(), info.extra().c_str(),
                                           [this, syncPtr](Error e, handle h){ if (!e) digestPutResult(h, syncPtr); }));
}

#ifdef ENABLE_SYNC

void MegaBackupMonitor::updateOrRegisterSync(UnifiedSync& us)
{
    MegaBackupInfoSync currentInfo(us);

    if (!us.mBackupInfo && us.mConfig.getBackupId() == UNDEF) // not registered yet
    {
        us.mBackupInfo = ::mega::make_unique<MegaBackupInfoSync>(us);
        registerBackupInfo(currentInfo, &us);
    }
    else if (us.mConfig.getBackupId() != UNDEF  &&
           (!us.mBackupInfo || !(currentInfo == *us.mBackupInfo)))
    {
        updateBackupInfo(us.mConfig.getBackupId(), currentInfo); //queue update comand
        us.mBackupInfo = ::mega::make_unique<MegaBackupInfoSync>(us);
    }
}

bool  MegaBackupInfoSync::operator==(const MegaBackupInfoSync& o) const
{
    return  mType == o.mType &&
            mLocalFolder == o.mLocalFolder &&
            mMegaHandle == o.mMegaHandle &&
            mState == o.mState &&
            mSubState == o.mSubState &&
            mExtra == o.mExtra;
}
#endif

void MegaBackupMonitor::onSyncConfigChanged()
{
#ifdef ENABLE_SYNC
    mClient->syncs.forEachUnifiedSync([&](UnifiedSync& us) {
        updateOrRegisterSync(us);
    });
#endif
}

void MegaBackupMonitor::calculateStatus(HeartBeatBackupInfo *hbs, UnifiedSync& us)
{
   hbs->updateStatus(us);
}

void MegaBackupMonitor::beatBackupInfo(UnifiedSync& us)
{
    // send registration or update in case we missed it
    updateOrRegisterSync(us);

    if (!us.mBackupInfo || us.mConfig.getBackupId() == UNDEF)
    {
        LOG_warn << "Backup not registered yet. Skipping heartbeat...";
        return;
    }

    auto& hbs = us.mNextHeartbeat;

    auto now = m_time(nullptr);
    auto lapsed = now - hbs->lastBeat();
    if ( (hbs->lastAction() > hbs->lastBeat()) //something happened since last reported!
         || lapsed > MAX_HEARBEAT_SECS_DELAY) // max delay happened. Beating: Sicherheitsfahrschaltung!
    {
        calculateStatus(hbs.get(), us); //we asume this is costly: only do it when beating

        hbs->setLastBeat(m_time(nullptr));

        int8_t progress = (hbs->progress() < 0) ? -1 : static_cast<int8_t>(std::lround(hbs->progress()*100.0));

        auto newCommand = new CommandBackupPutHeartBeat(mClient, us.mConfig.getBackupId(),  static_cast<uint8_t>(hbs->status()),
                          progress, hbs->mPendingUps, hbs->mPendingDowns,
                          hbs->lastAction(), hbs->lastItemUpdated());

#ifdef ENABLE_SYNC
        if (hbs->status() == HeartBeatSyncInfo::Status::UPTODATE && progress >= 100)
        {
            hbs->invalidateProgress(); // we invalidate progress, so as not to keep on reporting 100% progress after reached up to date
            // note: new transfer updates will modify the progress and make it valid again
        }
#endif

        auto runningCommand = hbs->runningCommand();

        if (runningCommand && !runningCommand->getRead()) //replace existing command
        {
            LOG_warn << "Detected a yet unprocessed beat: replacing data with current";
            // instead of appending a new command, and potentially hammering, we just update the existing command with the updated input
            runningCommand->replaceWith(*newCommand);
        }
        else // append new command
        {
            hbs->setRunningCommand(newCommand);
            mClient->reqs.add(newCommand);
        }
    }
}
void MegaBackupMonitor::beat()
{
#ifdef ENABLE_SYNC
    mClient->syncs.forEachUnifiedSync([&](UnifiedSync& us){
        beatBackupInfo(us);
    });
#endif
}

}
