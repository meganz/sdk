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

std::shared_ptr<mega::MegaClient> makeClient(mega::MegaApp& app, mega::DbAccess* dbAccess)
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

    using namespace mega;
    auto waiter = std::make_shared<WAIT_CLASS>();

    std::shared_ptr<mega::MegaClient> client{new mega::MegaClient{
            &app, waiter, httpio, dbAccess, nullptr, "XXX", "unit_test", 0
        }, deleter};

    return client;
}

mega::Node& makeNode(mega::MegaClient& client, const mega::nodetype_t type, mega::NodeHandle handle, mega::Node* const parent)
{
    assert(client.nodeByHandle(handle) == nullptr);
    const auto ph = parent ? parent->nodeHandle() : ::mega::NodeHandle();
    auto n = new mega::Node{client, handle, ph, type, -1, mega::UNDEF, nullptr, 0}; // owned by the client
    if (type == mega::FILENODE || type == mega::FOLDERNODE || type == mega::TYPE_UNKNOWN)
    {
        n->setkey(reinterpret_cast<const mega::byte*>(std::string((type == mega::FILENODE) ? mega::FILENODEKEYLENGTH : mega::FOLDERNODEKEYLENGTH, 'X').c_str()));
    }

    return *n;
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
