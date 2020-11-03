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

#include "types.h"
#include "megaapi.h"
#include "mega.h"
#include <memory>
#include <functional>

namespace mega
{

/**
 * @brief The HeartBeatBackupInfo class
 * This class holds the information that will be heartbeated
 */
class HeartBeatBackupInfo : public CommandListener
{
public:
    HeartBeatBackupInfo(handle backupId);
    HeartBeatBackupInfo(HeartBeatBackupInfo&&) = default;
    HeartBeatBackupInfo& operator=(HeartBeatBackupInfo&&) = default;
    virtual ~HeartBeatBackupInfo() = default;

    MEGA_DISABLE_COPY(HeartBeatBackupInfo)

    virtual handle backupId() const;

    virtual int status() const;

    virtual double progress() const;
    virtual void invalidateProgress();

    virtual uint32_t pendingUps() const;

    virtual uint32_t pendingDowns() const;

    virtual m_time_t lastAction() const;

    virtual mega::MegaHandle lastItemUpdated() const;

    virtual Command *runningCommand() const;
    virtual void setRunningCommand(Command *runningCommand);

    virtual void onCommandToBeDeleted(Command *command) override;
    virtual m_time_t lastBeat() const;
    virtual void setLastBeat(const m_time_t &lastBeat);
    virtual void setLastAction(const m_time_t &lastAction);
    virtual void setStatus(const int &status);
    virtual void setProgress(const double &progress);
    virtual void setPendingUps(uint32_t pendingUps);
    virtual void setPendingDowns(uint32_t pendingDowns);
    virtual void setLastSyncedItem(const mega::MegaHandle &lastItemUpdated);
    virtual void setBackupId(const handle &backupId);

    virtual void updateStatus(mega::MegaClient *client) {}

protected:
    handle mBackupId = UNDEF;   // assigned by API upon registration

    int mStatus = 0;
    double mProgress = 0;
    bool mProgressInvalid = true;


    uint32_t mPendingUps = 0;
    uint32_t mPendingDowns = 0;

    mega::MegaHandle mLastItemUpdated = INVALID_HANDLE; // handle of node most recently updated

    m_time_t mLastAction = -1;   //timestamps of the last action
    m_time_t mLastBeat = -1;     //timestamps of the last beat

    Command *mRunningCommand = nullptr;

    void updateLastActionTime();
};

/**
 * @brief Backup info controlled by transfers
 * progress is advanced with transfer progress
 * by holding the count of total and transferred bytes
 *
 */
class HeartBeatTransferProgressedInfo : public HeartBeatBackupInfo
{
public:
    HeartBeatTransferProgressedInfo(handle backupId);
    MEGA_DISABLE_COPY(HeartBeatTransferProgressedInfo)

    virtual void updateTransferInfo(MegaTransfer *transfer);
    virtual void removePendingTransfer(MegaTransfer *transfer);
    virtual void clearFinshedTransfers();
    virtual void setTotalBytes(long long value);
    virtual void setTransferredBytes(long long value);

    double progress() const override;

private:

    long long mTotalBytes = 0;
    long long mTransferredBytes = 0;


    class PendingTransferInfo
    {
    public:
        long long mTotalBytes = 0;
        long long mTransferredBytes = 0;
    };
    std::map<int, std::unique_ptr<PendingTransferInfo>> mPendingTransfers;
    std::vector<std::unique_ptr<PendingTransferInfo>> mFinishedTransfers;
};

#ifdef ENABLE_SYNC
class HeartBeatSyncInfo : public HeartBeatTransferProgressedInfo
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

    HeartBeatSyncInfo(int tag, handle backupId);
    MEGA_DISABLE_COPY(HeartBeatSyncInfo)

    virtual int syncTag() const;
    virtual void updateStatus(mega::MegaClient *client) override;
private:
    int mSyncTag = 0;           // assigned by client (locally) for synced folders

};
#endif

/**
 * @brief Information for registration/update of a backup
 */
class MegaBackupInfo
{
public:
    MegaBackupInfo(BackupType type, string localFolder, handle megaHandle, int state, int substate, string extra, handle backupId = UNDEF);

    BackupType type() const;

    handle backupId() const;

    string localFolder() const;

    handle megaHandle() const;

    int state() const;

    int subState() const;

    string extra() const;

    void setBackupId(const handle &backupId);

protected:
    BackupType mType;
    handle mBackupId;
    string mLocalFolder;
    handle mMegaHandle;
    int mState;
    int mSubState;
    string mExtra;
};

#ifdef ENABLE_SYNC
class MegaBackupInfoSync : public MegaBackupInfo
{
public:
    enum State
    {
        ACTIVE = 1,             // Working fine (enabled)
        FAILED = 2,             // Failed (permanently disabled)
        TEMPORARY_DISABLED = 3, // Temporarily disabled due to a transient situation (e.g: account blocked). Will be resumed when the condition passes
        DISABLED = 4,           // Disabled by the user
        PAUSE_UP = 5,           // Active but upload transfers paused in the SDK
        PAUSE_DOWN = 6,         // Active but download transfers paused in the SDK
        PAUSE_FULL = 7,         // Active but transfers paused in the SDK
    };
    MegaBackupInfoSync(mega::MegaClient *client, const MegaSync &sync, handle backupid = UNDEF);

    void updatePauseState(MegaClient *client);

    static BackupType getSyncType(MegaClient *client, const MegaSync &sync);
    static int getSyncState (MegaClient *client, const MegaSync &sync);
    static int getSyncSubstatus (const MegaSync &sync);
    string getSyncExtraData(const MegaSync &sync);

private:
    static int calculatePauseActiveState(MegaClient *client);
};
#endif

class MegaBackupMonitor : public MegaListener
{
public:
    explicit MegaBackupMonitor(MegaClient * client);

    void beat(); // produce heartbeats!

    void digestPutResult(handle backupId);  // called at MegaApiImpl::backupput_result() <-- new backup registered
#ifdef ENABLE_SYNC
    void onSyncAdded(MegaApi *api, MegaSync *sync, int additionState) override;
    void onSyncDeleted(MegaApi *api, MegaSync *sync) override;
    void onSyncStateChanged(MegaApi *api, MegaSync *sync) override;
#endif
    void onPauseStateChanged(MegaApi *api);
    void onTransferStart(MegaApi *api, MegaTransfer *transfer) override;
    void onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error) override;
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer) override;

private:

    static constexpr int MAX_HEARBEAT_SECS_DELAY = 60*30; // max time to wait before a heartbeat for unchanged backup

    mega::MegaClient *mClient = nullptr;
    std::deque<std::function<void(handle)>> mPendingBackupPutCallbacks; // Callbacks to be executed when backupId received after a registering "sp"

    void updateBackupInfo(const MegaBackupInfo &info);
    void registerBackupInfo(const MegaBackupInfo &info);

    void beatBackupInfo(const std::shared_ptr<HeartBeatBackupInfo> &hbs);
    void calculateStatus(HeartBeatBackupInfo *hbs);

    std::shared_ptr<HeartBeatTransferProgressedInfo> getHeartBeatBackupInfoByTransfer(MegaApi *api, MegaTransfer *transfer);

#ifdef ENABLE_SYNC
    // --- Members and methods for syncs. i.e: backups of type: TWO_WAY, UP_SYNC, DOWN_SYNC
    std::map<int, std::shared_ptr<HeartBeatSyncInfo>> mHeartBeatedSyncs; // Map matching sync tag and HeartBeatBackupInfo
    std::map<int, int> mTransferToSyncMap; // maps transfer-tag and sync-tag to avoid costly search every update
    std::set<int> mPendingSyncPuts; // tags of registrations in-flight (waiting for CommandBackupPut's response)
    std::map<int, std::unique_ptr<MegaBackupInfo>> mPendingSyncUpdates; // updates that were received while CommandBackupPut was being resolved

    void updateOrRegisterSync(MegaSync *sync);


    void onSyncBackupRegistered(int syncTag, handle backupId);
#endif

};
}

