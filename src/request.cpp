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

//ProcResultQueue::entry::~entry()
//{
//    for (int i = (int)cmds.size(); i--; )
//    {
//        if (!cmds[i]->persistent)
//        {
//            delete cmds[i];
//        }
//    }
//    cmds.clear();
//}
//
//bool ProcResultQueue::entry::process(MegaClient* client)
//{
//    client->json = json;
//    for (; cmdindex < (int)cmds.size(); cmdindex++)
//    {
//        Command* cmd = cmds[cmdindex];
//
//        if (cmd->neededactionpacketcount)
//        {
//            LOG_info << "Pausing till actionpackets arrive " << cmdindex;
//
//            //std::cout << "Pausing till actionpackets arrive for index " << cmdindex << std::endl;
//            json = client->json;
//            break;  // we will try again when the actionpackets, tagged with our self value, arrive (after notification)
//        }
//
//        client->restag = cmd->tag;
//
//        cmd->client = client;
//
//        if (client->json.enterobject())
//        {
//            cmd->procresult();
//
//            if (!client->json.leaveobject())
//            {
//                LOG_err << "Invalid object";
//            }
//        }
//        else if (client->json.enterarray())
//        {
//            cmd->procresult();
//
//            if (!client->json.leavearray())
//            {
//                LOG_err << "Invalid array";
//            }
//        }
//        else
//        {
//            cmd->procresult();
//        }
//    }
//    client->json = json;
//    return cmdindex >= cmds.size();
//}
//
//bool ProcResultQueue::process(MegaClient* client)
//{
//    while (!queue.empty())
//    {
//        if (!queue.front()->process(client))
//        {
//            break;
//        }
//        delete queue.front();
//        queue.pop_front();
//    }
//    client->json.pos = NULL;
//    if (queue.empty())
//    {
//        LOG_debug << client->clientname << "no more actionpackets required, all commands satisfied";
//        //std::cout << "no more actionpackets required, all commands satisfied" << std::endl;
//        client->readnodes_newnodes.clear();  // ensure this doesn't contain any old data
//    }
//    return queue.empty();
//}
//
//void ProcResultQueue::ap_arrived(const string& tag, MegaClient* client, Request& sentcmds)
//{
//    bool mustnotmatchanythingelse = false;
//    for (deque<entry*>::iterator it = queue.begin(); it != queue.end(); ++it)
//    {
//        for (unsigned i = (*it)->cmdindex; i < (*it)->cmds.size(); ++i)
//        {
//            Command* cmd = (*it)->cmds[i];
//            if (!cmd->neededactionpacketcount || mustnotmatchanythingelse)
//            {
//                if (!(tag != cmd->uniqueid))
//                {
//                    assert(false);
//                }
//            }
//            else if (tag == cmd->uniqueid)
//            {
//                if (!--cmd->neededactionpacketcount)
//                {
//                    LOG_info << "Unpausing as actionpacket arrived at " << i;
//                    //std::cout << "Unpausing as actionpacket arrived at " << i << std::endl;
//                    process(client);
//                }
//                return;
//            }
//            else
//            {
//                mustnotmatchanythingelse = true;  // todo: should not be needed after converting all relevant commands to ap only, no local mods
//            }
//        }
//    }
//
//    // the sc actionpackets could arrive before the cs response
//    for (unsigned i = 0; i < sentcmds.cmds.size(); ++i)
//    {
//        Command* cmd = sentcmds.cmds[i];
//        if (!sentcmds.cmds[i]->neededactionpacketcount || mustnotmatchanythingelse)
//        {
//            assert(tag != cmd->uniqueid);
//        }
//        else if (tag == cmd->uniqueid)
//        {
//            --cmd->neededactionpacketcount;
//            LOG_info << "Actionpacket arrived before cs response at index " << i;
//            return;
//        }
//        else
//        {
//            mustnotmatchanythingelse = true;
//        }
//    }
//
//    if (!mustnotmatchanythingelse)
//    {
//        assert(false);  // somehow we missed an ap - can they be non-sequential or not delivered?
//    };   // eg. 't' node (often from putnodes) but actually from a move operation in this case
//}
//
//ProcResultQueue::~ProcResultQueue()
//{
//    while (!queue.empty())
//    {
//        delete queue.front();
//        queue.pop_front();
//    }
//}


void Request::add(Command* c)
{
    cmds.push_back(c);
}

size_t Request::size() const
{
    return cmds.size();
}

void Request::get(string* req, bool& suppressSID) const
{
    // concatenate all command objects, resulting in an API request
    *req = "[";

    for (int i = 0; i < (int)cmds.size(); i++)
    {
        req->append(i ? ",{" : "{");
        req->append(cmds[i]->getstring());
        req->append("}");
        suppressSID = suppressSID && cmds[i]->suppressSID;
    }

    req->append("]");
}

void Request::process(MegaClient* client)
{
    client->json = json;
    for (; processindex < (int)cmds.size(); processindex++)
    {
        Command* cmd = cmds[processindex];

        // in future we may exit this loop early here, when processing actionpackets associated with the command

        client->restag = cmd->tag;

        cmd->client = client;

        if (client->json.enterobject())
        {
            cmd->procresult();

            if (!client->json.leaveobject())
            {
                LOG_err << "Invalid object";
            }
        }
        else if (client->json.enterarray())
        {
            cmd->procresult();

            if (!client->json.leavearray())
            {
                LOG_err << "Invalid array";
            }
        }
        else
        {
            cmd->procresult();
        }
    }
    json = client->json;
    if (processindex == cmds.size())
    {
        clear();
    }
}

void Request::serverresponse(std::string& movestring, MegaClient* client)
{
    assert(processindex == 0);
    jsonresponse.swap(movestring);
    json.begin(jsonresponse.c_str());

    if (!json.enterarray())
    {
        LOG_err << "Invalid response from server";
    }
}

void Request::servererror(error e, MegaClient* client)
{
    ostringstream s;
    s << "[";
    for (size_t i = cmds.size(); i--; )
    {
        s << e << (i ? "," : "");
    }
    s << "]";
    string str = s.str();
    serverresponse(str, client);
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
    jsonresponse.clear();
    json.pos = NULL;
    processindex = 0;
}

bool Request::empty() const
{
    return cmds.empty();
}

void Request::swap(Request& r)
{
    // we use swap to move between queues, but process only after it gets into the completedreqs
    cmds.swap(r.cmds);
    assert(jsonresponse.empty() && r.jsonresponse.empty());
    assert(json.pos == NULL && r.json.pos == NULL);
    assert(processindex == 0 && r.processindex == 0);
}

Request::Request()
{
    json.pos = NULL;
    processindex = 0;
}

RequestDispatcher::RequestDispatcher()
{
    nextreqs.push_back(Request());
}

void RequestDispatcher::add(Command *c)
{
    if (nextreqs.back().size() >= MAX_COMMANDS)
    {
        LOG_debug << "Starting an additional Request due to MAX_COMMANDS";
        nextreqs.push_back(Request());
    }
    if (c->batchSeparately && !nextreqs.back().empty())
    {
        LOG_debug << "Starting an additional Request for a batch-separately command";
        nextreqs.push_back(Request());
    }
    nextreqs.back().add(c);
    if (c->batchSeparately)
    {
        nextreqs.push_back(Request());
    }
}

bool RequestDispatcher::cmdspending() const
{
    return !nextreqs.front().empty();
}

void RequestDispatcher::serverrequest(string *out, bool& suppressSID)
{
    assert(inflightreq.empty());
    inflightreq.swap(nextreqs.front());
    nextreqs.pop_front();
    if (nextreqs.empty())
    {
        nextreqs.push_back(Request());
    }
    inflightreq.get(out, suppressSID);
}

void RequestDispatcher::requeuerequest()
{
    assert(!inflightreq.empty());
    if (!nextreqs.front().empty())
    {
        nextreqs.push_front(Request());
    }
    nextreqs.front().swap(inflightreq);
}

void RequestDispatcher::serverresponse(std::string& movestring, MegaClient *client)
{
    inflightreq.serverresponse(movestring, client);
    inflightreq.process(client);
    assert(inflightreq.empty());
}

void RequestDispatcher::servererror(error e, MegaClient *client)
{
    // notify all the commands in the batch of the failure
    // so that they can deallocate memory, take corrective action etc.
    inflightreq.servererror(e, client);
    inflightreq.process(client);
    assert(inflightreq.empty());
}

void RequestDispatcher::clear()
{
    inflightreq.clear();
    for (deque<Request>::iterator i = nextreqs.begin(); i != nextreqs.end(); ++i)
    {
        i->clear();
    }
}

} // namespace
