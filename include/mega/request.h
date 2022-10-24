/**
 * @file mega/request.h
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

#ifndef MEGA_REQUEST_H
#define MEGA_REQUEST_H 1

#include "types.h"
#include "json.h"

namespace mega {

// API request
class MEGA_API Request
{
private:
    vector<Command*> cmds;
    string jsonresponse;
    JSON json;
    size_t processindex = 0;

public:
    void add(Command*);

    size_t size() const;

    void get(string*, bool& suppressSID) const;

    void serverresponse(string&& movestring, MegaClient*);
    void servererror(const std::string &e, MegaClient* client);

    void process(MegaClient* client);
    bool processCmdJSON(Command* cmd);

    void clear();
    bool empty() const;
    void swap(Request&);
    bool stopProcessing = false;

    // if contains only one command and that command is FetchNodes
    bool isFetchNodes() const;

    Command* getCurrentCommand();
};


class MEGA_API RequestDispatcher
{
    // these ones have been sent to the server, but we haven't received the response yet
    Request inflightreq;

    // client-server request double-buffering, in batches of up to MAX_COMMANDS
    deque<Request> nextreqs;

    // flags for dealing with resetting everything from a command in progress
    bool processing = false;
    bool clearWhenSafe = false;

    static const int MAX_COMMANDS = 10000;

public:
    RequestDispatcher();

    // Queue a command to be send to MEGA. Some commands must go in their own batch (in case other commands fail the whole batch), determined by the Command's `batchSeparately` field.
    void add(Command*);

    bool cmdspending() const;
    bool cmdsInflight() const;

    Command* getCurrentCommand(bool currSeqtagSeen);

    bool cmdsinflight() const { return inflightreq.size(); }

    /**
     * @brief get the set of commands to be sent to the server (could be a retry)
     * @param suppressSID
     * @param includesFetchingNodes set to whether the commands include fetch nodes
     */
    void serverrequest(string*, bool& suppressSID, bool &includesFetchingNodes);

    // once the server response is determined, call one of these to specify the results
    void requeuerequest();
    void serverresponse(string&& movestring, MegaClient*);
    void servererror(const std::string &e, MegaClient*);

    void clear();

#ifdef MEGA_MEASURE_CODE
    Request deferredRequests;
    std::function<bool(Command*)> deferRequests;
    void sendDeferred();
    uint64_t csRequestsSent = 0, csRequestsCompleted = 0;
    uint64_t csBatchesSent = 0, csBatchesReceived = 0;
#endif

};

} // namespace

#endif
