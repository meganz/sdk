/**
 * @file heartbeats.h
 * @brief
 * TODO: complete this
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#pragma once

#include "megaapi.h"
#include "types.h"
#include "mega.h"
#include <memory>

namespace mega
{

/**
 * @brief The HeartBeatSyncInfo class
 * This class holds the information that will be heartbeated
 */
class HeartBeatSyncInfo : public CommandListener
{

    enum Status
    {
        SCANNING = 1,
        UPTODATE = 2, //user disabled (if no syncError, otherwise automatically disabled. i.e SYNC_TEMPORARY_DISABLED)
        SYNCING = 3,
        FAILED = 4, // being deleted
        TEMPORARY_DISABLED = 5,
        UNKNOWN = 6,
    };

    class PendingTransferInfo
    {
    public:
        long long totalBytes = 0.;
        long long transferredBytes = 0.;
    };



public:
    HeartBeatSyncInfo(int tag, handle id);

    MEGA_DISABLE_COPY(HeartBeatSyncInfo)

    handle heartBeatId() const;

    Command *runningCommand() const;
    void setRunningCommand(Command *runningCommand);

    Status status() const;

    double progress() const;

    long long pendingUps() const;

    long long pendingDowns() const;

    m_time_t lastAction() const;

    mega::MegaHandle lastSyncedItem() const;

    void updateTransferInfo(MegaTransfer *transfer);
    void removePendingTransfer(MegaTransfer *transfer);

private:
    handle mHeartBeatId = UNDEF; //sync ID, from registration
    int mSyncTag = 0; //sync tag

    Status mStatus = UNKNOWN;

    long long mTotalBytes = 0;
    long long mTransferredBytes = 0;

    long long mPendingUps;
    long long mPendingDowns;

    std::map<int, std::unique_ptr<PendingTransferInfo>> mPendingTransfers;

    mega::MegaHandle mLastSyncedItem; //last synced item

    m_time_t mLastAction = 0; //timestamps of the last action
    m_time_t mLastBeat = 0; //timestamps of the last beat

    Command *mRunningCommand = nullptr;

    void updateLastActionTime();

public:
    void onCommandToBeDeleted(Command *command) override;
    m_time_t lastBeat() const;
    void setLastBeat(const m_time_t &lastBeat);
    void setLastAction(const m_time_t &lastAction); //TODO: call this for all the other setters!
    void setStatus(const Status &status);
    void setPendingUps(long long pendingUps);
    void setPendingDowns(long long pendingDowns);
    void setLastSyncedItem(const mega::MegaHandle &lastSyncedItem);
    void setTotalBytes(long long value);
    void setTransferredBytes(long long value);
};


class MegaHeartBeatMonitor : public MegaListener
{
public:
    explicit MegaHeartBeatMonitor(MegaClient * client);
    ~MegaHeartBeatMonitor() override;
    void beat(); //produce heartbeats!

private:
    std::map<int, std::shared_ptr<HeartBeatSyncInfo>> mHeartBeatedSyncs; //Map matching sync tag and HeartBeatSyncInfo
    mega::MegaClient *mClient = nullptr;

    std::deque<int> mPendingBackupPuts;

    void updateOrRegisterSync(MegaSync *sync);
    int getHBStatus (MegaSync *sync);
    int getHBSubstatus (MegaSync *sync);
    string getHBExtraData(MegaSync *sync);

    BackupType getHBType(MegaSync *sync);

    m_time_t mLastBeat = 0;
    std::shared_ptr<HeartBeatSyncInfo> getSyncTagByTransfer(MegaTransfer *transfer);
public:
    void reset();

    void setRegisteredId(handle id);

    void onSyncAdded(MegaApi *api, MegaSync *sync, int additionState) override;
    void onSyncDeleted(MegaApi *api, MegaSync *sync) override;
    void onSyncStateChanged(MegaApi *api, MegaSync *sync) override;

    void onTransferStart(MegaApi *api, MegaTransfer *transfer) override;
    void onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error) override;
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer) override;

    void onGlobalSyncStateChanged(MegaApi *api) override {}
    void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, std::string *localPath, int newState) override {}
    void onSyncEvent(MegaApi *api, MegaSync *sync, MegaSyncEvent *event) override {}
    void onSyncDisabled(MegaApi *api, MegaSync *sync) override {}
    void onSyncEnabled(MegaApi *api, MegaSync *sync) override {}

};
}

