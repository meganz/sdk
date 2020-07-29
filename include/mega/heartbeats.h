/**
 * @file heartbeats.h
 * @brief Classes for heartbeating Backup configuration and status
 *
 * (c) 2020 by Mega Limited, Auckland, New Zealand
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
 * @brief The HeartBeatBackupInfo class
 * This class holds the information that will be heartbeated
 */
class HeartBeatBackupInfo : public CommandListener
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

    HeartBeatBackupInfo(int tag, handle aBackupId);

    MEGA_DISABLE_COPY(HeartBeatBackupInfo)

    handle backupId() const;

    Command *runningCommand() const;
    void setRunningCommand(Command *runningCommand);

    Status status() const;

    double progress() const;

    uint32_t pendingUps() const;

    uint32_t pendingDowns() const;

    m_time_t lastAction() const;

    mega::MegaHandle lastItemUpdated() const;

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
    void setLastSyncedItem(const mega::MegaHandle &lastItemUpdated);
    void setTotalBytes(long long value);
    void setTransferredBytes(long long value);
    int syncTag() const;
    void setBackupId(const handle &backupId);

private:

    class PendingTransferInfo
    {
    public:
        long long mTotalBytes = 0;
        long long mTransferredBytes = 0;
    };

    handle mBackupId = UNDEF;   // assigned by API upon registration
    int mSyncTag = 0;           // assigned by client (locally) for synced folders
    // TODO: add other ids for different backup types: Camera Uploads, Backup from MegaApi...

    Status mStatus = UNKNOWN;

    long long mTotalBytes = 0;
    long long mTransferredBytes = 0;

    uint32_t mPendingUps = 0;
    uint32_t mPendingDowns = 0;

    std::map<int, std::unique_ptr<PendingTransferInfo>> mPendingTransfers;
    std::vector<std::unique_ptr<PendingTransferInfo>> mFinishedTransfers;

    mega::MegaHandle mLastItemUpdated = INVALID_HANDLE; // handle of node most recently updated

    m_time_t mLastAction = 0;   //timestamps of the last action
    m_time_t mLastBeat = 0;     //timestamps of the last beat

    Command *mRunningCommand = nullptr;

    void updateLastActionTime();
};


class MegaBackupMonitor : public MegaListener
{
public:
    explicit MegaBackupMonitor(MegaClient * client);
    virtual ~MegaBackupMonitor();
    void beat(); //produce heartbeats!

    void digestPutResult(handle backupId);

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

private:
    enum State
    {
        ACTIVE = 1,             // Working fine (enabled)
        FAILED = 2,             // Failed (permanently disabled)
        TEMPORARY_DISABLED = 3, // Temporarily disabled due to a transient situation (e.g: account blocked). Will be resumed when the condition passes
        DISABLED = 4,           // Disabled by the user
        UNKNOWN = 5,            // Unknown state
    };

    static constexpr int MAX_HEARBEAT_SECS_DELAY = 60*30; // max time to wait before a heartbeat for unchanged backup

    mega::MegaClient *mClient = nullptr;
    std::deque<int> mPendingBackupPuts; // tags of hearbeats in-flight (waiting for CommandBackupPut's response)

    // --- Members and methods for backups of type: TWO_WAY, UP_SYNC, DOWN_SYNC
    std::map<int, std::shared_ptr<HeartBeatBackupInfo>> mHeartBeatedSyncs; // Map matching sync tag and HeartBeatBackupInfo
    std::map<int, int> mTransferToSyncMap; // maps transfer-tag and sync-tag
    std::map<int, std::unique_ptr<MegaSync>> mPendingSyncUpdates; // updates that were received while CommandBackupPut was being resolved

    void updateOrRegisterSync(MegaSync *sync);
    void updateSyncInfo(handle backupId, MegaSync *sync);
    int getHBState (MegaSync *sync);
    int getHBSubstatus (MegaSync *sync);
    string getHBExtraData(MegaSync *sync);
    BackupType getHBType(MegaSync *sync);

    m_time_t mLastBeat = 0;
    std::shared_ptr<HeartBeatBackupInfo> getHeartBeatBackupInfoByTransfer(MegaTransfer *transfer);
    void calculateStatus(HeartBeatBackupInfo *hbs);
};
}

