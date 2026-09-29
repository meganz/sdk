/**
 * @file
 * @brief Helpers for the tests that intercept API requests through
 * globalMegaTestHooks.onHttpReqPost: installing the hook for a scope, inspecting the commands of a
 * request payload, and answering requests with a simulated error.
 *
 * These utilities extend the ones defined in the more general level for the tests
 * (sdk_test_utils.h) so the namespace is extended (sdk_test).
 */

#ifndef INCLUDE_INTEGRATION_API_REQUEST_HOOK_H_
#define INCLUDE_INTEGRATION_API_REQUEST_HOOK_H_

#include "mega/testhooks.h"

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace sdk_test
{

/**
 * @brief The commands of an API request, i.e. the top level objects of its "[{...},{...}]" payload.
 *
 * Each command is returned without its enclosing braces, so it starts with its own first argument.
 */
std::vector<std::string> splitApiCommands(const std::string& payload);

/**
 * @brief The value of a string argument of a command, empty if it carries none.
 *
 * Only the first occurrence of the argument is considered, which is the command's own: the
 * arguments of the objects nested in it, e.g. the new nodes of a putnodes, are serialized after it.
 */
std::string commandStringArg(const std::string& command, const std::string& argName);

/**
 * @class ScopedHttpReqPostHook
 * @brief Installs an onHttpReqPost hook for as long as it lives.
 *
 * The hook already in place, if any, is offered every request first and stays in charge of the ones
 * it handles, and is put back on destruction.
 */
class ScopedHttpReqPostHook
{
public:
    using Hook = std::function<bool(::mega::HttpReq*)>;

    explicit ScopedHttpReqPostHook(Hook hook);
    ~ScopedHttpReqPostHook();

    // The installed hook holds a pointer to this object.
    ScopedHttpReqPostHook(const ScopedHttpReqPostHook&) = delete;
    ScopedHttpReqPostHook& operator=(const ScopedHttpReqPostHook&) = delete;

private:
    const Hook mHook;
    Hook mPrev;
};

/**
 * @class ScopedApiErrorInjector
 * @brief Answers the API requests carrying an accepted command with the given error, locally,
 * instead of letting them be sent.
 *
 * The whole batch is failed command by command, as the API would answer a request whose commands
 * failed individually: an error for the request as a whole would make the client log out.
 */
class ScopedApiErrorInjector
{
public:
    using CommandPredicate = std::function<bool(const std::string& command)>;

    ScopedApiErrorInjector(::mega::error e, CommandPredicate accept);

    // True once a request has been answered with the error.
    bool fired() const
    {
        return mFired.load();
    }

private:
    bool injectError(::mega::HttpReq* req);

    const ::mega::error mError;
    const CommandPredicate mAccept;
    std::atomic<bool> mFired{false};

    // Declared last so the hook, which holds a pointer to this object, is uninstalled before the
    // members it uses are destroyed.
    ScopedHttpReqPostHook mHook;
};

} // namespace sdk_test

#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED

#endif // INCLUDE_INTEGRATION_API_REQUEST_HOOK_H_
