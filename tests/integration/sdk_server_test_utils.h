#pragma once

#include "easy_curl.h"
#include "mega/scoped_helpers.h"
#include "mega/testhooks.h"
#include "SdkTest_test.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

class SdkServerTest: public SdkTest
{
protected:
    unique_ptr<MegaNode> createFolder(unsigned int apiIndex,
                                      const std::string& name,
                                      MegaNode* parent);

    unique_ptr<MegaNode> createFolder(unsigned int apiIndex, const std::string& name);

    unique_ptr<MegaNode> uploadFile(unsigned int apiIndex,
                                    const std::string& name,
                                    const std::string& contents,
                                    MegaNode* parent = nullptr);
};

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
/**
 * @brief Makes the local TCP server drop the connections it accepts.
 *
 * While this object is alive, the server fails the setup of the first failuresToSimulate
 * connections right after accepting them, which is where a real accept failure leaves the
 * connection. The object also counts the connections whose asynchronous close has already
 * finished, so a test can wait for the teardown instead of sleeping for a fixed time.
 *
 * The counters live in a shared state, so a hook call that is already running keeps them
 * alive even if this object is destroyed in the meantime.
 */
class ScopedTcpServerAcceptFailure
{
public:
    explicit ScopedTcpServerAcceptFailure(int failuresToSimulate):
        mState{std::make_shared<State>()}
    {
        auto state = mState;

        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);

        ::mega::globalMegaTestHooks.onTcpServerSimulateAcceptFailure =
            [state, failuresToSimulate]() -> bool
        {
            // Several servers may run their own loop thread, so claim the slot atomically
            int simulated = state->simulated.load();
            while (simulated < failuresToSimulate)
            {
                if (state->simulated.compare_exchange_weak(simulated, simulated + 1))
                {
                    return true;
                }
            }
            return false;
        };

        ::mega::globalMegaTestHooks.onTcpServerConnectionClosed = [state](bool wasOwnedByServer)
        {
            if (!wasOwnedByServer)
            {
                state->unowned++;
            }
            state->closed++;
        };
    }

    ~ScopedTcpServerAcceptFailure()
    {
        std::lock_guard<std::mutex> g(::mega::globalMegaTestHooks.mMutex);
        ::mega::globalMegaTestHooks.onTcpServerSimulateAcceptFailure = nullptr;
        ::mega::globalMegaTestHooks.onTcpServerConnectionClosed = nullptr;
    }

    ScopedTcpServerAcceptFailure(const ScopedTcpServerAcceptFailure&) = delete;
    ScopedTcpServerAcceptFailure& operator=(const ScopedTcpServerAcceptFailure&) = delete;

    int simulatedFailures() const
    {
        return mState->simulated.load();
    }

    int closedConnections() const
    {
        return mState->closed.load();
    }

    // Connections that the server no longer owned when their close finished. The context of
    // such a connection was freed while its libuv close callbacks were still pending
    int connectionsClosedAfterBeingFreed() const
    {
        return mState->unowned.load();
    }

    bool waitForClosedConnections(int expected, std::chrono::milliseconds timeout) const
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (closedConnections() < expected)
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return true;
    }

private:
    struct State
    {
        std::atomic<int> simulated{0};
        std::atomic<int> closed{0};
        std::atomic<int> unowned{0};
    };

    std::shared_ptr<State> mState;
};
#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED

/**
 * Helper class for HTTP client requests
 */
class HttpClient
{
public:
    static inline const std::string EmptyRange = {};

    enum class BodyMode
    {
        WithBody,
        WithoutBody
    };

    struct Response
    {
        int statusCode;
        map<string, string> headers;
        std::string body;
        curl_off_t contentLength;
    };

    static Response get(const std::string& url,
                        const std::string& range = EmptyRange,
                        const map<string, string>& headers = {},
                        bool curlPathAsIs = false)
    {
        return performRequest(url, "GET", range, headers, "", BodyMode::WithBody, curlPathAsIs);
    }

    static Response post(const std::string& url,
                         const map<string, string>& headers = {},
                         const string& body = "")
    {
        return performRequest(url, "POST", EmptyRange, headers, body, BodyMode::WithBody);
    }

    static Response put(const std::string& url,
                        const map<string, string>& headers = {},
                        const string& body = "")
    {
        return performRequest(url, "PUT", EmptyRange, headers, body, BodyMode::WithBody);
    }

    static Response del(const std::string& url, const map<string, string>& headers = {})
    {
        return performRequest(url, "DELETE", EmptyRange, headers, "", BodyMode::WithBody);
    }

    static Response head(const std::string& url, const map<string, string>& headers = {})
    {
        return performRequest(url, "HEAD", EmptyRange, headers, "", BodyMode::WithoutBody);
    }

    static Response request(const std::string& url,
                            const std::string& method,
                            const map<string, string>& headers = {},
                            const string& body = "",
                            bool curlPathAsIs = false)
    {
        return performRequest(url,
                              method,
                              EmptyRange,
                              headers,
                              body,
                              BodyMode::WithBody,
                              curlPathAsIs);
    }

private:
    static bool appendHttpHeaders(sdk_test::EasyCurlSlist& easyCurlSlist,
                                  const std::map<std::string, std::string>& headers)
    {
        for (const auto& header: headers)
        {
            std::string headerStr = header.first + ": " + header.second;
            if (!easyCurlSlist.append(headerStr))
            {
                return false;
            }
        }
        return true;
    }

    static Response performRequest(const std::string& url,
                                   const std::string& method,
                                   const std::string& rangeHeader = EmptyRange,
                                   const map<string, string>& headers = {},
                                   const string& body = "",
                                   BodyMode bodyMode = BodyMode::WithBody,
                                   bool curlPathAsIs = false)
    {
        Response response;
        auto easyCurl = sdk_test::EasyCurl();
        auto easyCurlSlist = sdk_test::EasyCurlSlist();
        auto curl = easyCurl.curl();

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        if (curlPathAsIs)
        {
            curl_easy_setopt(curl, CURLOPT_PATH_AS_IS, 1L);
        }
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        if (method != "GET" && method != "HEAD")
        {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        }

        if (bodyMode == BodyMode::WithoutBody)
        {
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        }

        if (!body.empty())
        {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl,
                             CURLOPT_POSTFIELDSIZE_LARGE,
                             static_cast<curl_off_t>(body.size()));
        }
        else
        {
            curl_easy_setopt(curl,
                             CURLOPT_POSTFIELDSIZE_LARGE,
                             static_cast<curl_off_t>(body.size()));
        }

        if (!appendHttpHeaders(easyCurlSlist, headers))
        {
            LOG_err << "Failed to append HTTP headers";
            response.statusCode = 0;
            response.contentLength = -1;
            return response;
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, easyCurlSlist.slist());

        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");

        if (!rangeHeader.empty())
        {
            curl_easy_setopt(curl, CURLOPT_RANGE, rangeHeader.c_str());
        }

        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK)
        {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
            curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &response.contentLength);
        }
        else
        {
            LOG_err << "CURL error for " << method << " " << url << ": " << curl_easy_strerror(res)
                    << " (code: " << res << ")";
            response.statusCode = 0;
            response.contentLength = -1;
            response.body.clear();
            response.headers.clear();
        }

        return response;
    }

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, Response* response)
    {
        size_t totalSize = size * nmemb;
        response->body.append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }

    static size_t headerCallback(void* contents, size_t size, size_t nmemb, Response* response)
    {
        size_t totalSize = size * nmemb;
        std::string headerLine(static_cast<char*>(contents), totalSize);
        if (headerLine == "\r\n")
        {
            // end of header block
            return totalSize;
        }

        if (headerLine.rfind("HTTP/", 0) == 0)
        {
            // status line
            return totalSize;
        }
        size_t colonPos = headerLine.find(':');
        if (colonPos != std::string::npos)
        {
            std::string headerName = headerLine.substr(0, colonPos);
            headerName = Utils::toLowerUtf8(headerName);
            std::string headerValue = headerLine.substr(colonPos + 1);
            // Trim whitespace
            headerValue = Utils::trim(headerValue);
            response->headers[headerName] = headerValue;
        }
        return totalSize;
    }
};

std::optional<ScopedDestructor> scopedHttpServer(MegaApi* api);
