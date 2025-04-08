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
TreeProcShareKeys::TreeProcShareKeys(std::shared_ptr<Node> n, bool includeParentChain)
    : sn(n)
    , includeParentChain(includeParentChain)
{
}

void TreeProcShareKeys::proc(MegaClient*, std::shared_ptr<Node> n)
{
    snk.add(n, sn, includeParentChain);
}

void TreeProcShareKeys::get(Command* c)
{
    snk.get(c);
}

void TreeProcForeignKeys::proc(MegaClient* client, std::shared_ptr<Node> n)
{
    if (n->foreignkey)
    {
        client->nodekeyrewrite.push_back(n->nodehandle);

        n->foreignkey = false;
    }
}

// mark node as removed and notify
void TreeProcDel::proc(MegaClient* client, std::shared_ptr<Node> n)
{
    n->changed.removed = true;
    client->mNodeManager.notifyNode(n);
    handle userHandle = ISUNDEF(mOriginatingUser) ? n->owner : mOriginatingUser;

    if (userHandle != client->me && !client->loggedIntoFolder())
    {
        client->useralerts.noteSharedNode(userHandle, n->type, 0, n.get());
    }
}

void TreeProcDel::setOriginatingUser(const handle &handle)
{
    mOriginatingUser = handle;
}

void TreeProcApplyKey::proc(MegaClient *client, std::shared_ptr<Node> n)
{
    if (n->attrstring)
    {
        n->applykey();
        if (!n->attrstring)
        {
            n->changed.attrs = true;
            client->mNodeManager.notifyNode(n);
        }
    }
}

void TreeProcCopy::allocnodes()
{
    nn.resize(nc);
    allocated = true;
}

// determine node tree size (nn = NULL) or write node tree to new nodes array
void TreeProcCopy::proc(MegaClient* client, std::shared_ptr<mega::Node> n)
{
    if (allocated)
    {
        string attrstring;
        SymmCipher key;
        assert(nc > 0);
        NewNode* t = &nn[--nc];

        // copy node
        t->source = NEW_NODE;
        t->type = n->type;
        t->nodehandle = n->nodehandle;
        t->parenthandle = n->parent ? n->parent->nodehandle : UNDEF;

        // copy key (if file) or generate new key (if folder)
        if (n->type == FILENODE) t->nodekey = n->nodekey();
        else
        {
            byte buf[FOLDERNODEKEYLENGTH];
            client->rng.genblock(buf,sizeof buf);
            t->nodekey.assign((char*)buf,FOLDERNODEKEYLENGTH);
        }

        t->attrstring.reset(new string);
        if(t->nodekey.size())
        {
            key.setkey((const byte*)t->nodekey.data(),n->type);

            AttrMap tattrs;
            tattrs.map = n->attrs.map;
            nameid rrname = AttrMap::string2nameid("rr");
            attr_map::iterator it = tattrs.map.find(rrname);
            if (it != tattrs.map.end())
            {
                LOG_debug << "Removing rr attribute";
                tattrs.map.erase(it);
            }
            if (resetSensitive && tattrs.map.erase(AttrMap::string2nameid("sen")))
            {
                LOG_debug << "Removing sen attribute";
            }

            tattrs.getjson(&attrstring);
            client->makeattr(&key, t->attrstring, attrstring.c_str());
        }
    }
    else nc++;
}

#ifdef ENABLE_SYNC

LocalTreeProcMove::LocalTreeProcMove(Sync* sync)
{
    newsync = sync;
    nc = 0;
}

void LocalTreeProcMove::proc(FileSystemAccess&, LocalNode* localnode)
{
    if (newsync != localnode->sync)
    {
        localnode->sync->statecachedel(localnode);
        localnode->sync = newsync;
        newsync->statecacheadd(localnode);
    }
    nc++;
}

void LocalTreeProcUpdateTransfers::proc(FileSystemAccess&, LocalNode* localnode)
{
    // Only updating the localname thread safe field.
    // Transfers are managed from the megaclient thread

    localnode->updateTransferLocalname();
}

#endif
} // namespace
