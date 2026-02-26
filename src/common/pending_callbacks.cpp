#include <utility>

#include <mega/common/pending_callbacks.h>

namespace mega
{
namespace common
{


PendingCallbacks::Context::Context(PendingCallbacks& pendingCallbacks)
  : mPendingCallbacks(pendingCallbacks)
{
}

PendingCallbacks::Context::~Context() = default;

bool PendingCallbacks::Context::remove(const ContextPtr& context)
{
    // Acquire lock.
    std::lock_guard<std::mutex> guard(mPendingCallbacks.mLock);

    // Try and remove this context.
    return mPendingCallbacks.mContexts.erase(context) > 0;
}

PendingCallbacks::PendingCallbacks()
  : mContexts()
  , mLock()
{
}

PendingCallbacks::~PendingCallbacks()
{
    // Cancel any outstanding contexts.
    cancel();
}

void PendingCallbacks::cancel()
{
    // Cancel contexts until we hit a steady state.
    while (true)
    {
        ContextSet contexts;

        // Take ownership of context set.
        {
            std::lock_guard<std::mutex> guard(mLock);
            std::swap(contexts, mContexts);
        }

        // No contexts left to cancel.
        if (contexts.empty())
            return;

        // Cancel each wrapped callback in turn.
        for (auto& context : contexts)
            context->cancel();
    }
}

} // common
} // mega

