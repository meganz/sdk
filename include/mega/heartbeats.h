/**
 * @file heartbeats.h
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
public:
    enum Status
    {
        UPTODATE = 1, // Up to date: local and remote paths are in sync
        SYNCING = 2, // The sync engine is working, transfers are in progress
        PENDING = 3, // The sync engine is working, e.g: scanning local folders
        INACTIVE = 4, // Sync is not active. A state != ACTIVE should have been sent through '''sp'''
        UNKNOWN = 5, // Unknown status
    };

    HeartBeatSyncInfo(int tag, handle id);

    MEGA_DISABLE_COPY(HeartBeatSyncInfo)

    handle heartBeatId() const;

    Command *runningCommand() const;
    void setRunningCommand(Command *runningCommand);

    Status status() const;

    double progress() const;

    uint32_t pendingUps() const;

    uint32_t pendingDowns() const;

    m_time_t lastAction() const;

    mega::MegaHandle lastSyncedItem() const;

    void updateTransferInfo(MegaTransfer *transfer);
    void removePendingTransfer(MegaTransfer *transfer);
    void clearFinshedTransfers();

    void onCommandToBeDeleted(Command *command) override;
    m_time_t lastBeat() const;
    void setLastBeat(const m_time_t &lastBeat);
    void setLastAction(const m_time_t &lastAction);
    void setStatus(const Status &status);
    void setPendingUps(uint32_t pendingUps);
    void setPendingDowns(uint32_t pendingDowns);
    void setLastSyncedItem(const mega::MegaHandle &lastSyncedItem);
    void setTotalBytes(long long value);
    void setTransferredBytes(long long value);
    int syncTag() const;
    void setHeartBeatId(const handle &heartBeatId);

private:

    class PendingTransferInfo
    {
    public:
        long long totalBytes = 0;
        long long transferredBytes = 0;
    };

    handle mHeartBeatId = UNDEF; //sync ID, from registration
    int mSyncTag = 0; //sync tag

    Status mStatus = UNKNOWN;

    long long mTotalBytes = 0;
    long long mTransferredBytes = 0;

    uint32_t mPendingUps = 0;
    uint32_t mPendingDowns = 0;

    std::map<int, std::unique_ptr<PendingTransferInfo>> mPendingTransfers;
    std::vector<std::unique_ptr<PendingTransferInfo>> mFinishedTransfers;

    mega::MegaHandle mLastSyncedItem; //last synced item

    m_time_t mLastAction = 0; //timestamps of the last action
    m_time_t mLastBeat = 0; //timestamps of the last beat

    Command *mRunningCommand = nullptr;

    void updateLastActionTime();

};


class MegaHeartBeatMonitor : public MegaListener
{
    enum State
    {
        ACTIVE = 1, // Working fine (enabled)
        FAILED = 2, // Failed (permanently disabled)
        TEMPORARY_DISABLED = 3, // Temporarily disabled due to a transient situation (e.g: account blocked). Will be resumed when the condition passes
        DISABLED = 4, // Disabled by the user
        UNKNOWN = 5, // Unknown state
    };

    static constexpr int MAX_HEARBEAT_SECS_DELAY = 60*30; //max time to wait to update unchanged sync
public:
    explicit MegaHeartBeatMonitor(MegaClient * client);
    ~MegaHeartBeatMonitor() override;
    void beat(); //produce heartbeats!

private:
    std::map<int, std::shared_ptr<HeartBeatSyncInfo>> mHeartBeatedSyncs; //Map matching sync tag and HeartBeatSyncInfo
    mega::MegaClient *mClient = nullptr;
    std::map<int, int> mTransferToSyncMap; //Map matching transfer tag and sync tag

    std::deque<int> mPendingBackupPuts; //to store tags of syncs pending response of CommandBackupPut
    std::map<int, std::unique_ptr<MegaSync>> mPendingSyncUpdates; // to store updates that were received while CommandBackupPut was being resolved

    void updateOrRegisterSync(MegaSync *sync);
    void updateSyncInfo(handle syncID, MegaSync *sync);

    int getHBState (MegaSync *sync);
    int getHBSubstatus (MegaSync *sync);
    string getHBExtraData(MegaSync *sync);

    BackupType getHBType(MegaSync *sync);

    m_time_t mLastBeat = 0;
    std::shared_ptr<HeartBeatSyncInfo> getSyncHeartBeatInfoByTransfer(MegaTransfer *transfer);
    void calculateStatus(HeartBeatSyncInfo *hbs);
public:
    void reset();

    void digestPutResult(handle id);

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

