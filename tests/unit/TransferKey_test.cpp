/**
 * @file TransferKey_test.cpp
 * @brief Upload crypto material of a new Transfer: transferkey and ctriv start zeroed, so the WS
 *        preflight (MegaClient::prepareUploadForWs), which treats a zero transferkey as "not
 *        generated yet", always gives a fresh upload its own random key and CTR IV.
 *
 * Every Transfer here is built over storage filled with a non-zero pattern, so the cells do not
 * depend on what the allocator hands out.
 *
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 */

#include "mega/crypto/cryptopp.h"
#include "mega/file.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "mega/transfer.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <system_error>

namespace
{

constexpr unsigned char kPoison = 0x5A;

// Storage for one Transfer, filled with kPoison before every construction.
class RecycledTransferStorage
{
public:
    RecycledTransferStorage():
        mRaw(::operator new(sizeof(mega::Transfer)))
    {}

    ~RecycledTransferStorage()
    {
        destroy();
        ::operator delete(mRaw);
    }

    RecycledTransferStorage(const RecycledTransferStorage&) = delete;
    RecycledTransferStorage& operator=(const RecycledTransferStorage&) = delete;

    // Constructs a Transfer over poisoned storage.
    mega::Transfer& constructPoisoned(mega::MegaClient& client)
    {
        destroy();
        std::memset(mRaw, kPoison, sizeof(mega::Transfer));
        return constructInPlace(client);
    }

    // Ends the current Transfer and constructs a new one in the same storage, without
    // re-poisoning: the new Transfer starts over the bytes the finished one left behind.
    mega::Transfer& reincarnate(mega::MegaClient& client)
    {
        destroy();
        return constructInPlace(client);
    }

private:
    mega::Transfer& constructInPlace(mega::MegaClient& client)
    {
        mTransfer = new (mRaw) mega::Transfer(&client, mega::PUT);
        return *mTransfer;
    }

    void destroy()
    {
        if (mTransfer)
        {
            mTransfer->~Transfer();
            mTransfer = nullptr;
        }
    }

    void* mRaw;
    mega::Transfer* mTransfer = nullptr;
};

bool isZeroKey(const mega::Transfer& t)
{
    return mega::SymmCipher::isZeroKey(t.transferkey.data(), mega::SymmCipher::KEYLENGTH);
}

bool isPoisonKey(const mega::Transfer& t)
{
    return std::all_of(t.transferkey.begin(),
                       t.transferkey.end(),
                       [](const mega::byte b)
                       {
                           return b == kPoison;
                       });
}

using TransferKeyBytes = std::array<mega::byte, mega::SymmCipher::KEYLENGTH>;

// A small local file for the cells that go through MegaClient::startxfer.
class ScopedLocalFile
{
public:
    explicit ScopedLocalFile(const std::string& name):
        mPath(std::filesystem::temp_directory_path() / name)
    {
        std::ofstream out(mPath, std::ios::binary | std::ios::trunc);
        out << std::string(1024, 'x');
    }

    ~ScopedLocalFile()
    {
        std::error_code ec;
        std::filesystem::remove(mPath, ec);
    }

    ScopedLocalFile(const ScopedLocalFile&) = delete;
    ScopedLocalFile& operator=(const ScopedLocalFile&) = delete;

    // Replaces the file's content, as an edit made while the app was closed.
    void rewrite(const std::string& content) const
    {
        std::ofstream out(mPath, std::ios::binary | std::ios::trunc);
        out << content;
    }

    mega::LocalPath localPath() const
    {
        return mega::LocalPath::fromAbsolutePath(mPath.string());
    }

private:
    std::filesystem::path mPath;
};

// Puts a prepared, partly uploaded PUT transfer for `file` into the client's cached transfers,
// as Transfer::unserialize restores it, and resumes it through startxfer. `beforeResume`, if
// given, runs after the transfer is cached and before startxfer. Returns the transfer `file` ends
// up attached to (the client owns it; destroying `file` removes it).
mega::Transfer* resumeCachedPreparedUpload(mega::MegaClient& client,
                                           mega::File& file,
                                           const mega::LocalPath& localPath,
                                           const TransferKeyBytes& key,
                                           const int64_t iv,
                                           const std::function<void()>& beforeResume = {})
{
    file.setLocalname(localPath);
    file.name = "transferkey_test_upload.bin";
    file.targetuser = mega::MegaClient::SUPPORT_USER_HANDLE; // needs no target node
    {
        auto fa = client.fsaccess->newfileaccess();
        if (!fa->fopen(localPath, mega::OPEN_RDONLY, mega::FSLogging::logOnError))
        {
            ADD_FAILURE() << "harness: cannot open the local file";
            return nullptr;
        }
        file.genfingerprint(fa.get());
    }

    auto* cached = new mega::Transfer(&client, mega::PUT);
    *static_cast<mega::FileFingerprint*>(cached) = static_cast<const mega::FileFingerprint&>(file);
    cached->localfilename = localPath;
    cached->transferkey = key;
    cached->ctriv = iv;
    cached->ws_fileno = 7;
    cached->ws_session_url = "wss://transferkey.test.invalid/session";
    cached->ultoken = std::make_unique<mega::UploadToken>();
    cached->pos = mega::SymmCipher::BLOCKSIZE;
    {
        // One confirmed chunk MAC.
        TransferKeyBytes macKey{};
        mega::SymmCipher cipher(macKey.data());
        std::array<mega::byte, mega::SymmCipher::BLOCKSIZE> block{};
        cached->chunkmacs
            .ctr_encrypt(0, &cipher, block.data(), static_cast<unsigned>(block.size()), 0, 0, true);
    }
    cached->lastaccesstime = mega::m_time();
    cached->priority = mega::TransferList::PRIORITY_START + mega::TransferList::PRIORITY_STEP;
    client.multi_cachedtransfers[mega::PUT].insert({cached, cached});

    if (beforeResume)
    {
        beforeResume();
    }

    mega::error e = mega::API_OK;
    mega::TransferDbCommitter committer(client.tctable);
    const bool started = client.startxfer(mega::PUT,
                                          &file,
                                          committer,
                                          false,
                                          false,
                                          true,
                                          mega::NoVersioning,
                                          &e,
                                          client.nextreqtag());
    EXPECT_TRUE(started) << "harness: startxfer failed with " << e;
    EXPECT_EQ(file.transfer, cached) << "harness: startxfer must adopt the cached transfer";
    return file.transfer;
}

} // namespace

TEST(TransferKey, NewTransferOverRecycledStorageHasZeroKeyAndIv)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    RecycledTransferStorage storage;

    const mega::Transfer& t = storage.constructPoisoned(*client);

    EXPECT_TRUE(isZeroKey(t)) << "transferkey is not zero-initialised";
    EXPECT_EQ(t.ctriv, 0);
}

// A prepared upload always carries the random CTR IV generated together with its key, so a
// cached prepared upload with IV 0 never had a key generated (a remote-copy transfer, for
// example, is persisted prepared but keyless). Resuming it must keep nothing sent under that
// key and must leave the transfer unprepared, so that it gets a fresh key and IV like any new
// upload.
TEST(TransferKey, ResumeRestartsACachedPreparedUploadWhoseKeyWasNeverGenerated)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    const ScopedLocalFile localFile("transferkey_test_resume_nokey.bin");
    mega::File file;

    TransferKeyBytes persistedKey{};
    persistedKey.fill(kPoison);
    const mega::Transfer* t =
        resumeCachedPreparedUpload(*client, file, localFile.localPath(), persistedKey, 0);
    ASSERT_NE(t, nullptr);

    EXPECT_TRUE(t->localfilename.empty()) << "the upload is still marked as prepared";
    EXPECT_FALSE(isPoisonKey(*t)) << "the upload kept a key that was never generated";
    EXPECT_EQ(t->chunkmacs.size(), 0u);
    EXPECT_EQ(t->pos, 0);
    EXPECT_EQ(t->ultoken, nullptr);
    EXPECT_EQ(t->ws_fileno, 0u);
    EXPECT_TRUE(t->ws_session_url.empty());
}

// The restart above still checks the local file first: when the file changed while the app was
// closed, the restarted upload takes the file's current fingerprint, so it is not failed as
// modified when it starts.
TEST(TransferKey, ResumeRestartUsesTheCurrentFingerprintOfAModifiedFile)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    const ScopedLocalFile localFile("transferkey_test_resume_modified.bin");
    mega::File file;

    TransferKeyBytes persistedKey{};
    persistedKey.fill(kPoison);
    const std::string newContent(2048, 'y');
    const mega::Transfer* t = resumeCachedPreparedUpload(*client,
                                                         file,
                                                         localFile.localPath(),
                                                         persistedKey,
                                                         0,
                                                         [&localFile, &newContent]
                                                         {
                                                             localFile.rewrite(newContent);
                                                         });
    ASSERT_NE(t, nullptr);

    EXPECT_TRUE(t->localfilename.empty()) << "the upload is still marked as prepared";
    EXPECT_EQ(t->size, static_cast<int64_t>(newContent.size()))
        << "the restarted upload kept the fingerprint of the previous file content";
}

// GUARD (passes before and after the fix): a cached prepared upload with a generated key and
// IV resumes with its key, IV and progress.
TEST(TransferKey, ResumeKeepsACachedPreparedUploadWithAGeneratedKeyAndIv)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    const ScopedLocalFile localFile("transferkey_test_resume_key.bin");
    mega::File file;

    TransferKeyBytes persistedKey{};
    for (std::size_t i = 0; i < persistedKey.size(); ++i)
    {
        persistedKey[i] = static_cast<mega::byte>(i + 1);
    }
    constexpr int64_t persistedIv = 0x0102030405060708;
    const mega::Transfer* t =
        resumeCachedPreparedUpload(*client, file, localFile.localPath(), persistedKey, persistedIv);
    ASSERT_NE(t, nullptr);

    EXPECT_FALSE(t->localfilename.empty());
    EXPECT_EQ(t->transferkey, persistedKey);
    EXPECT_EQ(t->ctriv, persistedIv);
    EXPECT_EQ(t->chunkmacs.size(), 1u);
    EXPECT_NE(t->ultoken, nullptr);
    EXPECT_EQ(t->ws_fileno, 7u);
    EXPECT_FALSE(t->ws_session_url.empty());
}

#ifdef MEGA_USE_WSUPLOAD

// prepareUploadForWs generates the key first; with no File attached it then defers (returns
// false) before any file I/O, which keeps these cells offline and I/O-free.

TEST(TransferKey, WsPreflightGeneratesKeyAndIvForTransferOverRecycledStorage)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    RecycledTransferStorage storage;

    mega::Transfer& t = storage.constructPoisoned(*client);
    EXPECT_FALSE(client->prepareUploadForWs(t)) << "harness: preflight must defer (no File)";

    EXPECT_FALSE(isPoisonKey(t)) << "WS preflight did not generate the key";
    EXPECT_FALSE(isZeroKey(t));
    EXPECT_NE(t.ctriv, 0) << "WS preflight left the CTR IV at 0";
}

TEST(TransferKey, WsPreflightGivesEachTransferItsOwnKeyAndIv)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    RecycledTransferStorage storage;

    mega::Transfer& first = storage.constructPoisoned(*client);
    EXPECT_FALSE(client->prepareUploadForWs(first));
    const auto firstKey = first.transferkey;
    const auto firstIv = first.ctriv;

    mega::Transfer& second = storage.reincarnate(*client);
    ASSERT_EQ(&second, &first) << "harness: the second transfer must reuse the first's storage";
    EXPECT_FALSE(client->prepareUploadForWs(second));

    EXPECT_NE(second.transferkey, firstKey) << "WS preflight did not generate a new key";
    EXPECT_NE(second.ctriv, 0) << "WS preflight left the CTR IV at 0";
    EXPECT_FALSE(second.transferkey == firstKey && second.ctriv == firstIv)
        << "two uploads share key and CTR IV";
}

// A transfer persisted to the transfer cache before it was prepared (no localfilename) carries
// whatever transferkey it was serialized with. Nothing was uploaded under that key, so the
// preflight must generate a fresh one.
TEST(TransferKey, WsPreflightReplacesAKeyPersistedBeforePreparation)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    RecycledTransferStorage storage;

    mega::Transfer& t = storage.constructPoisoned(*client);
    t.transferkey.fill(kPoison); // as Transfer::unserialize restores it
    t.ctriv = 0;
    ASSERT_TRUE(t.localfilename.empty());

    EXPECT_FALSE(client->prepareUploadForWs(t));

    EXPECT_FALSE(isPoisonKey(t)) << "WS preflight kept a key persisted before preparation";
    EXPECT_NE(t.ctriv, 0) << "WS preflight left the CTR IV at 0";
}

// A transfer can be prepared without a key: a remote-copy transfer gets its localfilename from
// File::prepare and is persisted like that. If it is later uploaded for real, the zero-key test
// is the only thing that gives it a key.
TEST(TransferKey, WsPreflightGeneratesKeyAndIvForAPreparedTransferWithoutKey)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    RecycledTransferStorage storage;

    mega::Transfer& t = storage.constructPoisoned(*client);
    t.localfilename = mega::LocalPath::fromAbsolutePath("transferkey_test_missing_file.bin");
    ASSERT_TRUE(isZeroKey(t)) << "a new transfer does not start with a zero key";

    EXPECT_FALSE(client->prepareUploadForWs(t));

    EXPECT_FALSE(isZeroKey(t)) << "WS preflight left a prepared transfer without a key";
    EXPECT_NE(t.ctriv, 0) << "WS preflight left the CTR IV at 0";
}

// GUARD (passes before and after the fix): a prepared transfer restored from the cache keeps its
// persisted key and IV, which the bytes already on the server were encrypted with. The local
// file does not exist, so the preflight fails after the key step.
TEST(TransferKey, WsPreflightKeepsAPreparedRestoredTransfersKeyAndIv)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    RecycledTransferStorage storage;

    mega::Transfer& t = storage.constructPoisoned(*client);
    std::array<mega::byte, mega::SymmCipher::KEYLENGTH> persistedKey{};
    for (std::size_t i = 0; i < persistedKey.size(); ++i)
    {
        persistedKey[i] = static_cast<mega::byte>(i + 1);
    }
    constexpr int64_t persistedIv = 0x0102030405060708;
    // As Transfer::unserialize restores them.
    t.localfilename = mega::LocalPath::fromAbsolutePath("transferkey_test_missing_file.bin");
    t.transferkey = persistedKey;
    t.ctriv = persistedIv;

    EXPECT_FALSE(client->prepareUploadForWs(t));

    EXPECT_EQ(t.transferkey, persistedKey) << "WS preflight replaced a restored key";
    EXPECT_EQ(t.ctriv, persistedIv) << "WS preflight replaced a restored CTR IV";
}

#endif // MEGA_USE_WSUPLOAD
