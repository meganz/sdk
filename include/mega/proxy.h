#ifndef PROXY_H
#define PROXY_H

#include "mega/types.h"
#include <string>

namespace mega {
struct MEGA_API Proxy
{
public:
    enum ProxyType {NONE = 0, AUTO = 1, CUSTOM = 2};

    Proxy();
    void setProxyType(int proxyType);
    void setProxyURL(std::string *proxyURL);
    void setCredentials(std::string *username, std::string *password);
    int getProxyType();
    std::string getProxyURL();
    bool credentialsNeeded();
    std::string getUsername();
    std::string getPassword();

protected:
    int proxyType;
    std::string proxyURL;
    std::string username;
    std::string password;
};
} // namespace

#endif // PROXY_H
