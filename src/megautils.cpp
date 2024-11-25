/**
 * @file megautils.cpp
 *
 * (c) 2024 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
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

std::vector<std::vector<std::string>> bucketsToVector(const MegaRecentActionBucketList& buckets)
{
    std::vector<std::vector<std::string>> result;
    for (int i = 0; i < buckets.size(); ++i)
    {
        auto bucketNodes = buckets.get(i)->getNodes();
        if (!bucketNodes)
            continue;
        std::vector<std::string> bucketInfo;
        for (int j = 0; j < bucketNodes->size(); ++j)
        {
            bucketInfo.emplace_back(bucketNodes->get(j)->getName());
        }
        result.emplace_back(std::move(bucketInfo));
    }
    return result;
}

std::vector<std::unique_ptr<MegaSyncStall>> toSyncStallVector(const MegaSyncStallList& stallList)
{
    std::vector<std::unique_ptr<MegaSyncStall>> result;
    result.reserve(stallList.size());
    for (size_t i = 0; i < stallList.size(); ++i)
    {
        result.emplace_back(std::unique_ptr<MegaSyncStall>(stallList.get(i)->copy()));
    }
    return result;
}
}
