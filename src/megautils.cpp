/**
 * @file megautils.cpp
 * @brief Mega utilities
 *
 * (c) 2024 by Mega Limited, Wellsford, New Zealand
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

#include <megautils.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace mega
{

std::vector<std::string> toNamesVector(const MegaNodeList& nodes)
{
    std::vector<std::string> result;
    result.reserve(static_cast<size_t>(nodes.size()));
    for (int i = 0; i < nodes.size(); ++i)
    {
        result.emplace_back(nodes.get(i)->getName());
    }
    return result;
}

std::vector<std::string> stringListToVector(const MegaStringList& l)
{
    std::vector<std::string> result;
    result.reserve(static_cast<size_t>(l.size()));
    for (int i = 0; i < l.size(); ++i)
        result.emplace_back(l.get(i));
    return result;
}
}
