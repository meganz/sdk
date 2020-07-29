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

namespace mega {


HeartBeatBackupInfo::HeartBeatBackupInfo(int tag, handle aBackupId)
    : mBackupId(aBackupId), mSyncTag(tag)
{
}

handle HeartBeatBackupInfo::backupId() const
{
    return mBackupId;
}

Command *HeartBeatBackupInfo::runningCommand() const
{
    return mRunningCommand;
}

void HeartBeatBackupInfo::setRunningCommand(Command *runningCommand)
{
    mRunningCommand = runningCommand;
}

HeartBeatBackupInfo::Status HeartBeatBackupInfo::status() const
{
    return mStatus;
}

double HeartBeatBackupInfo::progress() const
{
    return std::max(0., std::min(1., static_cast<double>(mTransferredBytes) / static_cast<double>(mTotalBytes)));
}

uint32_t HeartBeatBackupInfo::pendingUps() const
{
    return mPendingUps;
}

uint32_t HeartBeatBackupInfo::pendingDowns() const
{
    return mPendingDowns;
}

m_time_t HeartBeatBackupInfo::lastAction() const
{
    return mLastAction;
}

mega::MegaHandle HeartBeatBackupInfo::lastItemUpdated() const
{
    return mLastItemUpdated;
}

void HeartBeatBackupInfo::updateTransferInfo(MegaTransfer *transfer)
{
    auto it = mPendingTransfers.find(transfer->getTag());
    if (it == mPendingTransfers.end())
    {
        it = mPendingTransfers.insert(std::make_pair(transfer->getTag(), ::mega::make_unique<PendingTransferInfo>())).first;
    }

    const unique_ptr<PendingTransferInfo> &pending = it->second;

    auto total = mTotalBytes;
    auto transferred = mTransferredBytes;

    // reduce globals by the last known data
    total -= pending->mTotalBytes;
    transferred -= pending->mTransferredBytes;

    // update values with those of the transfer
    pending->mTotalBytes = transfer->getTotalBytes();
    pending->mTransferredBytes = transfer->getTransferredBytes();

    // reflect those in the globals
    total += pending->mTotalBytes;
    transferred += pending->mTransferredBytes;

    setTotalBytes(total);
    setTransferredBytes(transferred);
}

void HeartBeatBackupInfo::removePendingTransfer(MegaTransfer *transfer)
{
    const auto it = mPendingTransfers.find(transfer->getTag());
    if (it == mPendingTransfers.end())
    {
        assert(false && "removing a non included transfer");
        return;
    }

    unique_ptr<PendingTransferInfo> &pending = it->second;

    // add the transfer data to a list of finished. To reduce the totals with its values
    // when we consider progress is complete
    mFinishedTransfers.push_back(std::move(pending));

    mPendingTransfers.erase(transfer->getTag());

    if (mPendingTransfers.empty())
    {
        clearFinshedTransfers(); // asume the sync is up-to-date: clear totals
        assert(!mPendingUps && !mPendingDowns);
        mTotalBytes = 0;
        mTransferredBytes = 0;
    }
}

void HeartBeatBackupInfo::clearFinshedTransfers()
{
    for (const auto &pending : mFinishedTransfers)
    {
        // reduce globals by the last known data
        mTotalBytes -= pending->mTotalBytes;
        mTransferredBytes -= pending->mTransferredBytes;
    }

    mFinishedTransfers.clear();
}

void HeartBeatBackupInfo::setBackupId(const handle &backupId)
{
    mBackupId = backupId;
}

int HeartBeatBackupInfo::syncTag() const
{
    return mSyncTag;
}

void HeartBeatBackupInfo::setTransferredBytes(long long value)
{
    if (mTransferredBytes != value)
    {
        mTransferredBytes = value;
        updateLastActionTime();
    }
}

void HeartBeatBackupInfo::setTotalBytes(long long value)
{
    if (mTotalBytes != value)
    {
        mTotalBytes = value;
        updateLastActionTime();
    }
}

void HeartBeatBackupInfo::setLastSyncedItem(const mega::MegaHandle &lastSyncedItem)
{
    if (mLastItemUpdated != lastSyncedItem)
    {
        mLastItemUpdated = lastSyncedItem;
        updateLastActionTime();
    }
}

void HeartBeatBackupInfo::setPendingDowns(uint32_t pendingDowns)
{
    if (mPendingDowns != pendingDowns)
    {
        mPendingDowns = pendingDowns;
        updateLastActionTime();
    }
}

void HeartBeatBackupInfo::setPendingUps(uint32_t pendingUps)
{
    if (mPendingUps != pendingUps)
    {
        mPendingUps = pendingUps;
        updateLastActionTime();
    }
}

void HeartBeatBackupInfo::setStatus(const Status &status)
{
    if (mStatus != status)
    {
        mStatus = status;
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

MegaBackupMonitor::MegaBackupMonitor(MegaClient *client)
    : mClient(client)
{
}

MegaBackupMonitor::~MegaBackupMonitor()
{
}

void MegaBackupMonitor::reset()
{
    mPendingBackupPuts.clear();
    mHeartBeatedSyncs.clear();
    mPendingSyncUpdates.clear();
    mLastBeat = 0;
}

void MegaBackupMonitor::digestPutResult(handle backupId)
{
    bool needsAdding = true;

    // get the tag from queue of pending puts
    assert(mPendingBackupPuts.size());
    auto syncTag = mPendingBackupPuts.front();
    mPendingBackupPuts.pop_front();

    if (ISUNDEF(backupId))
    {
        LOG_warn << "Received invalid id for sync with tag: " << syncTag;
        needsAdding = false;
    }

    // set heartBeat ID

    if (!ISUNDEF(backupId) && mHeartBeatedSyncs.find(syncTag) != mHeartBeatedSyncs.end())
    {
        mHeartBeatedSyncs[syncTag]->setBackupId(backupId);
        needsAdding = false;
    }

    if (needsAdding)
    {
        for (const auto &hBPair : mHeartBeatedSyncs)
        {
            if (hBPair.second->backupId() == backupId)
            {
                needsAdding = false;
                break;
            }
        }

        if (needsAdding)
        {
            //create new HeartBeatSyncInfo
            mHeartBeatedSyncs[syncTag] = std::make_shared<HeartBeatBackupInfo>(syncTag, backupId);
        }
    }

    // store the id in the sync configuration
    if (!ISUNDEF(backupId))
    {
        mClient->updateSyncBackupId(syncTag, backupId);
    }

    // handle pending updates
    const auto &pendingSyncPair = mPendingSyncUpdates.find(syncTag);
    if (pendingSyncPair != mPendingSyncUpdates.end())
    {
        if (!ISUNDEF(backupId))
        {
            updateSyncInfo(backupId, pendingSyncPair->second.get());
        }
        else
        {
            LOG_warn << "discarding heartbeat update for pending sync: no valid id received for sync: " << pendingSyncPair->second->getLocalFolder();

        }
        mPendingSyncUpdates.erase(pendingSyncPair);
    }
}

int MegaBackupMonitor::getHBState(MegaSync *sync)
{
    if (sync->isTemporaryDisabled())
    {
        return MegaBackupMonitor::State::TEMPORARY_DISABLED;
    }
    else if (sync->isActive())
    {
        //TODO: consider use case: paused, if transfers are paused!?
        return MegaBackupMonitor::State::ACTIVE;
    }
    else if (!sync->isEnabled())
    {
        return MegaBackupMonitor::State::DISABLED;
    }
    else
    {
        return MegaBackupMonitor::State::FAILED;
    }
}

int MegaBackupMonitor::getHBSubstatus(MegaSync *sync)
{
    return sync->getError();
}

string MegaBackupMonitor::getHBExtraData(MegaSync *sync)
{
    return "";
}

BackupType MegaBackupMonitor::getHBType(MegaSync *sync)
{
    return BackupType::TWO_WAY; // TODO: get that from sync whenever others are supported
}

void MegaBackupMonitor::updateSyncInfo(handle backupId, MegaSync *sync)
{

    string localFolderEncrypted(mClient->cypherTLVTextWithMasterKey("lf", sync->getLocalFolder()) );
    string deviceIDEncrypted(mClient->cypherTLVTextWithMasterKey("de", mClient->getDeviceid()) );
    string nameEncrypted(mClient->cypherTLVTextWithMasterKey("na", sync->getName()) );


    mClient->reqs.add(new CommandBackupPut(mClient, backupId, BackupType::TWO_WAY, sync->getMegaHandle(), localFolderEncrypted.c_str(),
                                           deviceIDEncrypted.c_str(), nameEncrypted.c_str(),
                                           getHBState(sync), getHBSubstatus(sync), getHBExtraData(sync).c_str()
                                           ));
}

void MegaBackupMonitor::updateOrRegisterSync(MegaSync *sync)
{
    if (!sync)
    {
        return;
    }
    auto config = mClient->syncConfigs->get(sync->getTag());

    handle syncID = UNDEF;
    if (config)
    {
        syncID = config->getBackupId();
    }

    bool pushInPending = false;

    string extraData;
    if (syncID == UNDEF) //register
    {

        string localFolderEncrypted(mClient->cypherTLVTextWithMasterKey("lf", sync->getLocalFolder()) );
        string deviceIDEncrypted(mClient->cypherTLVTextWithMasterKey("de", mClient->getDeviceid()) );
        string nameEncrypted(mClient->cypherTLVTextWithMasterKey("na", sync->getName()) );


        if (std::find(mPendingBackupPuts.begin(), mPendingBackupPuts.end(), sync->getTag()) == mPendingBackupPuts.end())
        {
            pushInPending = true;
            mClient->reqs.add(new CommandBackupPut(mClient, BackupType::TWO_WAY, sync->getMegaHandle(), localFolderEncrypted.c_str(),
                                           deviceIDEncrypted.c_str(), nameEncrypted.c_str(),
                                           getHBState(sync), getHBSubstatus(sync), getHBExtraData(sync)
                                           ));
        }
        else // ID not received yet, let's queue the update (copying sync data)
        {
            LOG_debug << " Queuing sync update, register is on progress for sync: " << sync->getLocalFolder();
            mPendingSyncUpdates[sync->getTag()].reset(sync->copy()); // we replace any previous pending updates
        }
    }
    else //update
    {
        pushInPending = true;
        updateSyncInfo(syncID, sync);
    }

    if (pushInPending)
    {
        mPendingBackupPuts.push_back(sync->getTag());
    }
}

void MegaBackupMonitor::onSyncAdded(MegaApi *api, MegaSync *sync, int additionState)
{
    updateOrRegisterSync(sync);
}

void MegaBackupMonitor::onSyncStateChanged(MegaApi *api, MegaSync *sync)
{
    updateOrRegisterSync(sync);
}

std::shared_ptr<HeartBeatBackupInfo> MegaBackupMonitor::getHeartBeatBackupInfoByTransfer(MegaTransfer *transfer)
{
    if (!transfer->isSyncTransfer())
    {
        return nullptr;
    }

    int syncTag = 0;

    // use map to get the syncTag directly if there was one
    auto mTSPair = mTransferToSyncMap.find(transfer->getTag());
    if (mTSPair != mTransferToSyncMap.end())
    {
        syncTag = mTSPair->second;
    }

    if (!syncTag) //first time
    {
        Node *n = mClient->nodebyhandle(transfer->getType() == MegaTransfer::TYPE_UPLOAD ? transfer->getParentHandle() : transfer->getNodeHandle());
        while (n)
        {
            if (n && n->localnode && n->localnode->sync)
            {
                syncTag = n->localnode->sync->tag;
                mTransferToSyncMap[transfer->getTag()] = syncTag;
                break;
            }
            LOG_warn << "Heartbeat could not get sync tag direclty from transfer handle. Going up";
            n = n->parent;
        }
    }

    if (syncTag)
    {
        auto hBPair = mHeartBeatedSyncs.find(syncTag);
        if (hBPair->second)
        {
            return hBPair->second;
        }
        else
        {
            //create new HeartBeatSyncInfo
            auto newHB = mHeartBeatedSyncs[syncTag] = std::make_shared<HeartBeatBackupInfo>(syncTag, UNDEF);
            return newHB;
        }
    }

    return nullptr;
}

void MegaBackupMonitor::onTransferStart(MegaApi *api, MegaTransfer *transfer)
{
    auto hbs = getHeartBeatBackupInfoByTransfer(transfer);
    if (hbs)
    {
        if (transfer->getType() == MegaTransfer::TYPE_UPLOAD)
        {
            hbs->setPendingUps(hbs->pendingUps() + 1);
        }
        else
        {
            hbs->setPendingDowns(hbs->pendingDowns() + 1);
        }
        hbs->updateTransferInfo(transfer);
    }
}

void MegaBackupMonitor::onTransferUpdate(MegaApi *api, MegaTransfer *transfer)
{
    auto hbs = getHeartBeatBackupInfoByTransfer(transfer);
    if (hbs)
    {
        hbs->updateTransferInfo(transfer);
    }
}

void MegaBackupMonitor::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error)
{
    auto hbs = getHeartBeatBackupInfoByTransfer(transfer);
    if (hbs)
    {
        if (transfer->getType() == MegaTransfer::TYPE_UPLOAD)
        {
            hbs->setPendingUps(hbs->pendingUps() - 1);
        }
        else
        {
            hbs->setPendingDowns(hbs->pendingDowns() - 1);
        }
        hbs->updateTransferInfo(transfer);

        hbs->removePendingTransfer(transfer);
        mTransferToSyncMap.erase(transfer->getTag());

        if (error->getErrorCode() == API_OK)
        {
            hbs->setLastSyncedItem(transfer->getNodeHandle());
        }
    }
}

void MegaBackupMonitor::onSyncDeleted(MegaApi *api, MegaSync *sync)
{
    auto hBPair = mHeartBeatedSyncs.find(sync->getTag());
    if (hBPair != mHeartBeatedSyncs.end())
    {
        mClient->reqs.add(new CommandBackupRemove(mClient, hBPair->second->backupId()));

        mHeartBeatedSyncs.erase(hBPair); //This is speculative: could be moved to backupremove_result
        // in case we wanted to handle possible failing cases.
    }
}

void MegaBackupMonitor::calculateStatus(HeartBeatBackupInfo *hbs)
{
    HeartBeatBackupInfo::Status status = HeartBeatBackupInfo::Status::INACTIVE;

    int tag = hbs->syncTag();

    for (sync_list::iterator it = mClient->syncs.begin(); it != mClient->syncs.end(); it++)
    {
        Sync *sync = (*it);
        if (sync->tag == tag)
        {
            switch(sync->localroot->ts)
            {
            case TREESTATE_SYNCED:
                status = HeartBeatBackupInfo::Status::UPTODATE;
                break;
            case TREESTATE_PENDING:
                status = HeartBeatBackupInfo::Status::PENDING;
                break;
            case TREESTATE_SYNCING:
                status = HeartBeatBackupInfo::Status::SYNCING;
                break;
            default:
                status = HeartBeatBackupInfo::Status::UNKNOWN;
                break;
            }
        }
    }

    hbs->setStatus(status);
}

void MegaBackupMonitor::beat()
{
    for (const auto &hBPair : mHeartBeatedSyncs)
    {
        HeartBeatBackupInfo *hbs  = hBPair.second.get();
        auto now = m_time(nullptr);
        auto lapsed = now - hbs->lastBeat();
        if ( (hbs->lastAction() > hbs->lastBeat()) //something happened since last reported!
             || lapsed > MAX_HEARBEAT_SECS_DELAY) // max delay happened. Beating: Sicherheitsfahrschaltung!
        {
            calculateStatus(hbs);

            hbs->setLastBeat(m_time(nullptr));

            auto newCommand = new CommandBackupPutHeartBeat(mClient, hbs->backupId(), hbs->status(),
                              static_cast<uint8_t>(std::lround(hbs->progress()*100.0)), hbs->pendingUps(), hbs->pendingDowns(),
                              hbs->lastAction(), hbs->lastItemUpdated());

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
                newCommand->addListener(hBPair.second);
                mClient->reqs.add(newCommand);
            }
        }
    }
}



}
