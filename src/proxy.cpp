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

using namespace std;

namespace mega {

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
}
