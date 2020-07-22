/**
 * @file heartbeats.cpp
 * @brief TODO: complete this
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

long long HeartBeatSyncInfo::pendingUps() const
{
    return mPendingUps;
}

long long HeartBeatSyncInfo::pendingDowns() const
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

    const auto &pending = mPendingTransfers[transfer->getTag()];

    // reduce globals by the last known data
    mTotalBytes -= pending->totalBytes;
    mTransferredBytes -= pending->transferredBytes;

    mPendingTransfers.erase(transfer->getTag());

}

void HeartBeatSyncInfo::setTransferredBytes(long long value)
{
    mTransferredBytes = value;
    updateLastActionTime();
}

void HeartBeatSyncInfo::setTotalBytes(long long value)
{
    mTotalBytes = value;
    updateLastActionTime();
}

void HeartBeatSyncInfo::setLastSyncedItem(const mega::MegaHandle &lastSyncedItem)
{
    mLastSyncedItem = lastSyncedItem;
}

void HeartBeatSyncInfo::setPendingDowns(long long pendingDowns)
{
    mPendingDowns = pendingDowns;
    updateLastActionTime();
}

void HeartBeatSyncInfo::setPendingUps(long long pendingUps)
{
    mPendingUps = pendingUps;
    updateLastActionTime();
}

void HeartBeatSyncInfo::setStatus(const Status &status)
{
    mStatus = status;
    updateLastActionTime();
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
    auto tag = mPendingBackupPuts.front();

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
        mHeartBeatedSyncs[tag] = ::mega::make_unique<HeartBeatSyncInfo>(tag, id);
    }

    mPendingBackupPuts.pop_front();
}

int MegaHeartBeatMonitor::getHBStatus(MegaSync *sync)
{
    return sync->getState(); // TODO: do the actual conversion
}

int MegaHeartBeatMonitor::getHBSubstatus(MegaSync *sync)
{
    return sync->getError(); // TODO: define this & document
}

string MegaHeartBeatMonitor::getHBExtraData(MegaSync *sync)
{
    return ""; // TODO: define this & document
}

BackupType MegaHeartBeatMonitor::getHBType(MegaSync *sync)
{
    return BackupType::TWO_WAY; // TODO: get that from sync whenever others are supported
}

void MegaHeartBeatMonitor::updateOrRegisterSync(MegaSync *sync)
{
    string extraData;
    mClient->reqs.add(new CommandBackupPut(mClient, BackupType::TWO_WAY, sync->getMegaHandle(), sync->getLocalFolder(),
                                           mClient->getDeviceid().c_str(), sync->getLocalFolder(),  /*TODO: start holding name, gotten from MEGAsync*/
                                           getHBStatus(sync), getHBSubstatus(sync), getHBExtraData(sync)
                                           ));

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

std::shared_ptr<HeartBeatSyncInfo> MegaHeartBeatMonitor::getSyncTagByTransfer(MegaTransfer *transfer)
{
    if (!transfer->isSyncTransfer())
    {
        return nullptr;
    }

    int tag = 0;

    Node *n = mClient->nodebyhandle(transfer->getType() == MegaTransfer::TYPE_UPLOAD ? transfer->getParentHandle() : transfer->getNodeHandle());
    if (n && n->localnode && n->localnode->sync)
    {
         tag = n->localnode->sync->tag;
    }

    if (tag)
    {
        auto hBPair = mHeartBeatedSyncs.find(tag);
        return hBPair->second;
    }

    return nullptr;
}

void MegaHeartBeatMonitor::onTransferStart(MegaApi *api, MegaTransfer *transfer)
{
    auto hbs = getSyncTagByTransfer(transfer);
    if (hbs)
    {
        if (transfer->getType() == MegaTransfer::TYPE_UPLOAD)
        {
            hbs->setPendingUps(hbs->pendingUps() + 1);
        }
        else
        {
            hbs->setPendingUps(hbs->pendingDowns() + 1);
        }
        hbs->updateTransferInfo(transfer);
    }
}

void MegaHeartBeatMonitor::onTransferUpdate(MegaApi *api, MegaTransfer *transfer)
{
    auto hbs = getSyncTagByTransfer(transfer);
    if (hbs)
    {
        hbs->updateTransferInfo(transfer);
    }
}

void MegaHeartBeatMonitor::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error)
{
    auto hbs = getSyncTagByTransfer(transfer);
    if (hbs)
    {
        if (transfer->getType() == MegaTransfer::TYPE_UPLOAD)
        {
            hbs->setPendingUps(hbs->pendingUps() - 1);
        }
        else
        {
            hbs->setPendingUps(hbs->pendingDowns() - 1);
        }
        hbs->removePendingTransfer(transfer);
    }
}

void MegaHeartBeatMonitor::onSyncDeleted(MegaApi *api, MegaSync *sync)
{
    auto hBPair = mHeartBeatedSyncs.find(sync->getTag());
    if (hBPair != mHeartBeatedSyncs.end())
    {
        mClient->reqs.add(new CommandBackupRemove(mClient, hBPair->second->heartBeatId()));

        mHeartBeatedSyncs.erase(hBPair); //TODO: this is speculative: we might want to do it upon backupremove_result
        // and consider retries (perhaps that's already considered)
    }
}

void MegaHeartBeatMonitor::beat()
{
    for (const auto &hBPair : mHeartBeatedSyncs)
    {
        HeartBeatSyncInfo *hbs  = hBPair.second.get();
        auto now = m_time(nullptr);
        if ( hbs->lastAction() > hbs->lastBeat() //something happened since last reported!
             || now > hbs->lastBeat() + 30*60) //30 minutes without beating // TODO: use variables
        {
            hbs->setLastBeat(now);
            auto newCommand = new CommandBackupPutHeartBeat(mClient, hbs->heartBeatId(), hbs->status(),
                                                            hbs->progress(), hbs->pendingUps(), hbs->pendingDowns(), hbs->lastAction(), hbs->lastSyncedItem());

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
