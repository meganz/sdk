#include "mega/proxy.h"
#include "mega/utils.h"

#include <gtest/gtest.h>

using ::mega::Proxy;

namespace mega
{
std::ostream& operator<<(std::ostream& os, const ::mega::Proxy& proxy)
{
    os << "["
       << "type=" << proxy.getProxyType() << ", url=" << proxy.getProxyURL()
       << ", user=" << proxy.getUsername() << ", password=" << proxy.getPassword() << "]";
    return os;
}
}

TEST(Proxy, NoHostURLReturnsDefaultProxy)
{
    ASSERT_EQ(Proxy{}, Proxy::parseFromURL("http://:122"));
}

TEST(Proxy, EmptyStringReturnsDefaultProxy)
{
    ASSERT_EQ(Proxy{}, Proxy::parseFromURL(""));
}

TEST(Proxy, ParseURLWithoutCredential)
{
    Proxy expected;
    {
        expected.setProxyType(Proxy::CUSTOM);
        expected.setProxyURL("https://example.com");
    }
    ASSERT_EQ(expected, Proxy::parseFromURL("https://example.com"));
}

TEST(Proxy, ParseURLWithPort)
{
    Proxy expected;
    {
        expected.setProxyType(Proxy::CUSTOM);
        expected.setProxyURL("https://example.com:1010");
    }
    ASSERT_EQ(expected, Proxy::parseFromURL("https://example.com:1010"));
}

TEST(Proxy, ParseURLWithCrendential)
{
    Proxy expected;
    {
        expected.setProxyType(Proxy::CUSTOM);
        expected.setCredentials("user", "pass");
        expected.setProxyURL("https://example.com:1010");
    }
    ASSERT_EQ(expected, Proxy::parseFromURL("https://user:pass@example.com:1010"));
}

TEST(Proxy, SocksSchemeIsSupported)
{
    Proxy expected;
    {
        expected.setProxyType(Proxy::CUSTOM);
        expected.setProxyURL("socks5h://example.com");
    }
    ASSERT_EQ(expected, Proxy::parseFromURL("socks5h://example.com"));
}

TEST(Proxy, SchemeIsGuessed)
{
    Proxy expected;
    {
        expected.setProxyType(Proxy::CUSTOM);
        expected.setProxyURL("http://example.com");
    }
    ASSERT_EQ(expected, Proxy::parseFromURL("example.com"));
}

TEST(Proxy, IncompleteCrendentialIsNeglected)
{
    Proxy expected;
    {
        expected.setProxyType(Proxy::CUSTOM);
        expected.setProxyURL("https://example.com:1010");
    }
    ASSERT_EQ(expected, Proxy::parseFromURL("https://user@example.com:1010"));
}

#if !defined(WIN32) && !defined(__APPLE__) && !(__ANDROID__)
using mega::Utils;

class EnvRestorer
{
public:
    EnvRestorer(const std::string& name);
    ~EnvRestorer();

private:
    std::string mName;
    std::string mValue;
    bool mHasValue{false};
};

EnvRestorer::EnvRestorer(const std::string& name):
    mName{name}
{
    std::tie(mValue, mHasValue) = Utils::getenv(mName);
}

EnvRestorer::~EnvRestorer()
{
    if (!mHasValue)
        Utils::unsetenv(mName);
    else
        Utils::setenv(mName, mValue);
}

TEST(Proxy, GetProxyFromEnv)
{
    EnvRestorer http_proxy_restorer{"http_proxy"};
    EnvRestorer HTTP_PROXY_restorer{"HTTP_PROXY"};
    EnvRestorer https_proxy_restorer{"https_proxy"};
    EnvRestorer HTTPS_PROXY_restorer{"HTTPS_PROXY"};

    // unset
    Utils::unsetenv("http_proxy");
    Utils::unsetenv("HTTP_PROXY");
    Utils::unsetenv("https_proxy");
    Utils::unsetenv("HTTPS_PROXY");

    // No environment is set, default is returned
    Proxy expected{};
    Proxy proxy{};
    mega::getEnvProxy(&proxy);
    ASSERT_EQ(expected, proxy);

    // https_proxy is set, get from https_proxy
    Utils::setenv("https_proxy", "https://example3.com");
    expected.setProxyType(Proxy::CUSTOM);
    expected.setProxyURL("https://example3.com");
    mega::getEnvProxy(&proxy);
    ASSERT_EQ(expected, proxy);

    // https_proxy, HTTP_PROXY is set. HTTP_PROXY takes priority
    Utils::setenv("HTTP_PROXY", "http://example2.com");
    expected.setProxyType(Proxy::CUSTOM);
    expected.setProxyURL("http://example2.com");
    mega::getEnvProxy(&proxy);
    ASSERT_EQ(expected, proxy);

    // https_proxy, HTTP_PROXY and http_proxy is set. http_proxy takes prority
    Utils::setenv("http_proxy", "http://example1.com");
    expected.setProxyType(Proxy::CUSTOM);
    expected.setProxyURL("http://example1.com");
    mega::getEnvProxy(&proxy);
    ASSERT_EQ(expected, proxy);
}
#endif
