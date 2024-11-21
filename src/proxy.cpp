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

void Proxy::setProxyURL(string* newURL)
{
    proxyURL = *newURL;
}

void Proxy::setCredentials(string* newUsername, string* newPassword)
{
    username = *newUsername;
    password = *newPassword;
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

}
