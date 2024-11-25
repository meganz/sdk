/**
 * @file megautils.h
 * @brief Utilities related with public objects from intermediate layer
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

#ifndef MEGAUTILS_H
#define MEGAUTILS_H

#include <megaapi.h>
#include <memory>
#include <string>
#include <vector>

namespace mega
{
/**
 * @brief Aux function to get a vector with the names of the nodes in a given MegaNodeList
 */
std::vector<std::string> toNamesVector(const MegaNodeList& nodes);

/**
 * @brief Aux function to get a vector with the strings in a given MegaStringList
 */
std::vector<std::string> stringListToVector(const MegaStringList& l);

/**
 * @brief Aux function to get a vector with vector of strings in a given MegaRecentActionBucketList
 */
std::vector<std::vector<std::string>> bucketsToVector(const MegaRecentActionBucketList& buckets);

/**
 * @brief Convert a MegaSyncStallList into a vector of unique_ptr to its components. To own the
 * elements, the function copies each element in the list.
 */
std::vector<std::unique_ptr<MegaSyncStall>> toSyncStallVector(const MegaSyncStallList& stallList);
}

#endif // MEGAUTILS_H
