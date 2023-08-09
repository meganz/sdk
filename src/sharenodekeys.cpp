/**
 * @file sharenodekeys.cpp
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

#include "mega/sharenodekeys.h"
#include "mega/node.h"
#include "mega/base64.h"
#include "mega/megaclient.h"
#include "mega/command.h"
#include "mega/logging.h"

namespace mega {
// add share node and return its index
int ShareNodeKeys::addshare(std::shared_ptr<Node> sn)
{
    for (int i = static_cast<int>(shares.size()); i--;)
    {
        if (shares[i] == sn)
        {
            return i;
        }
    }

    shares.push_back(sn);

    return static_cast<int>(shares.size() - 1);
}

void ShareNodeKeys::add(std::shared_ptr<Node> n, std::shared_ptr<Node> sn, bool includeParentChain)
{
    if (!sn)
    {
        sn = n;
    }

    if (n->attrstring)  // invalid nodekey or undecryptable attributes
    {
        LOG_err << "Skip CR request for node: " << toNodeHandle(n->nodehandle) << " (invalid node key)";
        return;
    }

    add(n->nodekey(), n->nodehandle, sn, includeParentChain);
}

// add a nodecore (!sn: all relevant shares, otherwise starting from sn, fixed: only sn)
void ShareNodeKeys::add(const string& nodekey, handle nodehandle, std::shared_ptr<Node> sn, bool includeParentChain, const byte* item, int itemlen)
{
    char buf[96];
    char* ptr;
    byte key[FILENODEKEYLENGTH];

    int addnode = 0;

    // emit all share nodekeys for known shares
    do {
        if (sn->sharekey)
        {
            snprintf(buf, sizeof(buf), ",%d,%d,\"", addshare(sn), (int)items.size());

            sn->sharekey->ecb_encrypt((byte*)nodekey.data(), key, nodekey.size());

            ptr = strchr(buf + 5, 0);
            ptr += Base64::btoa(key, int(nodekey.size()), ptr);
            *ptr++ = '"';

            keys.append(buf, ptr - buf);
            addnode = 1;
        }
    } while (includeParentChain && (sn = sn->parent));

    if (addnode)
    {
        items.resize(items.size() + 1);

        if (item)
        {
            items[items.size() - 1].assign((const char*)item, itemlen);
        }
        else
        {
            items[items.size() - 1].assign((const char*)&nodehandle, MegaClient::NODEHANDLE);
        }
    }
}

void ShareNodeKeys::get(Command* c, bool skiphandles)
{
    if (keys.size())
    {
        c->beginarray("cr");

        // emit share node handles
        c->beginarray();
        for (unsigned i = 0; i < shares.size(); i++)
        {
            c->element((const byte*)&shares[i]->nodehandle, MegaClient::NODEHANDLE);
        }

        c->endarray();

        // emit item handles (can be node handles or upload tokens)
        c->beginarray();

        if (!skiphandles)
        {
            for (unsigned i = 0; i < items.size(); i++)
            {
                c->element((const byte*)items[i].c_str(), int(items[i].size()));
            }
        }

        c->endarray();

        // emit linkage/keys
        c->beginarray();
        c->appendraw(keys.c_str() + 1, int(keys.size() - 1));
        c->endarray();

        c->endarray();
    }
}
} // namespace
