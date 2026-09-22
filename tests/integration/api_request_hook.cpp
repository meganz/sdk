#include "api_request_hook.h"

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED

#include "mega.h"

#include <algorithm>
#include <utility>

using namespace mega;

namespace sdk_test
{

std::vector<std::string> splitApiCommands(const std::string& payload)
{
    std::vector<std::string> commands;
    int depth = 0;
    size_t start = 0;
    bool inString = false;

    for (size_t i = 0; i < payload.size(); ++i)
    {
        const char c = payload[i];
        if (inString)
        {
            if (c == '\\')
                ++i;
            else if (c == '"')
                inString = false;
        }
        else if (c == '"')
        {
            inString = true;
        }
        else if (c == '{' || c == '[')
        {
            if (++depth == 2)
                start = i + 1;
        }
        else if (c == '}' || c == ']')
        {
            if (depth-- == 2)
                commands.emplace_back(payload, start, i - start);
            else if (depth < 0)
                break;
        }
    }

    return commands;
}

std::string commandStringArg(const std::string& command, const std::string& argName)
{
    const std::string argToken{"\"" + argName + "\":\""};
    const auto tokenPos = command.find(argToken);
    if (tokenPos == std::string::npos)
        return {};

    const auto valueStart = tokenPos + argToken.size();
    const auto valueEnd = command.find('"', valueStart);
    if (valueEnd == std::string::npos)
        return {};

    return command.substr(valueStart, valueEnd - valueStart);
}

ScopedHttpReqPostHook::ScopedHttpReqPostHook(Hook hook):
    mHook{std::move(hook)},
    mPrev{globalMegaTestHooks.onHttpReqPost}
{
    globalMegaTestHooks.onHttpReqPost = [this](HttpReq* req)
    {
        return (mPrev && mPrev(req)) || (mHook && mHook(req));
    };
}

ScopedHttpReqPostHook::~ScopedHttpReqPostHook()
{
    globalMegaTestHooks.onHttpReqPost = std::move(mPrev);
}

ScopedApiErrorInjector::ScopedApiErrorInjector(const error e,
                                               std::optional<ExtraInfo> extraInfo,
                                               CommandPredicate accept):
    mError{e},
    mExtraInfo{std::move(extraInfo)},
    mAccept{std::move(accept)},
    mHook{[this](HttpReq* req)
          {
              return injectError(req);
          }}
{}

bool ScopedApiErrorInjector::injectError(HttpReq* const req)
{
    if (!req || req->type != REQ_JSON || !req->out)
        return false;

    const auto commands = splitApiCommands(*req->out);
    if (!mAccept || std::none_of(commands.begin(), commands.end(), mAccept))
        return false;

    // An error with extra info is an object; a bare one is just its code. Command::checkError
    // reads both, per command.
    const std::string answer = mExtraInfo ?
                                   "{\"err\":" + std::to_string(static_cast<int>(mError)) +
                                       ",\"u\":" + std::to_string(mExtraInfo->userStatus) +
                                       ",\"l\":" + std::to_string(mExtraInfo->linkStatus) + "}" :
                                   std::to_string(static_cast<int>(mError));

    std::string reply{"["};
    for (size_t i = 0; i < commands.size(); ++i)
    {
        if (i)
            reply += ',';
        reply += answer;
    }
    reply += ']';

    LOG_info << "SIMULATING ERROR " << mError << " for an API request with " << commands.size()
             << " command(s): " << reply;

    req->in = std::move(reply);
    req->httpstatus = 200;
    req->contentlength = static_cast<m_off_t>(req->in.size());
    req->bufpos = req->contentlength;
    req->status = REQ_SUCCESS;
    mFired = true;
    return true;
}

} // namespace sdk_test

#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED
