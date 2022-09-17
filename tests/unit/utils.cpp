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
#include <mega/heartbeats.h>
#include <mega.h>

#include "constants.h"
#include "DefaultedFileSystemAccess.h"
#include "FsNode.h"

namespace mt {

namespace {

std::mt19937 gRandomGenerator{1};

::mega::FSACCESS_CLASS g_fsa;

} // anonymous

mega::handle nextFsId()
{
    static mega::handle fsId{0};
    return fsId++;
}

std::shared_ptr<mega::MegaClient> makeClient(mega::MegaApp& app)
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
            &app, nullptr, httpio, ::mega::make_unique<::mega::FSACCESS_CLASS>(), nullptr, nullptr, "XXX", "unit_test", 0
        }, deleter};

    return client;
}

mega::Node& makeNode(mega::MegaClient& client, const mega::nodetype_t type, mega::NodeHandle handle, mega::Node* const parent)
{
    assert(client.nodes.find(handle) == client.nodes.end());
    mega::node_vector dp;
    const auto ph = parent ? parent->nodeHandle() : ::mega::NodeHandle();
    auto n = new mega::Node{&client, &dp, handle, ph, type, -1, mega::UNDEF, nullptr, 0}; // owned by the client
    n->setkey(reinterpret_cast<const mega::byte*>(std::string((type == mega::FILENODE) ? mega::FILENODEKEYLENGTH : mega::FOLDERNODEKEYLENGTH, 'X').c_str()));
    return *n;
}

void collectAllFsNodes(std::map<mega::LocalPath, const mt::FsNode*>& nodes, const mt::FsNode& node)
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
