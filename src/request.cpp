/**
 * @file request.cpp
 * @brief Generic request interface
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

#include "mega/request.h"
#include "mega/command.h"
#include "mega/logging.h"
#include "mega/megaclient.h"

namespace mega {
void Request::add(Command* c)
{
    cmds.push_back(c);
}

int Request::cmdspending() const
{
    return cmds.size();
}

void Request::get(string* req) const
{
    // concatenate all command objects, resulting in an API request
    *req = "[";

    for (int i = 0; i < (int)cmds.size(); i++)
    {
        req->append(i ? ",{" : "{");
        req->append(cmds[i]->getstring());
        req->append("}");
    }

    req->append("]");
}

void Request::procresult(MegaClient* client)
{
    if (!client->json.enterarray())
    {
        LOG_err << "Invalid response from server";
    }

    for (int i = 0; i < (int)cmds.size(); i++)
    {
        client->restag = cmds[i]->tag;

        cmds[i]->client = client;

        if (client->json.enterobject())
        {
            cmds[i]->procresult();

            if (!client->json.leaveobject())
            {
                LOG_err << "Invalid object";
            }
        }
        else if (client->json.enterarray())
        {
            cmds[i]->procresult();

            if (!client->json.leavearray())
            {
                LOG_err << "Invalid array";
            }
        }
        else
        {
            cmds[i]->procresult();
        }

        if(!cmds.size())
        {
            return;
        }
    }

    clear();
}

void Request::clear()
{
    for (int i = (int)cmds.size(); i--; )
    {
        if (!cmds[i]->persistent)
        {
            delete cmds[i];
        }
    }
    cmds.clear();
}

RequestDispatcher::RequestDispatcher()
{
    r = 0;
}

void RequestDispatcher::nextRequest()
{
    r ^= 1;

    if (!reqs[r].cmdspending())
    {
        while(!reqbuf.empty() && reqs[r].cmdspending() < MAX_COMMANDS)
        {
            Command *c = reqbuf.front();
            reqbuf.pop();
            reqs[r].add(c);
            LOG_debug << "Command extracted from secondary buffer: " << reqbuf.size();
        }
    }
}

void RequestDispatcher::add(Command *c)
{
    if(reqs[r].cmdspending() < MAX_COMMANDS)
    {
        reqs[r].add(c);
    }
    else
    {
        reqbuf.push(c);
        LOG_debug << "Command added to secondary buffer: " << reqbuf.size();
    }
}

int RequestDispatcher::cmdspending() const
{
    return reqs[r].cmdspending();
}

void RequestDispatcher::get(string *out) const
{
    reqs[r].get(out);
}

void RequestDispatcher::procresult(MegaClient *client)
{
    reqs[r ^ 1].procresult(client);
}

void RequestDispatcher::clear()
{
    for (int i = sizeof(reqs)/sizeof(*reqs); i--; )
    {
        reqs[i].clear();
    }

    while (!reqbuf.empty())
    {
        Command *c = reqbuf.front();
        reqbuf.pop();

        if (!c->persistent)
        {
            delete c;
        }
    }
}

} // namespace
