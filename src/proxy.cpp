#include "mega/proxy.h"

using namespace mega;
using namespace std;

Proxy::Proxy()
{
    proxyType = AUTO;
}

void Proxy::setProxyType(int proxyType)
{
    this->proxyType = proxyType;
}

void Proxy::setProxyURL(string *proxyURL)
{
    this->proxyURL = *proxyURL;
}

void Proxy::setCredentials(string *username, string *password)
{
    this->username = *username;
    this->password = *password;
}

int Proxy::getProxyType()
{
    return proxyType;
}

string Proxy::getProxyURL()
{
    return this->proxyURL;
}

bool Proxy::credentialsNeeded()
{
    return (username.size() != 0);
}

string Proxy::getUsername()
{
    return username;
}

string Proxy::getPassword()
{
    return password;
}
