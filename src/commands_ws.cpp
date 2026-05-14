/**
 * @file commands_ws.cpp
 * @brief Command wrappers specific to the websocket-upload engine.
 */

#ifdef MEGA_USE_WSUPLOAD

#include "mega/commands_ws.h"

#include "mega/megaclient.h"

#include <utility>

namespace mega
{

CommandUSCForWsUpload::CommandUSCForWsUpload(MegaClient& client, Completion completion)
    : mCompletion(std::move(completion))
{
    cmd("usc");
    tag = client.reqtag;
    // USC is read-only and safe to run on the lockless request channel.
    mLockless = true;
}

bool CommandUSCForWsUpload::procresult(Result r, JSON& json)
{
    if (r.wasErrorOrOK())
    {
        if (r.wasError(API_OK))
            mCompletion(API_EINTERNAL, {});
        else
            mCompletion(r.errorOrOK(), {});
        return true;
    }

    if (!r.hasJsonArray())
    {
        mCompletion(API_EINTERNAL, {});
        return true;
    }

    std::vector<SizeClass> sizeClasses;

    auto peek = [](const JSON& j) -> char
    {
        const char* p = j.pos;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',' || *p == ':')
            ++p;
        return *p;
    };

    auto parseEntryArray = [&](JSON& j)
    {
        std::string host;
        std::string path;
        m_off_t maxSize = 0;

        const bool okHost = j.storeobject(&host);
        const bool okPath = j.storeobject(&path);
        if (okHost && okPath)
        {
            if (j.isnumeric())
            {
                maxSize = j.getint();
            }

            // Ignore any extra fields we don't currently understand.
            while (j.storeobject())
                ;

            std::string url = "wss://";
            url.append(host);
            url.append("/");
            url.append(path);
            sizeClasses.emplace_back(std::move(url), maxSize);
        }
        else
        {
            while (j.storeobject())
                ;
        }
    };

    std::function<void(JSON&)> parseArrayContents;
    parseArrayContents = [&](JSON& j)
    {
        const char next = peek(j);
        if (next == ']')
        {
            return;
        }

        if (next == '[')
        {
            while (j.enterarray())
            {
                parseArrayContents(j);
                j.leavearray();
            }
            return;
        }

        if (next != '"')
        {
            while (j.storeobject())
                ;
            return;
        }

        parseEntryArray(j);
    };

    // Parse using a copy to avoid cursor desync on the main JSON instance.
    JSON jsonCopy = json;
    while (jsonCopy.enterarray())
    {
        parseArrayContents(jsonCopy);
        jsonCopy.leavearray();
    }

    // Consume the full response element in the original JSON.
    while (json.storeobject())
        ;

    if (sizeClasses.empty())
    {
        mCompletion(API_EINTERNAL, {});
    }
    else
    {
        mCompletion(API_OK, std::move(sizeClasses));
    }
    return true;
}

} // namespace mega

#endif // MEGA_USE_WSUPLOAD
