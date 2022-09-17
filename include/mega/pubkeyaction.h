/**
 * @file mega/pubkeyaction.h
 * @brief Classes for manipulating user's public key
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

#ifndef MEGA_PUBKEYACTION_H
#define MEGA_PUBKEYACTION_H 1

#include "mega/megaclient.h"
#include "mega/user.h"
#include "mega/node.h"

namespace mega {
// action to be performed upon arrival of a user's public key
class MEGA_API PubKeyAction
{
public:
    int tag;
    CommandPubKeyRequest *cmd;

    virtual void proc(MegaClient*, User*) = 0;

    PubKeyAction();
    virtual ~PubKeyAction() { }
};

class MEGA_API PubKeyActionCreateShare : public PubKeyAction
{
    handle h;   // node to create share on
    accesslevel_t a;    // desired access level
    string selfemail;  // optional personal representation when sharing to a non-contact
    bool mWritable = false;

    std::function<void(Error, bool writable)> completion;

public:
    void proc(MegaClient*, User*);

    PubKeyActionCreateShare(handle, accesslevel_t, int, bool writable, const char*,
	    std::function<void(Error, bool writable)> completion);
};

class MEGA_API PubKeyActionSendShareKey : public PubKeyAction
{
    handle sh;  // share node the key was requested on

public:
    void proc(MegaClient*, User*);

    PubKeyActionSendShareKey(handle);
};

class MEGA_API PubKeyActionPutNodes : public PubKeyAction
{
    vector<NewNode> nn;    // nodes to add
    CommandPutNodes::Completion completion;

public:
    void proc(MegaClient*, User*);

    PubKeyActionPutNodes(vector<NewNode>&&, int, CommandPutNodes::Completion&&);
};

class MEGA_API PubKeyActionNotifyApp : public PubKeyAction
{
public:
    void proc(MegaClient*, User*);

    PubKeyActionNotifyApp(int);
};
} // namespace

#endif
