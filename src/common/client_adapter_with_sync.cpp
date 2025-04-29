#include <future>

#include <mega/common/client_adapter.h>
#include <mega/common/normalized_path.h>

#include <mega/megaclient.h>

namespace mega
{
namespace common
{

void ClientAdapter::desynchronize(mega::handle id)
{
    // So we can wait for the client's result.
    std::promise<void> notifier;

    // Remove the sync on the client thread.
    execute([&](const Task& task) {
        // Client's being torn down.
        if (task.cancelled())
            return notifier.set_value();

        // So we can use set_value(...) as our completion function.
        auto completion =
          std::bind(&std::promise<void>::set_value, &notifier);

        // Ask the client to remove our sync.
        mClient.syncs.deregisterThenRemoveSyncById(id, std::move(completion));
    });

    // Wait for the client to process our request.
    notifier.get_future().get();
}


bool ClientAdapter::mountable(const NormalizedPath& path) const
{
    // Check if the sync described by us is related to path.
    auto isRelated = [&path](const UnifiedSync& us) {
        return path.related(us.mConfig.mLocalPath);
    }; // isRelated

    // Are any syncs related to path?
    auto anyRelated = mClient.syncs.anySyncMatching(isRelated);

    // Path is only mountable if it is unrelated to any active sync.
    return !anyRelated;
}


auto ClientAdapter::synchronize(const NormalizedPath& path, NodeHandle target)
  -> std::tuple<mega::handle, Error, SyncError>
{
    // Convenience.
    using Result = decltype(synchronize(NormalizedPath(), NodeHandle()));

    std::promise<Result> notifier;

    // Transmit a result to our notifier.
    auto notify = [&](mega::handle handle,
                      Error error,
                      SyncError syncError) {
        return notifier.set_value({handle, error, syncError});
    }; // notify

    // Add the new sync on the client thread.
    execute([&](const Task& task) {
        // Client's being torn down.
        if (task.cancelled())
            return notify(UNDEF, API_EINCOMPLETE, NO_SYNC_ERROR);

        // So we can use notify as our completion function.
        auto completion = std::bind(std::move(notify),
                                    std::placeholders::_3,
                                    std::placeholders::_1,
                                    std::placeholders::_2);

        // Populate sync config object.
        auto config = SyncConfig(path,
                                 std::string(),
                                 target,
                                 std::string(),
                                 fsfp_t(),
                                 LocalPath());

        // Ask the client to add our new sync.
        mClient.addsync(std::move(config),
                        std::move(completion),
                        std::string(),
                        std::string());
    });

    // Return the client's result to our caller.
    return notifier.get_future().get();
}

} // common
} // mega

