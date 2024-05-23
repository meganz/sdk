#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <set>

#include <mega/fuse/common/error_or_forward.h>
#include <mega/fuse/common/type_traits.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

class PendingCallbacks
{
    class Context;

    // Convenience.
    using ContextPtr = std::shared_ptr<Context>;
    using ContextSet = std::set<ContextPtr>;

    // Represents a cancellable callback.
    class Context
    {
        // The pending callbacks instance that contains us.
        PendingCallbacks& mPendingCallbacks;

    protected:
        // Constructs a new instance.
        explicit Context(PendingCallbacks& pendingCallbacks);

    public:
        // Destroys an existing instance.
        virtual ~Context();

        // Cancel the callback represented by this context.
        virtual void cancel() = 0;

        // Remove this context from the pending callbacks instance that owns it.
        bool remove(const ContextPtr& context);
    }; // Context

    // Represents a specific kind of cancellable callback.
    template<typename T>
    class SpecificContext
      : public Context
    {
        // Sanity.
        static_assert(IsErrorLike<T>::value, "");

        // The callback this context represents.
        std::function<void(T)> mCallback;

    public:
        // Constructs a new instance.
        SpecificContext(std::function<void(T)> callback,
                        PendingCallbacks& pendingCallbacks)
          : Context(pendingCallbacks)
          , mCallback(std::move(callback))
        {
            // Sanity.
            assert(mCallback);
        }

        // Cancels the callback represented by this context.
        void cancel() override
        {
            invoke(API_EINCOMPLETE);
        }

        // Invoke the callback represented by this context.
        void invoke(T result)
        {
            // Latch callback.
            auto callback = std::move(mCallback);

            // Forward result to callback.
            callback(std::move(result));
        }
    }; // SpecificContext<T>

    // Convenience.
    template<typename T>
    using SpecificContextWeakPtr = std::weak_ptr<SpecificContext<T>>;

    // Tracks any pending contexts.
    ContextSet mContexts;

    // Serializes access to mCallbacks.
    std::mutex mLock;

public:
    // Construct a new instance.
    PendingCallbacks();

    // Destroy an existing instance.
    ~PendingCallbacks();

    // Cancel any pending callbacks.
    void cancel();

    // Wrap a callback such that it can be cancelled.
    template<typename T, typename U = IsErrorLike<T>>
    auto wrap(std::function<void(T)> callback)
      -> typename std::enable_if<U::value, std::function<void(T)>>::type
    {
        // Sanity.
        assert(callback);

        // Wrap callback in a context.
        auto context = std::make_shared<SpecificContext<T>>(
                         std::move(callback),
                         *this);

        // Forwards result to the callback's context.
        auto wrapper = [](SpecificContextWeakPtr<T> cookie,
                          T result) {
            // Check if the context is still alive.
            auto context = cookie.lock();

            // Context isn't alive.
            if (!context)
                return;

            // Remove the context from its owner.
            auto removed = context->remove(context);

            // Context was already removed.
            if (!removed)
                return;

            // Forward result to wrapped user callback.
            context->invoke(std::move(result));
        }; // wrapper

        // Wrap context in another callback.
        callback = std::bind(std::move(wrapper),
                             context,
                             std::placeholders::_1);

        // Acquire lock.
        std::lock_guard<std::mutex> guard(mLock);

        // Add context to pending context set.
        mContexts.emplace(std::move(context));

        // Return wrapper to caller.
        return callback;
    }
}; // PendingCallbacks

} // fuse
} // mega

