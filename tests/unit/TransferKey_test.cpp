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
#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "mega/transfer.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <new>

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
