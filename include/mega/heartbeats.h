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
#include <memory>
#include <functional>
#include "mega/command.h"

namespace mega
{

#ifdef ENABLE_SYNC
struct UnifiedSync;
struct Syncs;

/**
 * @brief The HeartBeatBackupInfo class
 * This class holds the information that will be heartbeated
 */

class HeartBeatBackupInfo
{
    bool mModified = false;

public:
    HeartBeatBackupInfo();
    HeartBeatBackupInfo(HeartBeatBackupInfo&&) = delete;
    HeartBeatBackupInfo& operator=(HeartBeatBackupInfo&&) = delete;
    virtual ~HeartBeatBackupInfo() = default;

    virtual m_time_t lastAction() const;

    virtual handle lastItemUpdated() const;

    virtual m_time_t lastBeat() const;
    virtual void setLastBeat(const m_time_t &lastBeat);
    virtual void setLastAction(const m_time_t &lastAction);
    virtual void setLastSyncedItem(const handle &lastItemUpdated);

    std::atomic<bool> mSending{false};

protected:
    handle mLastItemUpdated = UNDEF; // handle of node most recently updated

    m_time_t mLastAction = -1;   //timestamps of the last action
    m_time_t mLastBeat = -1;     //timestamps of the last beat

    void updateLastActionTime();

    friend class BackupMonitor;
};

class HeartBeatSyncInfo : public HeartBeatBackupInfo
{
public:
    void updateSPHBStatus(UnifiedSync& us);

    using SPHBStatus = CommandBackupPutHeartBeat::SPHBStatus;
    SPHBStatus sphbStatus() { return mSPHBStatus; }

    SyncTransferCounts mSnapshotTransferCounts;
    SyncTransferCounts mResolvedTransferCounts;
private:
    SPHBStatus mSPHBStatus = CommandBackupPutHeartBeat::STATE_NOT_INITIALIZED;
};

class BackupInfoSync : public CommandBackupPut::BackupInfo
{
public:

    BackupInfoSync(const SyncConfig& config, const string& device, handle drive, CommandBackupPut::SPState calculatedState);
    BackupInfoSync(const UnifiedSync& us, bool pauseDown, bool pauseUp);

    static BackupType getSyncType(const SyncConfig& config);
    static CommandBackupPut::SPState getSyncState (const UnifiedSync &, bool pauseDown, bool pauseUp);
    static CommandBackupPut::SPState getSyncState(SyncError error, SyncRunState, bool pauseDown, bool pauseUp);
    static CommandBackupPut::SPState getSyncState(const SyncConfig& config, bool pauseDown, bool pauseUp);
    static handle getDriveId(const UnifiedSync&);

    bool operator==(const BackupInfoSync& o) const;
    bool operator!=(const BackupInfoSync& o) const;

private:
    static CommandBackupPut::SPState calculatePauseActiveState(bool pauseDown, bool pauseUp);
};


class BackupMonitor
{
public:
    explicit BackupMonitor(Syncs&);

    void beat(); // produce heartbeats!

    void updateOrRegisterSync(UnifiedSync&);

private:
    static constexpr int MAX_HEARBEAT_SECS_DELAY = 60*30; // max time to wait before a heartbeat for unchanged backup

    Syncs& syncs;

    void beatBackupInfo(UnifiedSync& us);
};

#endif

}

