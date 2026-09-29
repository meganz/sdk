/**
 * @file WsUploadFilenoHarness.h
 * @brief Shared harness for the SDK-6600 WS fileno cells (WsUploadFileno_test.cpp, and
 *        WsUploadFilenoSeed_test.cpp once UploadEngine::reserveFileNos exists): synthetic PUT
 *        transfers, real UploadEngines that never reach the network, and the exhaustive
 *        small-scope fileno model.
 *
 * The invariant under test: a wss:// upload URL receives each fileno for at most one file,
 * across every engine generation that shares the persisted transfer cache.
 *
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 */

#pragma once

#ifdef MEGA_USE_WSUPLOAD

#include "utils.h"

#include <gtest/gtest.h>
#include <mega/crypto/cryptopp.h>
#include <mega/file.h>
#include <mega/megaclient.h>
#include <mega/testhooks.h>
#include <mega/transfer.h>
#include <mega/utils.h>
#include <mega/wsupload.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mt::wsfileno
{

inline constexpr m_off_t kFileSize = 12 * 1024 * 1024;
// Two whole chunks confirmed (128 KiB + 256 KiB on the chunk lattice): a genuine resume head.
inline constexpr m_off_t kResumePos = 393216;

// A synthetic PUT transfer that owns its File. The Transfer is destroyed first (~Transfer
// clears file->transfer and notifies the file), then the File.
class SyntheticUpload
{
public:
    SyntheticUpload(mega::MegaClient& client, const std::string& localName):
        mFile(std::make_unique<mega::File>()),
        mTransfer(std::make_unique<mega::Transfer>(&client, mega::PUT))
    {
        attach(localName);
    }

    ~SyntheticUpload()
    {
        mTransfer.reset();
        mFile.reset();
    }

    SyntheticUpload(const SyntheticUpload&) = delete;
    SyntheticUpload& operator=(const SyntheticUpload&) = delete;

    mega::Transfer& transfer() noexcept
    {
        return *mTransfer;
    }

    const mega::Transfer& transfer() const noexcept
    {
        return *mTransfer;
    }

    // Ends this transfer and constructs a NEW one in the same storage, for another local file: a
    // restored transfer that lives at a finished transfer's address, as any allocator may hand
    // that address out again. Returns the new transfer (same address as the old one).
    mega::Transfer& reincarnate(mega::MegaClient& client, const std::string& localName)
    {
        mega::Transfer* const storage = mTransfer.release();
        storage->~Transfer();
        mTransfer.reset(new (storage) mega::Transfer(&client, mega::PUT));
        attach(localName);
        return *mTransfer;
    }

    // Transfer-cache state as Transfer::unserialize restores it: fileno and session URL and,
    // when pos > 0, the resume head (pos, progress and a confirmed chunk MAC).
    void setPersisted(const std::uint32_t fileno, const std::string& url, const m_off_t pos)
    {
        mega::Transfer& t = *mTransfer;
        t.ws_fileno = fileno;
        t.ws_session_url = url;
        setResumeHead(pos);
    }

    // What Transfer::failed leaves for a whole-file retry, or pos > 0 for a resume head.
    void setResumeHead(const m_off_t pos)
    {
        mega::Transfer& t = *mTransfer;
        t.pos = pos;
        t.setProgresscompleted(pos);
        t.chunkmacs.clear();
        if (pos > 0)
        {
            std::array<mega::byte, mega::SymmCipher::KEYLENGTH> key{};
            mega::SymmCipher cipher(key.data());
            std::array<mega::byte, mega::SymmCipher::BLOCKSIZE> block{};
            t.chunkmacs.ctr_encrypt(0,
                                    &cipher,
                                    block.data(),
                                    static_cast<unsigned>(block.size()),
                                    0,
                                    0,
                                    true);
        }
    }

private:
    void attach(const std::string& localName)
    {
        const auto path = mega::LocalPath::fromAbsolutePath(localName);
        mFile->setLocalname(path);
        mFile->size = kFileSize;
        mFile->mtime = 1700000000;
        mTransfer->localfilename = path;
        mTransfer->size = kFileSize;
        mTransfer->files.push_back(mFile.get());
    }

    std::unique_ptr<mega::File> mFile;
    std::unique_ptr<mega::Transfer> mTransfer;
};

// A real engine that builds pinned pools but never lets a pool worker run, for the single-shot
// cells. start() is the only place that sets poolMgr.mImpl (without it ensurePinnedPool is a
// no-op); stop() right after means every pool worker a later enqueue spawns exits on its first
// loop test (ws_pool.cpp, !stopping()), before it creates any connection. Offline: start()'s
// refreshPools only queues a client-thread action that this client never runs (its HttpIO is a
// stub), and the manager thread polls an empty curl multi and skips checkPools while paused.
// setMaxConnections(1) keeps it to one worker per pinned pool.
// The manager thread's run() sets uploadThreadRunning = true on entry, so a stop() that lands
// before that store is overwritten and the thread never exits (pre-existing engine race; the
// destructor's join then hangs). Hence: wait until the manager is live, then stop() until it has
// left run(). Deterministic: run() sets the flag once, so the first stop() after it wins.
inline std::unique_ptr<mega::ws::UploadEngine> makeStartedQuiescedEngine(mega::MegaClient& client)
{
    auto engine = std::make_unique<mega::ws::UploadEngine>(client);
    engine->setMaxConnections(1);
    engine->start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!engine->managerThreadLive() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    do
    {
        engine->stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    while (engine->managerThreadLive() && std::chrono::steady_clock::now() < deadline);
    if (engine->managerThreadLive())
    {
        ADD_FAILURE() << "harness: the engine manager thread did not leave run() within 30 s";
    }
    return engine;
}

// A never-started engine, for the model replays (thousands of engines): enqueue()/remove() and
// the fileno allocation work exactly as on a started engine, but it creates no pools (see above)
// and runs no thread.
inline std::unique_ptr<mega::ws::UploadEngine> makeUnstartedEngine(mega::MegaClient& client)
{
    return std::make_unique<mega::ws::UploadEngine>(client);
}

// Whether a pinned pool exists for `url`; nullopt when the build has no test hooks
// (UploadEngine::getPoolStateForTesting is compiled out there).
inline std::optional<bool> pinnedPoolExists(const mega::ws::UploadEngine& engine,
                                            const std::string& url)
{
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    mega::ws::UploadEngine::PoolStateForTesting state;
    return engine.getPoolStateForTesting(url, state) && state.found && state.pinned;
#else
    (void)engine;
    (void)url;
    return std::nullopt;
#endif
}

// ---------- Exhaustive small-scope fileno model over the REAL engine ----------
//
// Operations over <= 3 file identities (preconditions in brackets):
//   new(i)        [i idle]      fresh upload, enqueued and sent: it starts and persists its
//                               (fileno, URL) and a mid-file head, as onStart + onProgress do.
//   queue(i)      [i idle]      fresh upload, enqueued but not started before the next restart:
//                               its fileno is persisted without a URL.
//   done(i)       [i live]      completion or cancel: removed from the engine and the cache.
//   restart()     [<= R times]  process death or locallogout: every live upload keeps its
//                               persisted state and a NEW engine is built.
//   seed()        [deferred]    the transfer-cache load seeds the engine (reserveFileNos with
//                               the highest persisted fileno). Seeded models apply it inside
//                               restart(); with deferSeed it is an op of its own, so new
//                               uploads (e.g. support uploads, which need no parent node) may
//                               be enqueued before it. Restores always wait for it.
//   restore(i)    [i persisted] transfer-cache restore: re-enqueued with the persisted state and
//                               sent; restores interleave freely with new uploads (Timeline A).
//   invalidate(i) [i restored]  invalidatePinnedSessionUrl: the file stays in the engine with
//                               its fileno but loses its pinned URL (and head), so it is sent on
//                               the generation's normal pool. No-op if it has no pinned URL.
// A send feeds (effective URL, fileno): the effective URL is the transfer's URL when non-empty
// (its pinned pool), else the generation's normal-pool URL (USC URLs are unique per reply).
// Invariant: no pair is ever fed to two file identities. Never-used identities are
// interchangeable, so only the lowest one may start (symmetry reduction).

enum class ModelOp : std::uint8_t
{
    New,
    Queue,
    Done,
    Restart,
    Seed,
    Restore,
    Invalidate,
};

struct ModelStep
{
    ModelOp op;
    unsigned file;
};

struct FilenoModelConfig
{
    unsigned files = 3;
    unsigned steps = 6;
    unsigned maxRestarts = 2;
    // Seeded variants: invoked with the highest persisted fileno on every restarted engine,
    // inside restart() or, with deferSeed, as the seed() op.
    std::function<void(mega::ws::UploadEngine&, std::uint32_t)> seed;
    bool deferSeed = false;
};

struct FilenoModelReport
{
    std::size_t sequences = 0;
    std::size_t violatingSequences = 0;
    std::size_t sends = 0;
    std::size_t restores = 0;
    std::size_t restoresWithUrl = 0;
    std::size_t invalidations = 0;
    // New uploads enqueued before the seed of their generation (deferSeed only).
    std::size_t uploadsBeforeSeed = 0;
    // Model-side precondition probes, taken before the enqueue under test: a restored fileno
    // that is live (mechanism a), that this engine already handed to another, now finished
    // file (claimed), or that is URL-less while its pair on this generation's URL was already
    // fed to another file (mechanism g).
    std::size_t collidingRestores = 0;
    std::size_t claimedRestores = 0;
    std::size_t reusedPairRestores = 0;
    // Restored (fileno, URL, head) that did not survive enqueue unchanged.
    std::size_t resumeLost = 0;
    // The engine left two live transfers with one fileno, or a zero fileno.
    std::size_t liveDuplicates = 0;
    std::string firstViolation;
    std::string firstResumeLoss;
    std::string firstLiveDuplicate;

    std::string summary() const
    {
        std::ostringstream os;
        os << "sequences=" << sequences << " violating=" << violatingSequences << " sends=" << sends
           << " restores=" << restores << " restoresWithUrl=" << restoresWithUrl
           << " invalidations=" << invalidations << " uploadsBeforeSeed=" << uploadsBeforeSeed
           << " collidingRestores=" << collidingRestores << " claimedRestores=" << claimedRestores
           << " reusedPairRestores=" << reusedPairRestores << " resumeLost=" << resumeLost
           << " liveDuplicates=" << liveDuplicates;
        return os.str();
    }
};

// Model depth: `fallback` unless MEGA_TEST_WSFILENO_MODEL_STEPS asks for another one, clamped
// to [5, 9] (5 is the shortest sequence reaching mechanism g; 9 takes minutes).
inline unsigned modelSteps(const unsigned fallback)
{
    const auto [value, isSet] = mega::Utils::getenv("MEGA_TEST_WSFILENO_MODEL_STEPS");
    if (!isSet || value.empty())
    {
        return fallback;
    }
    const unsigned long requested = std::strtoul(value.c_str(), nullptr, 10);
    return static_cast<unsigned>(std::clamp<unsigned long>(requested, 5, 9));
}

namespace detail
{

enum class Slot : std::uint8_t
{
    Unused,
    Idle,
    Live,
    LiveRestored,
    Persisted,
};

struct PersistedState
{
    std::uint32_t fileno = 0;
    std::string url;
    m_off_t pos = 0;
};

struct ModelFile
{
    std::unique_ptr<SyntheticUpload> upload; // non-null while live in the current engine
    std::optional<PersistedState> persisted; // set while awaiting restore
};

inline std::string describe(const std::vector<ModelStep>& sequence)
{
    std::ostringstream os;
    for (const ModelStep& step: sequence)
    {
        switch (step.op)
        {
            case ModelOp::New:
                os << "new(" << step.file << ") ";
                break;
            case ModelOp::Queue:
                os << "queue(" << step.file << ") ";
                break;
            case ModelOp::Done:
                os << "done(" << step.file << ") ";
                break;
            case ModelOp::Restart:
                os << "restart() ";
                break;
            case ModelOp::Seed:
                os << "seed() ";
                break;
            case ModelOp::Restore:
                os << "restore(" << step.file << ") ";
                break;
            case ModelOp::Invalidate:
                os << "invalidate(" << step.file << ") ";
                break;
        }
    }
    return os.str();
}

// Depth-first over the op tree on an abstract per-file status (no engine involved).
// `seeded`: restores are allowed in the current generation (the seed ran, or none is due).
inline void enumerate(const FilenoModelConfig& config,
                      std::vector<ModelStep>& prefix,
                      std::vector<Slot>& slots,
                      const unsigned restarts,
                      const bool seeded,
                      const std::function<void(const std::vector<ModelStep>&)>& onSequence)
{
    if (prefix.size() == config.steps)
    {
        onSequence(prefix);
        return;
    }

    const auto recurse =
        [&](const ModelStep step, const unsigned restartsAfter, const bool seededAfter)
    {
        prefix.push_back(step);
        enumerate(config, prefix, slots, restartsAfter, seededAfter, onSequence);
        prefix.pop_back();
    };

    const auto firstUnused = static_cast<std::size_t>(
        std::find(slots.begin(), slots.end(), Slot::Unused) - slots.begin());
    for (unsigned i = 0; i < slots.size(); ++i)
    {
        const Slot before = slots[i];
        if (before == Slot::Idle || (before == Slot::Unused && i == firstUnused))
        {
            slots[i] = Slot::Live;
            recurse({ModelOp::New, i}, restarts, seeded);
            recurse({ModelOp::Queue, i}, restarts, seeded);
        }
        else if (before == Slot::Live || before == Slot::LiveRestored)
        {
            slots[i] = Slot::Idle;
            recurse({ModelOp::Done, i}, restarts, seeded);
            if (before == Slot::LiveRestored)
            {
                slots[i] = Slot::Live;
                recurse({ModelOp::Invalidate, i}, restarts, seeded);
            }
        }
        else if (before == Slot::Persisted && seeded)
        {
            slots[i] = Slot::LiveRestored;
            recurse({ModelOp::Restore, i}, restarts, seeded);
        }
        slots[i] = before;
    }

    if (!seeded)
    {
        recurse({ModelOp::Seed, 0}, restarts, true);
    }

    if (restarts < config.maxRestarts)
    {
        const std::vector<Slot> saved = slots;
        for (Slot& slot: slots)
        {
            if (slot == Slot::Live || slot == Slot::LiveRestored)
            {
                slot = Slot::Persisted;
            }
        }
        recurse({ModelOp::Restart, 0}, restarts + 1, !(config.seed && config.deferSeed));
        std::copy(saved.begin(), saved.end(), slots.begin());
    }
}

inline void replay(mega::MegaClient& client,
                   const FilenoModelConfig& config,
                   const std::vector<ModelStep>& sequence,
                   FilenoModelReport& report)
{
    unsigned generation = 1;
    bool seedPending = false;
    auto engine = makeUnstartedEngine(client);
    std::vector<ModelFile> files(config.files);
    // Every (URL, fileno) pair ever fed, and the file it was fed to (all generations).
    std::map<std::pair<std::string, std::uint32_t>, unsigned> fedTo;
    // Filenos the current engine left on a transfer after enqueue, and the file it was.
    std::map<std::uint32_t, unsigned> handedOut;
    bool violated = false;

    const auto generationUrl = [&generation]()
    {
        return "wss://model.example/ul/gen" + std::to_string(generation);
    };

    const auto startUpload = [&client, &files](const unsigned i) -> mega::Transfer&
    {
        files[i].upload =
            std::make_unique<SyntheticUpload>(client,
                                              "wsfileno_model_" + std::to_string(i) + ".bin");
        return files[i].upload->transfer();
    };

    const auto persistedHighest = [&files]()
    {
        std::uint32_t highest = 0;
        for (const ModelFile& file: files)
        {
            if (file.persisted)
            {
                highest = std::max(highest, file.persisted->fileno);
            }
        }
        return highest;
    };

    const auto afterEnqueue = [&](const unsigned i)
    {
        const std::uint32_t fileno = files[i].upload->transfer().ws_fileno;
        handedOut[fileno] = i;
        bool duplicate = fileno == 0;
        for (unsigned j = 0; j < files.size(); ++j)
        {
            duplicate = duplicate || (j != i && files[j].upload &&
                                      files[j].upload->transfer().ws_fileno == fileno);
        }
        if (duplicate)
        {
            ++report.liveDuplicates;
            if (report.firstLiveDuplicate.empty())
            {
                report.firstLiveDuplicate = describe(sequence) + ":: file " + std::to_string(i) +
                                            " got the live-duplicate or zero fileno " +
                                            std::to_string(fileno);
            }
        }
    };

    const auto send = [&](const unsigned i)
    {
        mega::Transfer& t = files[i].upload->transfer();
        const std::string url = t.ws_session_url.empty() ? generationUrl() : t.ws_session_url;
        ++report.sends;
        const auto [it, inserted] = fedTo.emplace(std::make_pair(url, t.ws_fileno), i);
        if (!inserted && it->second != i && !violated)
        {
            violated = true;
            if (report.firstViolation.empty())
            {
                std::ostringstream os;
                os << describe(sequence) << ":: (" << url << ", fileno " << t.ws_fileno
                   << ") fed to file " << it->second << ", then to file " << i;
                report.firstViolation = os.str();
            }
        }
        // What onStart + onProgress persist once the upload is under way on that URL.
        files[i].upload->setPersisted(t.ws_fileno, url, kResumePos);
    };

    for (const ModelStep& step: sequence)
    {
        const unsigned i = step.file;
        switch (step.op)
        {
            case ModelOp::New:
            case ModelOp::Queue:
            {
                report.uploadsBeforeSeed += seedPending ? 1u : 0u;
                engine->enqueue(startUpload(i));
                afterEnqueue(i);
                if (step.op == ModelOp::New)
                {
                    send(i);
                }
                break;
            }
            case ModelOp::Done:
            {
                engine->remove(files[i].upload->transfer());
                files[i].upload.reset();
                break;
            }
            case ModelOp::Restart:
            {
                for (ModelFile& file: files)
                {
                    if (file.upload)
                    {
                        const mega::Transfer& t = file.upload->transfer();
                        file.persisted = PersistedState{t.ws_fileno, t.ws_session_url, t.pos};
                        engine->remove(file.upload->transfer());
                        file.upload.reset();
                    }
                }
                engine.reset();
                handedOut.clear();
                ++generation;
                engine = makeUnstartedEngine(client);
                seedPending = config.seed && config.deferSeed;
                if (config.seed && !config.deferSeed)
                {
                    config.seed(*engine, persistedHighest());
                }
                break;
            }
            case ModelOp::Seed:
            {
                config.seed(*engine, persistedHighest());
                seedPending = false;
                break;
            }
            case ModelOp::Restore:
            {
                const PersistedState persisted = *files[i].persisted;
                files[i].persisted.reset();
                ++report.restores;

                bool live = false;
                for (const ModelFile& other: files)
                {
                    live = live || (other.upload && persisted.fileno &&
                                    other.upload->transfer().ws_fileno == persisted.fileno);
                }
                const auto handed = handedOut.find(persisted.fileno);
                const bool claimed =
                    persisted.fileno && !live && handed != handedOut.end() && handed->second != i;
                const auto fed = fedTo.find(std::make_pair(generationUrl(), persisted.fileno));
                const bool reusedPair = persisted.fileno && persisted.url.empty() &&
                                        fed != fedTo.end() && fed->second != i;
                report.collidingRestores += live ? 1u : 0u;
                report.claimedRestores += claimed ? 1u : 0u;
                report.reusedPairRestores += reusedPair ? 1u : 0u;

                mega::Transfer& t = startUpload(i);
                files[i].upload->setPersisted(persisted.fileno, persisted.url, persisted.pos);
                engine->enqueue(t);
                if (!persisted.url.empty())
                {
                    ++report.restoresWithUrl;
                    if (t.ws_fileno != persisted.fileno || t.ws_session_url != persisted.url ||
                        t.pos != persisted.pos)
                    {
                        ++report.resumeLost;
                        if (report.firstResumeLoss.empty())
                        {
                            std::ostringstream os;
                            os << describe(sequence) << ":: file " << i << " persisted (fileno "
                               << persisted.fileno << ", " << persisted.url << ", pos "
                               << persisted.pos << ") became (fileno " << t.ws_fileno << ", '"
                               << t.ws_session_url << "', pos " << t.pos << ")";
                            report.firstResumeLoss = os.str();
                        }
                    }
                }
                afterEnqueue(i);
                send(i);
                break;
            }
            case ModelOp::Invalidate:
            {
                const mega::Transfer& t = files[i].upload->transfer();
                if (!t.ws_session_url.empty() && t.ws_session_url != generationUrl())
                {
                    ++report.invalidations;
                    // The engine keeps the file and its fileno; the client side clears URL,
                    // chunk MACs, pos and progress, so the file restarts on a normal pool.
                    files[i].upload->setPersisted(t.ws_fileno, std::string{}, 0);
                    send(i);
                }
                break;
            }
        }
    }

    for (ModelFile& file: files)
    {
        if (file.upload)
        {
            engine->remove(file.upload->transfer());
            file.upload.reset();
        }
    }
    report.violatingSequences += violated ? 1u : 0u;
}

} // namespace detail

inline FilenoModelReport exploreFilenoModel(mega::MegaClient& client,
                                            const FilenoModelConfig& config)
{
    FilenoModelReport report;
    std::vector<ModelStep> prefix;
    std::vector<detail::Slot> slots(config.files, detail::Slot::Unused);
    detail::enumerate(config,
                      prefix,
                      slots,
                      0,
                      true,
                      [&](const std::vector<ModelStep>& sequence)
                      {
                          ++report.sequences;
                          detail::replay(client, config, sequence, report);
                      });
    return report;
}

} // namespace mt::wsfileno

#endif // MEGA_USE_WSUPLOAD
