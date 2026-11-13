/**
 * @file MetaMacCheck.h
 * @brief Strict upload-integrity oracles for the integration tests (SDK-6795).
 *
 * A download plus a hash check cannot see a node key whose meta-MAC misses chunk MACs: the bytes
 * on the server are intact, and the download-side tolerance (`checkMetaMacWithMissingLateEntries`)
 * accepts many such keys, logging only "Found mac gaps". The apps keep that tolerance; our tests do
 * not. A test that uploads checks the node's meta-MAC against the local file's, and fails if the
 * SDK logged a download gap match while it ran.
 */

#pragma once

#include "mega/filesystem.h"
#include "mega/logging.h"
#include "mega/utils.h"
#include "megaapi.h"
#include "megaapi_impl.h"
#include "sdk_test_utils.h"
#include "SdkTest_test.h"
#include "stdfs.h"
#include "test.h"

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace mega::test::integrity
{

/**
 * @brief Compares the meta-MAC stored in the node key with the meta-MAC of the local file.
 * Never tolerates gaps: any difference is a failure.
 */
inline ::testing::AssertionResult strictMetaMacMatches(::mega::MegaNode* node,
                                                       const fs::path& localPath)
{
    if (!node)
    {
        return ::testing::AssertionFailure() << "strict meta-MAC check: no node";
    }
    const std::string* nodeKey = node->getNodeKey();
    if (!nodeKey || nodeKey->size() != static_cast<size_t>(::mega::FILENODEKEYLENGTH))
    {
        return ::testing::AssertionFailure() << "strict meta-MAC check: node has no file key";
    }

    auto fsa = ::mega::createFSA();
    auto fa = fsa->newfileaccess();
    const auto local = ::mega::LocalPath::fromAbsolutePath(path_u8string(fs::absolute(localPath)));
    if (!fa->fopenForMacRead(local, ::mega::FSLogging::logOnError))
    {
        return ::testing::AssertionFailure()
               << "strict meta-MAC check: cannot open " << path_u8string(localPath);
    }
    const auto [localMac, remoteMac] =
        ::mega::genLocalAndRemoteMetaMac(fa.get(), *nodeKey, ::mega::FILENODE, std::nullopt);
    if (localMac == ::mega::INVALID_META_MAC)
    {
        return ::testing::AssertionFailure()
               << "strict meta-MAC check: local meta-MAC not computed";
    }
    if (localMac != remoteMac)
    {
        std::unique_ptr<char[]> handle(node->getBase64Handle());
        return ::testing::AssertionFailure()
               << "GAP KEY: node " << (handle ? handle.get() : "?") << " stores meta-MAC "
               << remoteMac << " but the local file's meta-MAC is " << localMac;
    }
    return ::testing::AssertionSuccess() << "meta-MAC " << localMac;
}

inline ::testing::AssertionResult strictMetaMacMatches(::mega::MegaApi& api,
                                                       ::mega::MegaHandle nodeHandle,
                                                       const fs::path& localPath)
{
    std::unique_ptr<::mega::MegaNode> node(api.getNodeByHandle(nodeHandle));
    return strictMetaMacMatches(node.get(), localPath);
}

/**
 * @brief Logger that forwards everything to the test logger and keeps the lines that show a gap
 * key: download gap matches ("Found mac gaps", "Download MAC matched with missing chunk MACs") and
 * upload-token gate lines ("Upload token with ... chunk MACs"). One instance lives for the whole
 * run (a worker thread may still log just after stop()).
 */
class MacGapLogWatch: public ::mega::Logger
{
public:
    static MacGapLogWatch& instance()
    {
        static auto* watch = new MacGapLogWatch(testOutputLogger());
        return *watch;
    }

    void start()
    {
        {
            std::lock_guard g{mMutex};
            mFoundMacGaps.clear();
            mUploadGateLines.clear();
        }
        ::mega::SimpleLogger::setOutputClass(this);
    }

    void stop()
    {
        ::mega::SimpleLogger::setOutputClass(&mForwardTo);
    }

    void log(const char* time,
             int loglevel,
             const char* source,
             const char* message
#ifdef ENABLE_LOG_PERFORMANCE
             ,
             const char** directMessages,
             size_t* directMessagesSizes,
             unsigned numberMessages
#endif
             ) override
    {
        std::string entry = message ? message : "";
#ifdef ENABLE_LOG_PERFORMANCE
        for (unsigned i = 0; i < numberMessages; ++i)
        {
            entry.append(directMessages[i], directMessagesSizes[i]);
        }
#endif
        if (entry.find("Found mac gaps") != std::string::npos ||
            entry.find("Download MAC matched with missing chunk MACs") != std::string::npos)
        {
            std::lock_guard g{mMutex};
            mFoundMacGaps.push_back(entry);
        }
        else if (entry.find("Upload token with") != std::string::npos &&
                 entry.find("chunk MACs") != std::string::npos)
        {
            std::lock_guard g{mMutex};
            mUploadGateLines.push_back(entry);
        }

        mForwardTo.log(time,
                       loglevel,
                       source,
                       message
#ifdef ENABLE_LOG_PERFORMANCE
                       ,
                       directMessages,
                       directMessagesSizes,
                       numberMessages
#endif
        );
    }

    std::vector<std::string> foundMacGaps() const
    {
        std::lock_guard g{mMutex};
        return mFoundMacGaps;
    }

    std::vector<std::string> uploadGateLines() const
    {
        std::lock_guard g{mMutex};
        return mUploadGateLines;
    }

private:
    explicit MacGapLogWatch(::mega::Logger& forwardTo):
        mForwardTo{forwardTo}
    {}

    ::mega::Logger& mForwardTo;
    mutable std::mutex mMutex;
    std::vector<std::string> mFoundMacGaps;
    std::vector<std::string> mUploadGateLines;
};

// Starts the watch for the lifetime of the object (one per test).
class ScopedMacGapLogWatch
{
public:
    ScopedMacGapLogWatch()
    {
        MacGapLogWatch::instance().start();
    }

    ~ScopedMacGapLogWatch()
    {
        MacGapLogWatch::instance().stop();
    }

    ScopedMacGapLogWatch(const ScopedMacGapLogWatch&) = delete;
    ScopedMacGapLogWatch& operator=(const ScopedMacGapLogWatch&) = delete;

    std::vector<std::string> foundMacGaps() const
    {
        return MacGapLogWatch::instance().foundMacGaps();
    }

    std::vector<std::string> uploadGateLines() const
    {
        return MacGapLogWatch::instance().uploadGateLines();
    }
};

inline std::string joinLines(const std::vector<std::string>& lines)
{
    std::string out;
    for (const auto& l: lines)
    {
        out += "\n  " + l;
    }
    return out;
}

// Downloads the node and returns {result, sha256 of the downloaded bytes or empty}.
inline std::pair<int, std::string> downloadAndHash(::mega::MegaApi& api,
                                                   ::mega::MegaNode* node,
                                                   const std::string& name)
{
    const fs::path downloadPath = fs::current_path() / ("dl_" + name);
    std::error_code ignore;
    fs::remove(downloadPath, ignore);
    TransferTracker dt(&api);
    api.startDownload(node,
                      path_u8string(downloadPath).c_str(),
                      nullptr,
                      nullptr,
                      false,
                      nullptr,
                      ::mega::MegaTransfer::COLLISION_CHECK_FINGERPRINT,
                      ::mega::MegaTransfer::COLLISION_RESOLUTION_NEW_WITH_N,
                      false,
                      &dt);
    const int result = dt.waitForResult(600);
    std::string hash =
        result == ::mega::API_OK ? sdk_test::hashFileHex(downloadPath) : std::string{};
    fs::remove(downloadPath, ignore);
    return {result, hash};
}

/**
 * @brief The integrity verdict for one uploaded file: strict meta-MAC, download, hash, and no
 * download gap match. Logs one [INTEGRITY] summary line.
 */
inline void expectFullMetaMac(::mega::MegaApi& api,
                              ::mega::MegaNode* node,
                              const fs::path& localPath,
                              const std::string& expectedHash,
                              const ScopedMacGapLogWatch& watch,
                              const std::string& summary)
{
    const auto strict = strictMetaMacMatches(node, localPath);
    const auto [dlRes, dlHash] = downloadAndHash(api, node, path_u8string(localPath.filename()));
    const auto gaps = watch.foundMacGaps();
    const auto gate = watch.uploadGateLines();
    LOG_warn << "[INTEGRITY] " << summary << " strictMac=" << static_cast<bool>(strict)
             << " download=" << dlRes << " hashEqual=" << (dlHash == expectedHash)
             << " foundMacGaps=" << gaps.size() << " uploadGateLines=" << gate.size();
    EXPECT_TRUE(strict);
    EXPECT_EQ(dlRes, ::mega::API_OK) << "download failed (API_EKEY = MAC verification failed)";
    EXPECT_EQ(dlHash, expectedHash) << "downloaded bytes differ from the source";
    EXPECT_TRUE(gaps.empty()) << "the download only verified through the MAC-gap tolerance:"
                              << joinLines(gaps);
}

} // namespace mega::test::integrity
