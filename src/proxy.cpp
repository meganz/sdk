/**
 * @file proxy.cpp
 * @brief Class for manipulating proxy data
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "mega/proxy.h"

#include "mega/logging.h"
#include "mega/scoped_helpers.h"
#include "mega/utils.h"

#include <curl/curl.h>

using namespace std;

namespace mega {

// A help function to get part from CURLU
static auto getCurlUrlPart(CURLU* handle, CURLUPart what, unsigned int flags)
{
    assert(handle);
    char* value = nullptr;
    curl_url_get(handle, what, &value, flags);
    return makeUniqueFrom(value,
                          [](void* p)
                          {
                              curl_free(p);
                          });
};

Proxy::Proxy()
{
    proxyType = AUTO;
}

void Proxy::setProxyType(int newType)
{
    proxyType = newType;
}

void Proxy::setProxyURL(const string& newURL)
{
    proxyURL = newURL;
}

void Proxy::setCredentials(const string& newUsername, const string& newPassword)
{
    username = newUsername;
    password = newPassword;
}

int Proxy::getProxyType() const
{
    return proxyType;
}

string Proxy::getProxyURL() const
{
    return this->proxyURL;
}

bool Proxy::credentialsNeeded() const
{
    return (username.size() != 0);
}

string Proxy::getUsername() const
{
    return username;
}

string Proxy::getPassword() const
{
    return password;
}

Proxy Proxy::parseFromURL(const std::string& url)
{
    if (url.empty())
        return {};

    // Allocate a CURLU
    auto curlUrl = makeUniqueFrom(curl_url(),
                                  [](CURLU* handle)
                                  {
                                      curl_url_cleanup(handle);
                                  });
    if (!curlUrl)
    {
        LOG_err << "curl_url failed";
        return {};
    }

    // Set full url, allow non-supported as proxy URL has makeup, and guess
    if (auto rc = curl_url_set(curlUrl.get(),
                               CURLUPART_URL,
                               url.c_str(),
                               CURLU_NON_SUPPORT_SCHEME | CURLU_GUESS_SCHEME);
        rc != CURLUE_OK)
    {
        LOG_err << "curl_url_set failed: " << rc << " url: " << url;
        return {};
    }

    // Get parts
    auto scheme = getCurlUrlPart(curlUrl.get(), CURLUPART_SCHEME, 0);
    auto username = getCurlUrlPart(curlUrl.get(), CURLUPART_USER, CURLU_URLDECODE);
    auto password = getCurlUrlPart(curlUrl.get(), CURLUPART_PASSWORD, CURLU_URLDECODE);
    auto host = getCurlUrlPart(curlUrl.get(), CURLUPART_HOST, CURLU_URLDECODE);
    auto port = getCurlUrlPart(curlUrl.get(), CURLUPART_PORT, 0);

    if (!host)
    {
        LOG_err << "curl_url_get host failed: " << url;
        return {};
    }

    Proxy proxy{};

    // Username and password, optional
    if (username && password)
    {
        proxy.setCredentials(username.get(), password.get());
    }

    // Format URL without user and password
    std::ostringstream oss;
    if (scheme)
        oss << scheme.get() << "://";

    oss << host.get();

    if (port)
        oss << ":" << port.get();

    // Set URL
    proxy.setProxyURL(oss.str());
    proxy.setProxyType(CUSTOM);
    return proxy;
}

bool Proxy::operator==(const Proxy& other) const
{
    return other.proxyType == proxyType && other.username == username &&
           other.password == password && other.proxyURL == proxyURL;
}

int proxyTypeFromString(const std::string& type)
{
#define ENTRY(name) {#name, Proxy::name},
    static const std::map<std::string, int> types = {DEFINE_PROXY_TYPES(ENTRY)}; // types
#undef ENTRY

    if (auto i = types.find(type); i != types.end())
        return i->second;

    return NONE;
}

const std::string* proxyTypeToString(int type)
{
#define ENTRY(name) {Proxy::name, #name},
    static const std::map<int, std::string> strings = {DEFINE_PROXY_TYPES(ENTRY)}; // strings
#undef ENTRY

    if (auto i = strings.find(type); i != strings.end())
        return &i->second;

    return nullptr;
}

std::string detectProxyFromEnv()
{
    const std::array<std::string, 4> envs{
        "http_proxy",
        "HTTP_PROXY",
        "https_proxy",
        "HTTPS_PROXY",
    };

    for (const auto& env: envs)
    {
        if (const auto& [value, hasValue] = Utils::getenv(env); hasValue && !value.empty())
            return value;
    }

    return "";
}

void getEnvProxy(Proxy* proxy)
{
    assert(proxy);
    *proxy = Proxy::parseFromURL(detectProxyFromEnv());
}
}
