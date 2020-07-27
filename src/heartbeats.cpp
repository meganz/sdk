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


HeartBeatSyncInfo::HeartBeatSyncInfo(int tag, handle id)
    : mHeartBeatId(id), mSyncTag(tag)
{
}

handle HeartBeatSyncInfo::heartBeatId() const
{
    return mHeartBeatId;
}

Command *HeartBeatSyncInfo::runningCommand() const
{
    return mRunningCommand;
}

void HeartBeatSyncInfo::setRunningCommand(Command *runningCommand)
{
    mRunningCommand = runningCommand;
}

HeartBeatSyncInfo::Status HeartBeatSyncInfo::status() const
{
    return mStatus;
}

double HeartBeatSyncInfo::progress() const
{
    return std::max(0.,std::min(1., mTransferredBytes * 1. /mTotalBytes));
}

uint8_t HeartBeatSyncInfo::pendingUps() const
{
    return mPendingUps;
}

uint8_t HeartBeatSyncInfo::pendingDowns() const
{
    return mPendingDowns;
}

m_time_t HeartBeatSyncInfo::lastAction() const
{
    return mLastAction;
}

mega::MegaHandle HeartBeatSyncInfo::lastSyncedItem() const
{
    return mLastSyncedItem;
}

void HeartBeatSyncInfo::updateTransferInfo(MegaTransfer *transfer)
{
    if (mPendingTransfers.find(transfer->getTag()) == mPendingTransfers.end())
    {
        mPendingTransfers.insert(std::make_pair(transfer->getTag(), ::mega::make_unique<PendingTransferInfo>()));
    }

    const auto &pending = mPendingTransfers[transfer->getTag()];

    auto total = mTotalBytes;
    auto transferred = mTransferredBytes;

    // reduce globals by the last known data
    total -= pending->totalBytes;
    transferred -= pending->transferredBytes;

    // update values with those of the transfer
    pending->totalBytes = transfer->getTotalBytes();
    pending->transferredBytes = transfer->getTransferredBytes();

    // reflect those in the globals
    total += pending->totalBytes;
    transferred += pending->transferredBytes;

    setTotalBytes(total);
    setTransferredBytes(transferred);
}

void HeartBeatSyncInfo::removePendingTransfer(MegaTransfer *transfer)
{
    if (mPendingTransfers.find(transfer->getTag()) == mPendingTransfers.end())
    {
        assert(false && "removing a non included transfer");
        return;
    }

    auto &pending = mPendingTransfers[transfer->getTag()];

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

void HeartBeatSyncInfo::clearFinshedTransfers()
{
    for (const auto &pending : mFinishedTransfers)
    {
        // reduce globals by the last known data
        mTotalBytes -= pending->totalBytes;
        mTransferredBytes -= pending->transferredBytes;
    }

    mFinishedTransfers.clear();
}

void HeartBeatSyncInfo::setHeartBeatId(const handle &heartBeatId)
{
    mHeartBeatId = heartBeatId;
}

int HeartBeatSyncInfo::syncTag() const
{
    return mSyncTag;
}

void HeartBeatSyncInfo::setTransferredBytes(long long value)
{
    if (mTransferredBytes != value)
    {
        mTransferredBytes = value;
        updateLastActionTime();
    }
}

void HeartBeatSyncInfo::setTotalBytes(long long value)
{
    if (mTotalBytes != value)
    {
        mTotalBytes = value;
        updateLastActionTime();
    }
}

void HeartBeatSyncInfo::setLastSyncedItem(const mega::MegaHandle &lastSyncedItem)
{
    if (mLastSyncedItem != lastSyncedItem)
    {
        mLastSyncedItem = lastSyncedItem;
        updateLastActionTime();
    }
}

void HeartBeatSyncInfo::setPendingDowns(uint8_t pendingDowns)
{
    if (mPendingDowns != pendingDowns)
    {
        mPendingDowns = pendingDowns;
        updateLastActionTime();
    }
}

void HeartBeatSyncInfo::setPendingUps(uint8_t pendingUps)
{
    if (mPendingUps != pendingUps)
    {
        mPendingUps = pendingUps;
        updateLastActionTime();
    }
}

void HeartBeatSyncInfo::setStatus(const Status &status)
{
    if (mStatus != status)
    {
        mStatus = status;
        updateLastActionTime();
    }
}

void HeartBeatSyncInfo::setLastAction(const m_time_t &lastAction)
{
    mLastAction = lastAction;
}

void HeartBeatSyncInfo::updateLastActionTime()
{
    setLastAction(m_time(nullptr));
}

void HeartBeatSyncInfo::setLastBeat(const m_time_t &lastBeat)
{
    mLastBeat = lastBeat;
}

m_time_t HeartBeatSyncInfo::lastBeat() const
{
    return mLastBeat;
}

void HeartBeatSyncInfo::onCommandToBeDeleted(Command *command)
{
    if (mRunningCommand == command)
    {
        mRunningCommand = nullptr;
    }
}

MegaHeartBeatMonitor::MegaHeartBeatMonitor(MegaClient *client)
    : mClient(client)
{
}

MegaHeartBeatMonitor::~MegaHeartBeatMonitor()
{
}

void MegaHeartBeatMonitor::reset()
{
    mPendingBackupPuts.clear();
    mHeartBeatedSyncs.clear();
    mLastBeat = 0;
}

void MegaHeartBeatMonitor::setRegisteredId(handle id)
{
    bool needsAdding = true;

    if (id == UNDEF)
    {
        LOG_warn << " setting invalid id";
        needsAdding = false;
        return;
    }

    assert(mPendingBackupPuts.size());
    auto syncTag = mPendingBackupPuts.front();

    if (mHeartBeatedSyncs.find(syncTag) != mHeartBeatedSyncs.end())
    {
        mHeartBeatedSyncs[syncTag]->setHeartBeatId(id);
        needsAdding = false;
    }

    if (needsAdding)
    {
        for (const auto &hBPair : mHeartBeatedSyncs)
        {
            if (hBPair.second->heartBeatId() == id)
            {
                needsAdding = false;
                break;
            }
        }

        if (needsAdding)
        {
            //create new HeartBeatSyncInfo
            mHeartBeatedSyncs[syncTag] = std::make_shared<HeartBeatSyncInfo>(syncTag, id);
        }
    }

    mClient->updateSyncHearBeatID(syncTag, id);

    mPendingBackupPuts.pop_front();
}

int MegaHeartBeatMonitor::getHBState(MegaSync *sync)
{
    if (sync->isTemporaryDisabled())
    {
        return MegaHeartBeatMonitor::State::TEMPORARY_DISABLED;
    }
    else if (sync->isActive())
    {
        //TODO: consider use case: paused, if transfers are paused!?
        return MegaHeartBeatMonitor::State::ACTIVE;
    }
    else if (!sync->isEnabled())
    {
        return MegaHeartBeatMonitor::State::DISABLED;
    }
    else
    {
        return MegaHeartBeatMonitor::State::FAILED;
    }
}

int MegaHeartBeatMonitor::getHBSubstatus(MegaSync *sync)
{
    return sync->getError();
}

string MegaHeartBeatMonitor::getHBExtraData(MegaSync *sync)
{
    return "";
}

BackupType MegaHeartBeatMonitor::getHBType(MegaSync *sync)
{
    return BackupType::TWO_WAY; // TODO: get that from sync whenever others are supported
}

void MegaHeartBeatMonitor::updateOrRegisterSync(MegaSync *sync)
{
    if (!sync)
    {
        return;
    }
    auto config = mClient->syncConfigs->get(sync->getTag());

    handle syncID = UNDEF;
    if (config)
    {
        syncID = config->getHeartBeatID();
    }

    std::unique_ptr<string> localFolderEncrypted(mClient->cypherTLVTextWithMasterKey("lf", sync->getLocalFolder()) );
    std::unique_ptr<string> deviceIDEncrypted(mClient->cypherTLVTextWithMasterKey("de", mClient->getDeviceid()) );
    std::unique_ptr<string> nameEncrypted(mClient->cypherTLVTextWithMasterKey("na", sync->getName()) );

    string extraData;
    if (syncID == UNDEF) //register
    {
        mClient->reqs.add(new CommandBackupPut(mClient, BackupType::TWO_WAY, sync->getMegaHandle(), localFolderEncrypted->c_str(),
                                           deviceIDEncrypted->c_str(), nameEncrypted->c_str(),
                                           getHBState(sync), getHBSubstatus(sync), getHBExtraData(sync)
                                           ));
    }
    else //update
    {
        mClient->reqs.add(new CommandBackupPut(mClient, syncID, BackupType::TWO_WAY, sync->getMegaHandle(), localFolderEncrypted->c_str(),
                                               deviceIDEncrypted->c_str(), nameEncrypted->c_str(),
                                               getHBState(sync), getHBSubstatus(sync), getHBExtraData(sync).c_str()
                                               ));
    }

    mPendingBackupPuts.push_back(sync->getTag());
}

void MegaHeartBeatMonitor::onSyncAdded(MegaApi *api, MegaSync *sync, int additionState)
{
    updateOrRegisterSync(sync);
}

void MegaHeartBeatMonitor::onSyncStateChanged(MegaApi *api, MegaSync *sync)
{
    updateOrRegisterSync(sync);
}

std::shared_ptr<HeartBeatSyncInfo> MegaHeartBeatMonitor::getSyncHeartBeatInfoByTransfer(MegaTransfer *transfer)
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
            auto newHB = mHeartBeatedSyncs[syncTag] = std::make_shared<HeartBeatSyncInfo>(syncTag, UNDEF);
            return newHB;
        }
    }

    return nullptr;
}

void MegaHeartBeatMonitor::onTransferStart(MegaApi *api, MegaTransfer *transfer)
{
    auto hbs = getSyncHeartBeatInfoByTransfer(transfer);
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

void MegaHeartBeatMonitor::onTransferUpdate(MegaApi *api, MegaTransfer *transfer)
{
    auto hbs = getSyncHeartBeatInfoByTransfer(transfer);
    if (hbs)
    {
        hbs->updateTransferInfo(transfer);
    }
}

void MegaHeartBeatMonitor::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error)
{
    auto hbs = getSyncHeartBeatInfoByTransfer(transfer);
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

void MegaHeartBeatMonitor::onSyncDeleted(MegaApi *api, MegaSync *sync)
{
    auto hBPair = mHeartBeatedSyncs.find(sync->getTag());
    if (hBPair != mHeartBeatedSyncs.end())
    {
        mClient->reqs.add(new CommandBackupRemove(mClient, hBPair->second->heartBeatId()));

        mHeartBeatedSyncs.erase(hBPair); //This is speculative: could be moved to backupremove_result
        // in case we wanted to handle possible faling cases.
    }
}

void MegaHeartBeatMonitor::calculateStatus(HeartBeatSyncInfo *hbs)
{
    HeartBeatSyncInfo::Status status = HeartBeatSyncInfo::Status::INACTIVE;

    int tag = hbs->syncTag();

    for (sync_list::iterator it = mClient->syncs.begin(); it != mClient->syncs.end(); it++)
    {
        Sync *sync = (*it);
        if (sync->tag == tag)
        {
            switch(sync->localroot->ts)
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
    }

    hbs->setStatus(status);
}

void MegaHeartBeatMonitor::beat()
{
    for (const auto &hBPair : mHeartBeatedSyncs)
    {
        HeartBeatSyncInfo *hbs  = hBPair.second.get();
        auto now = m_time(nullptr);
        auto lapsed = now - hbs->lastBeat();
        if ( (hbs->lastAction() > hbs->lastBeat()) //something happened since last reported!
             || lapsed > MAX_HEARBEAT_SECS_DELAY) // max delay happened. Beating: Sicherheitsfahrschaltung!
        {
            calculateStatus(hbs);

            hbs->setLastBeat(m_time(nullptr));

            auto newCommand = new CommandBackupPutHeartBeat(mClient, hbs->heartBeatId(), hbs->status(),
                              static_cast<uint8_t>(std::lround(hbs->progress()*100.0)), hbs->pendingUps(), hbs->pendingDowns(),
                              hbs->lastAction(), hbs->lastSyncedItem());

            auto runningCommand = hbs->runningCommand();

            if (runningCommand && !runningCommand->getRead()) //replace existing command
            {
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
