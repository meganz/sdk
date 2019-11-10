/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
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

#include "utils.h"

#include <random>

#include <mega/megaapp.h>

#include "constants.h"
#include "DefaultedFileSystemAccess.h"
#include "FsNode.h"

namespace mt {

namespace {

std::mt19937 gRandomGenerator{1};

void collectAllFsNodesImpl(std::map<std::string, const mt::FsNode*>& nodes, const mt::FsNode& node)
{
    const auto path = node.getPath();
    assert(nodes.find(path) == nodes.end());
    nodes[path] = &node;
    if (node.getType() == mega::FOLDERNODE)
    {
        for (const auto child : node.getChildren())
        {
            collectAllFsNodesImpl(nodes, *child);
        }
    }
}

#ifdef ENABLE_SYNC
void initializeLocalNode(mega::LocalNode& l, mega::Sync& sync, mega::LocalNode* parent, mega::handlelocalnode_map& fsidnodes,
                         mega::nodetype_t type, const std::string& name, const mega::FileFingerprint& ffp)
{
    l.sync = &sync;
    l.parent = parent;
    l.type = type;
    l.name = name;
    l.localname = name;
    l.slocalname = new std::string{name};
    l.fsid_it = fsidnodes.end();
    l.setfsid(nextFsId(), fsidnodes);
    if (parent)
    {
        assert(parent->children.find(&l.name) == parent->children.end());
        parent->children[&l.name] = &l;
    }
    static_cast<mega::FileFingerprint&>(l) = ffp;
}
#endif

} // anonymous

mega::handle nextFsId()
{
    static mega::handle fsId{0};
    return fsId++;
}

std::shared_ptr<mega::MegaClient> makeClient(mega::MegaApp& app, mega::FileSystemAccess& fsaccess)
{
    struct HttpIo : mega::HttpIO
    {
        void addevents(mega::Waiter*, int) override {}
        void post(struct mega::HttpReq*, const char* = NULL, unsigned = 0) override {}
        void cancel(mega::HttpReq*) override {}
        m_off_t postpos(void*) override { return {}; }
        bool doio(void) override { return {}; }
        void setuseragent(std::string*) override {}
    };

    auto httpio = new HttpIo;

    auto deleter = [httpio](mega::MegaClient* client)
    {
        delete client;
        delete httpio;
    };

    std::shared_ptr<mega::MegaClient> client{new mega::MegaClient{
            &app, nullptr, httpio, &fsaccess, nullptr, nullptr, "XXX", "unit_test"
        }, deleter};

    return client;
}

mega::Node& makeNode(mega::MegaClient& client, const mega::nodetype_t type, const mega::handle handle, mega::Node* const parent)
{
    ::mega::node_vector dp;
    assert(client.nodes.find(handle) == client.nodes.end());
    auto n = new mega::Node(&client, &dp, handle, parent ? parent->nodehandle : ::mega::UNDEF, type, -1, client.me, "\",", 0); // owned by the client
    assert(client.nodes.find(n->nodehandle) != client.nodes.end());
    assert(n->type == type);
    assert(n->client == &client);
    assert(n->todebris_it == client.todebris.end());
    assert(n->tounlink_it == client.tounlink.end());
    //client.mFingerprints.add(n);
    assert(n->nodehandle == handle);
    if (parent)
    {
        assert(n->parenthandle == parent->nodehandle);
    //    parent->children.push_front(n);
    //    n->child_it = parent->children.begin();
        assert(n->parent == parent);
    }
    assert(client.nodes[n->nodehandle] == n);
    n->setkey(reinterpret_cast<const mega::byte*>(std::string((type == mega::FILENODE) ? mega::FILENODEKEYLENGTH : mega::FOLDERNODEKEYLENGTH, 'X').c_str()));
    return *n;
}

#ifdef ENABLE_SYNC
std::unique_ptr<mega::Sync> makeSync(mega::MegaClient& client, const std::string& localname)
{
    auto sync = std::unique_ptr<mega::Sync>{new mega::Sync};
    sync->localroot = std::unique_ptr<mega::LocalNode>{new mega::LocalNode};
    sync->state = mega::SYNC_CANCELED; // to avoid the asssertion in Sync::~Sync()
    initializeLocalNode(*sync->localroot, *sync, nullptr, client.fsidnode,  mega::FOLDERNODE, localname, {});
    sync->localdebris = sync->localroot->localname + "/" + mt::gLocalDebris;
    sync->client = &client;
    client.syncs.push_front(sync.get());
    sync->sync_it = client.syncs.begin();
    return sync;
}

std::unique_ptr<mega::LocalNode> makeLocalNode(mega::Sync& sync, mega::LocalNode& parent,
                                               mega::nodetype_t type, const std::string& name,
                                               const mega::FileFingerprint& ffp)
{
    auto l = std::unique_ptr<mega::LocalNode>{new mega::LocalNode};
    initializeLocalNode(*l, sync, &parent, sync.client->fsidnode, type, name, ffp);
    return l;
}
#endif

void collectAllFsNodes(std::map<std::string, const mt::FsNode*>& nodes, const mt::FsNode& node)
{
    const auto path = node.getPath();
    assert(nodes.find(path) == nodes.end());
    nodes[path] = &node;
    if (node.getType() == mega::FOLDERNODE)
    {
        for (const auto child : node.getChildren())
        {
            collectAllFsNodes(nodes, *child);
        }
    }
}

std::uint16_t nextRandomInt()
{
    std::uniform_int_distribution<std::uint16_t> dist{0, std::numeric_limits<std::uint16_t>::max()};
    return dist(gRandomGenerator);
}

mega::byte nextRandomByte()
{
    std::uniform_int_distribution<unsigned short> dist{0, std::numeric_limits<mega::byte>::max()};
    return static_cast<mega::byte>(dist(gRandomGenerator));
}

} // mt
