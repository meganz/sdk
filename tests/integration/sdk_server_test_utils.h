#pragma once

#include "easy_curl.h"
#include "mega/scoped_helpers.h"
#include "SdkTest_test.h"

#include <optional>

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
