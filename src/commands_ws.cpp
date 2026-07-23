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

CommandTfsForWsUpload::CommandTfsForWsUpload(MegaClient& client,
                                             const std::vector<NodeHandle>& folders,
                                             Completion completion):
    mCompletion(std::move(completion))
{
    cmd("tfs");
    beginarray("n");
    for (const NodeHandle& h: folders)
    {
        element(h.as8byte(), MegaClient::NODEHANDLE);
    }
    endarray();
    tag = client.reqtag;
    // tfs is a pure read and safe to run on the lockless request channel, so a
    // burst of quota queries never queues behind putnodes on the main cs channel.
    mLockless = true;
}

bool CommandTfsForWsUpload::procresult(Result r, JSON& json)
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

    WsTfsGroupBalances groups;

    // Parse using a copy to avoid cursor desync on the main JSON instance.
    JSON jc = json;
    if (jc.enterarray())
    {
        while (jc.enterarray()) // one quota group: [writableBytes, handle, handle, ...]
        {
            m_off_t bytes = -1;
            if (jc.isnumeric())
                bytes = jc.getint();

            std::vector<NodeHandle> handles;
            while (jc.ishandle(MegaClient::NODEHANDLE))
                handles.push_back(NodeHandle().set6byte(jc.gethandle(MegaClient::NODEHANDLE)));

            // Tolerate forward-compatible extras appended to a group.
            while (jc.storeobject())
                ;
            jc.leavearray();

            // Fail-open: skip a malformed group (missing/negative balance or no
            // handles) but keep the rest.
            if (bytes >= 0 && !handles.empty())
                groups.emplace_back(bytes, std::move(handles));
        }
        jc.leavearray();
    }

    // Consume the full response element in the original JSON.
    while (json.storeobject())
        ;

    // An empty outer array is valid ("no balance data"): API_OK + empty groups.
    mCompletion(API_OK, std::move(groups));
    return true;
}

} // namespace mega

#endif // MEGA_USE_WSUPLOAD
