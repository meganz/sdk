/**
 * @file SdkTestSyncNodesOperations.h
 * @brief This file is expected to contain SdkTestSyncNodesOperations declaration.
 */

#ifdef ENABLE_SYNC

#ifndef INCLUDE_INTEGRATION_SDKTESTSYNCNODEOPERATIONS_H_
#define INCLUDE_INTEGRATION_SDKTESTSYNCNODEOPERATIONS_H_

#include "megautils.h"
#include "SdkTestNodesSetUp.h"

namespace sdk_test
{

/**
 * @class SdkTestSyncNodesOperations
 * @brief Implementation of SdkTestNodesSetUp that can be used for different test suites testing
 * syncs and node operations.
 *
 * @note As a reminder, everything is done inside the remote node named by getRootTestDir() which
 * means that all the methods involving a remote "path" are relative to that root test dir.
 */
class SdkTestSyncNodesOperations: public SdkTestNodesSetUp
{
public:
    static constexpr auto COMMON_TIMEOUT = 3min;
    static const std::string DEFAULT_SYNC_REMOTE_PATH;

    void SetUp() override;

    void TearDown() override;

    virtual bool createSyncOnSetup() const
    {
        return true;
    }

    /**
     * @brief Build a simple file tree
     */
    const std::vector<NodeInfo>& getElements() const override;

    const std::string& getRootTestDir() const override;

    /**
     * @brief We don't want different creation times
     */
    bool keepDifferentCreationTimes() override
    {
        return false;
    }

    /**
     * @brief Where should we put our sync locally?
     */
    const fs::path& getLocalTmpDir() const;

    /**
     * @brief Get a UTF-8 string from getLocalTmpDir().
     */
    std::string getLocalTmpDirU8string() const;

    /**
     * @brief Returns the identifier to get the sync from the megaApi
     */
    handle getBackupId() const
    {
        return mBackupId;
    }

    /**
     * @brief Returns the current sync state
     */
    std::unique_ptr<MegaSync> getSync() const;

    /**
     * @brief Moves the cloud node that is in the relative path "sourcePath" to the relative
     * "destPath"
     */
    void moveRemoteNode(const std::string& sourcePath, const std::string& destPath);

    /**
     * @brief Renames the remote node located at sourcePath with the new given name
     */
    void renameRemoteNode(const std::string& sourcePath, const std::string& newName);

    /**
     * @brief Removes the node located at the give relative path
     */
    void removeRemoteNode(const std::string& path);

    /**
     * @brief Asserts there is a sync pointing to the remote relative path and that it is in
     * RUNSTATE_RUNNING
     */
    void ensureSyncNodeIsRunning(const std::string& path);

    void suspendSync();

    void disableSync();

    void resumeSync();

    /**
     * @brief Asserts that the sync last known remote folder matches with the one give relative path
     */
    void ensureSyncLastKnownMegaFolder(const std::string& path);

    void initiateSync(const std::string& localPath,
                      const std::string& remotePath,
                      MegaHandle& backupId);

    /**
     * @brief Waits until all direct successors from both remote and local roots of the sync match.
     *
     * Asserts false if a timeout is exceeded.
     */
    void waitForSyncToMatchCloudAndLocal();

    void checkCurrentLocalMatchesOriginal(const std::string_view cloudDirName);

    /**
     * @brief Asserts that there are 2 stall issues pointing to local paths that end with the given
     * names and their reason is LocalAndRemotePreviouslyUnsyncedDiffer_userMustChoose.
     *
     * Useful to validate mirroring state between dir1 and dir2.
     */
    void thereIsAStall(const std::string_view fileName) const;

    /**
     * @brief Asserts that the local sync directory contains all the files matching a mirroring
     * state (all the files in dir1 merged with those in dir2)
     */
    void checkCurrentLocalMatchesMirror() const;

    /**
     * @brief Returns a vector with the names of the first successor files/directories inside the
     * local root.
     *
     * Hidden files (starting with . are excludoed)
     */
    std::vector<std::string> getLocalFirstChildrenNames() const;

protected:
    /**
     * @brief Constructs a tmp path using the thread id for thread safety.
     */
    static const fs::path& localTmpPath();

    LocalTempDir mTempLocalDir{localTmpPath()};
    handle mBackupId{UNDEF};
};

} // namespace sdk_test

#endif // INCLUDE_INTEGRATION_SDKTESTSYNCNODEOPERATIONS_H_
#endif // ENABLE_SYNC
