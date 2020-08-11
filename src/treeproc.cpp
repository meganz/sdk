/**
 * @file treeproc.cpp
 * @brief Node tree processor
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

#include "mega/treeproc.h"
#include "mega/megaclient.h"
#include "mega/logging.h"

namespace mega {
// create share keys
TreeProcShareKeys::TreeProcShareKeys(Node* n)
{
    sn = n;
}

void TreeProcShareKeys::proc(MegaClient*, Node* n)
{
    snk.add(n, sn, sn != NULL);
}

void TreeProcShareKeys::get(Command* c)
{
    snk.get(c);
}

void TreeProcForeignKeys::proc(MegaClient* client, Node* n)
{
    if (n->foreignkey)
    {
        client->nodekeyrewrite.push_back(n->nodehandle);

        n->foreignkey = false;
    }
}

// total disk space / node count
TreeProcDU::TreeProcDU()
{
    numbytes = 0;
    numfiles = 0;
    numfolders = 0;
}

void TreeProcDU::proc(MegaClient*, Node* n)
{
    if (n->type == FILENODE)
    {
        numbytes += n->size;
        numfiles++;
    }
    else
    {
        numfolders++;
    }
}

// mark node as removed and notify
void TreeProcDel::proc(MegaClient* client, Node* n)
{
    n->changed.removed = true;
    n->tag = client->reqtag;
    client->notifynode(n);
    if (n->owner != client->me)
    {
        client->useralerts.noteSharedNode(n->owner, n->type, 0, NULL);
    }
}

void TreeProcApplyKey::proc(MegaClient *client, Node *n)
{
    if (n->attrstring)
    {
        n->applykey();
        if (!n->attrstring)
        {
            n->changed.attrs = true;
            client->notifynode(n);
        }
    }
}

#ifdef ENABLE_SYNC
// stop sync get
void TreeProcDelSyncGet::proc(MegaClient*, Node* n)
{
    if (n->syncget)
    {
        delete n->syncget;
        n->syncget = NULL;
    }
}

LocalTreeProcMove::LocalTreeProcMove(Sync* sync, bool recreate)
{
    this->newsync = sync;
    this->recreate = recreate;
    nc = 0;
}

void LocalTreeProcMove::proc(MegaClient*, LocalNode* localnode)
{
    if (newsync != localnode->sync)
    {
        localnode->sync->statecachedel(localnode);
        localnode->sync = newsync;
        newsync->statecacheadd(localnode);
    }

    if (recreate)
    {
        localnode->created = false;
        localnode->node = NULL;
    }

    nc++;
}

void LocalTreeProcUpdateTransfers::proc(MegaClient *, LocalNode *localnode)
{
    if (localnode->transfer && !localnode->transfer->localfilename.empty())
    {
        LOG_debug << "Updating transfer path";
        localnode->prepare();
    }
}

void LocalTreeProcUnlinkNodes::proc(MegaClient *, LocalNode *localnode)
{
    if (localnode->node)
    {
        localnode->node->localnode = NULL;
        localnode->node = NULL;
    }
}

#endif
} // namespace
