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
    vector<unique_ptr<Command>> cmds;
    string jsonresponse;
    JSON json;
    size_t processindex = 0;
    JSONSplitter mJsonSplitter;
    size_t mChunkedProgress = 0;

    // once we send the commands, any retry must be for exactly
    // the same JSON, or idempotence will not work properly
    mutable string cachedJSON;
    mutable string cachedIdempotenceId;
    mutable string cachedCounts;

public:
    void add(Command*);

    size_t size() const;

    string get(MegaClient* client, char reqidCounter[10], string& idempotenceId) const;

    void serverresponse(string&& movestring, MegaClient*);
    void servererror(const std::string &e, MegaClient* client);

    void process(MegaClient* client);
    bool processCmdJSON(Command* cmd, bool couldBeError, JSON& json);
    bool processSeqTag(Command* cmd, bool withJSON, bool& parsedOk, bool inSeqTagArray, JSON& processingJson);

    m_off_t processChunk(const char* chunk, MegaClient*);
    m_off_t totalChunkedProgress();

    void clear();
    bool empty() const;
    void swap(Request&);
    bool stopProcessing = false;

    bool mV3 = true;

    // if contains only one command and that command is FetchNodes
    bool isFetchNodes() const;

    Command* getCurrentCommand();
};


class MEGA_API RequestDispatcher
{
    // these ones have been sent to the server, but we haven't received the response yet
    Request inflightreq;
    retryreason_t inflightFailReason = RETRY_NONE;

    // client-server request double-buffering, in batches of up to MAX_COMMANDS
    deque<Request> nextreqs;

    // flags for dealing with resetting everything from a command in progress
    bool processing = false;
    bool clearWhenSafe = false;

    static const int MAX_COMMANDS = 10000;

    // unique request ID
    char reqid[10];

public:
    RequestDispatcher(PrnGen&);

    // Queue a command to be send to MEGA. Some commands must go in their own batch (in case other commands fail the whole batch), determined by the Command's `batchSeparately` field.
    void add(Command*);

    // Commands are waiting and could be sent (could be a retry if connection failed etc) (they are not already sent, not awaiting response)
    bool readyToSend() const;

    // True if we started sending a Request and haven't received a server response yet,
    // and stays true even through network errors, -3, retries, etc until we get that response
    bool cmdsInflight() const;

    Command* getCurrentCommand(bool currSeqtagSeen);

    /**
     * @brief get the set of commands to be sent to the server (could be a retry)
     * @param includesFetchingNodes set to whether the commands include fetch nodes
     */
    string serverrequest(bool &includesFetchingNodes, bool& v3, MegaClient* client, string& idempotenceId);

    // Once we get a successful reply from the server, call this to complete everything
    // Since we need to support idempotence, we cannot add anything more to the in-progress request
    void serverresponse(string&& movestring, MegaClient*);

    // Call this function when a chunk of data is received from the server for chunked requests
    // The return value is the number of bytes that must be discarded. The chunk in the next call
    // must start with the data that was not discarded in the previous call
    size_t serverChunk(const char *chunk, MegaClient*);

    // Amount of data consumed for chunked requests, 0 for non-chunked requests
    size_t chunkedProgress();

    // If we need to retry (eg due to networking issue, abandoned req, server refusal etc) call this and we will abandon that attempt.
    // The req will be retried via the next serverrequest(), and idempotence takes care of avoiding duplicate actions
    void inflightFailure(retryreason_t reason);

    // If the server itself reports failure, use this one to resolve (commands are all failed)
    // and we will move to the next Request
    void servererror(const std::string &e, MegaClient*);

    void continueProcessing(MegaClient* client);

    void clear();

#if defined(MEGA_MEASURE_CODE) || defined(DEBUG)
    Request deferredRequests;
    std::function<bool(Command*)> deferRequests;
    void sendDeferred();
#endif
#ifdef MEGA_MEASURE_CODE
    uint64_t csRequestsSent = 0, csRequestsCompleted = 0;
    uint64_t csBatchesSent = 0, csBatchesReceived = 0;
#endif

};

} // namespace

#endif
