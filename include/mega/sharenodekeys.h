/**
 * @file mega/sharenodekeys.h
 * @brief cr element share/node map key generator
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

#ifndef MEGA_SHARENODEKEYS_H
#define MEGA_SHARENODEKEYS_H 1

#include "types.h"

namespace mega {
// cr element share/node map key generator
class MEGA_API ShareNodeKeys
{
    sharedNode_vector shares;
    vector<string> items;

    string keys;

    int addshare(std::shared_ptr<Node>);

public:
    // a convenience function for calling the full add() below when working with Node*
    void add(std::shared_ptr<Node>, std::shared_ptr<Node>, bool);

    // Adds keys needed for sharing the node (specifed wtih nodekey/nodehandle) to the `keys` and `items` collections.
    // Each node may be in multiple shares so the parent chain is traversed and if this node is in multiple shares, then multiple keys are added.
    // The result is suitable for sending all the collected keys for each share, per Node, to the API.
    void add(const string& nodekey, handle nodehandle, std::shared_ptr<Node>, bool, const byte* = NULL, int = 0);

    void get(Command*, bool skiphandles = false);
};
} // namespace

#endif
