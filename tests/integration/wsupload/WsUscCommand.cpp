#include "WsUscCommand.h"

#include "megaapi_impl.h"
#include "mega/command.h"
#include "mega/json.h"
#include "mega/scoped_helpers.h"

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <utility>

namespace mega::test::wsupload
{

namespace
{

class CommandUscForTest final : public ::mega::Command
{
public:
    using SizeClass = std::pair<std::string, ::m_off_t>;
    using Completion = std::function<void(::mega::Error, std::vector<SizeClass>&&)>;

    CommandUscForTest(::mega::MegaClient& client, Completion completion) :
        mCompletion(std::move(completion))
    {
        cmd("usc");
        tag = client.reqtag;
        mLockless = true;
    }

    bool procresult(Result r, ::mega::JSON& json) override
    {
        if (r.wasErrorOrOK())
        {
            if (r.wasError(::mega::API_OK))
                mCompletion(::mega::API_EINTERNAL, {});
            else
                mCompletion(r.errorOrOK(), {});
            return true;
        }

        if (!r.hasJsonArray())
        {
            mCompletion(::mega::API_EINTERNAL, {});
            return true;
        }

        std::vector<SizeClass> sizeClasses;

        auto peek = [](const ::mega::JSON& j) -> char
        {
            const char* p = j.pos;
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',' ||
                   *p == ':')
                ++p;
            return *p;
        };

        auto parseEntryArray = [&](::mega::JSON& j)
        {
            std::string host;
            std::string path;
            ::m_off_t maxSize = 0;

            const bool okHost = j.storeobject(&host);
            const bool okPath = j.storeobject(&path);
            if (okHost && okPath)
            {
                if (j.isnumeric())
                {
                    maxSize = j.getint();
                }

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

        std::function<void(::mega::JSON&)> parseArrayContents;
        parseArrayContents = [&](::mega::JSON& j)
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

        ::mega::JSON jsonCopy = json;
        while (jsonCopy.enterarray())
        {
            parseArrayContents(jsonCopy);
            jsonCopy.leavearray();
        }

        while (json.storeobject())
            ;

        if (sizeClasses.empty())
        {
            mCompletion(::mega::API_EINTERNAL, {});
        }
        else
        {
            mCompletion(::mega::API_OK, std::move(sizeClasses));
        }
        return true;
    }

private:
    Completion mCompletion;
};

} // namespace

bool fetchUscSizeClasses(::mega::MegaApi& api,
                         std::vector<int64_t>& maxSizes,
                         int timeoutSeconds)
{
    ::mega::MegaApiImpl* impl = ::mega::MegaApiImpl::ImplOf(&api);
    if (!impl)
    {
        return false;
    }

    auto promise = std::make_shared<std::promise<std::vector<CommandUscForTest::SizeClass>>>();
    auto future = promise->get_future();

    auto exec = std::make_shared<::mega::ExecuteOnce>(
        [impl, promise]()
        {
            ::mega::MegaClient* client = impl->getClientForTesting();
            if (!client)
            {
                promise->set_value({});
                return;
            }

            client->queueCommand(new CommandUscForTest(
                *client,
                [promise](::mega::Error e, std::vector<CommandUscForTest::SizeClass>&& classes)
                {
                    if (e != ::mega::API_OK)
                    {
                        promise->set_value({});
                        return;
                    }
                    promise->set_value(std::move(classes));
                }));
        });

    impl->executeOnThreadForTesting(exec);

    if (future.wait_for(std::chrono::seconds(timeoutSeconds)) != std::future_status::ready)
    {
        return false;
    }

    auto classes = future.get();
    if (classes.empty())
    {
        return false;
    }

    maxSizes.clear();
    maxSizes.reserve(classes.size());
    for (const auto& entry : classes)
    {
        maxSizes.push_back(entry.second);
    }
    return true;
}

} // namespace mega::test::wsupload
