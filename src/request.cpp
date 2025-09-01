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

bool Request::isFetchNodes() const
{
    return cmds.size() == 1 && dynamic_cast<CommandFetchNodes*>(cmds.back().get());
}

void Request::add(Command* c)
{
    // Once this becomes the in-progress request, it must not have anything added
    assert(cachedJSON.empty());

    cmds.push_back(unique_ptr<Command>(c));
}

size_t Request::size() const
{
    return cmds.size();
}

string Request::get(MegaClient* client, char reqidCounter[10], string& idempotenceId) const
{
    if (cachedJSON.empty())
    {
        // concatenate all command objects, resulting in an API request
        string& req = cachedJSON;
        req = "[";

        map<string, int> counts;

        for (int i = 0; i < (int)cmds.size(); i++)
        {
            req.append(i ? ",{" : "{");
            req.append(cmds[static_cast<size_t>(i)]->getJSON(client));
            req.append("}");
            ++counts[cmds[static_cast<size_t>(i)]->commandStr];
        }

        req.append("]");

        for (auto& e : counts)
        {
            if (!cachedCounts.empty()) cachedCounts += " ";
            cachedCounts += e.first + ":" + std::to_string(e.second);
        }

        // increment unique request ID
        for (int i = 10; i--; )
        {
            if (reqidCounter[i]++ < 'z')
            {
                break;
            }
            else
            {
                reqidCounter[i] = 'a';
            }
        }
        cachedIdempotenceId = string(reqidCounter, 10);
    }

    // once we send the commands, any retry must be for exactly
    // the same JSON, or idempotence will not work properly
    LOG_debug << "Req command counts: " << cachedCounts;
    idempotenceId = cachedIdempotenceId;
    return cachedJSON;
}

bool Request::processCmdJSON(Command* cmd, bool couldBeError, JSON& jsonResponse)
{
    Error e;
    if (couldBeError && cmd->checkError(e, jsonResponse))
    {
        return cmd->procresult(Command::Result(Command::CmdError, e), jsonResponse);
    }
    else if (jsonResponse.enterobject())
    {
        return cmd->procresult(Command::CmdObject, jsonResponse) && jsonResponse.leaveobject();
    }
    else if (jsonResponse.enterarray())
    {
        return cmd->procresult(Command::CmdArray, jsonResponse) && jsonResponse.leavearray();
    }
    else
    {
        return cmd->procresult(Command::CmdItem, jsonResponse);
    }
}

bool Request::processSeqTag(Command* cmd, bool withJSON, bool& parsedOk, bool inSeqTagArray, JSON& processingJson)
{
    string st;
    processingJson.storeobject(&st);

    if (inSeqTagArray)
    {
        if (*processingJson.pos == ',') ++processingJson.pos;
    }

    // In normal operation, we get called once to figure out the `st` to watch out for in sc json (else case below)
    // then the sc processing will process actionpackets up to and including the one(s) with that `st`
    // and only after that, call us a second time (with mCurrentSeqtagSeen true), and we will execute the if block.
    // And one additional case to consider, before we start the sc channel, it's ok to send non-actionpacket (ie, non-st) requests (hence scsn check)

    if (cmd->client->mCurrentSeqtag == st ||
        !cmd->client->scsn.ready())  // if we have not started the sc channel, we won't receive the matching st, so ignore st until we do
    {
        if (!cmd->client->scsn.ready())
        {
            LOG_verbose << "Processing command result regardless of st as we don't have scsn yet. " << st << cmd->client->mCurrentSeqtag;
        }

        assert(cmd->client->mCurrentSeqtagSeen || !cmd->client->scsn.ready());
        cmd->client->mCurrentSeqtag.clear();
        cmd->client->mCurrentSeqtagSeen = false;
        parsedOk = withJSON ? processCmdJSON(cmd, false, processingJson)
                            : cmd->procresult(Command::Result(Command::CmdError, API_OK), processingJson); // just an `st` returned is implicitly successful
        return true;
    }
    else
    {
        // result processing paused until we encounter and process actionpackets matching client->mCurrentSeqtag
        cmd->client->mPriorSeqTag = cmd->client->mCurrentSeqtag;
        cmd->client->mCurrentSeqtag = st;
        cmd->client->mCurrentSeqtagSeen = false;
        cmd->client->mCurrentSeqtagCmdtag = cmd->tag;
        assert(cmd->client->mPriorSeqTag.size() < cmd->client->mCurrentSeqtag.size() ||
              (cmd->client->mPriorSeqTag.size() == cmd->client->mCurrentSeqtag.size() &&
               cmd->client->mPriorSeqTag < cmd->client->mCurrentSeqtag));
        return false;
    }
}

m_off_t Request::processChunk(const char* chunk, MegaClient *client)
{
    if (stopProcessing || cmds.size() != 1)
    {
        clear();
        return 0;
    }

    // Only fetchnodes command is currently supported
    assert(isFetchNodes());

    m_off_t consumed = 0;
    Command& cmd = *cmds[0];
    client->restag = cmd.tag;
    cmd.client = client;

    bool start = !json.pos;
    json.begin(chunk);

    if (start)
    {
        if (!json.enterarray())
        {
            // Request-level error
            clear();
            return 0;
        }
        consumed++;
        assert(mJsonSplitter.isStarting());
    }

    consumed += mJsonSplitter.processChunk(&cmd.mFilters, json.pos);
    if (mJsonSplitter.hasFailed())
    {
        // stop the processing
        cmds[0].reset();
        clear();
        return 0;
    }

    mChunkedProgress += static_cast<size_t>(consumed);
    json.begin(chunk + consumed);
    if (mJsonSplitter.hasFinished())
    {
        if (!json.leavearray())
        {
            LOG_err << "Unexpected end of JSON stream: " << json.pos;
            assert(false);
        }
        else
        {
            consumed++;
        }
        assert(!chunk[consumed]);

        cmds[0].reset();
        clear();
    }

    return consumed;
}

m_off_t Request::totalChunkedProgress()
{
    return static_cast<m_off_t>(mChunkedProgress);
}

void Request::process(MegaClient* client)
{
    TransferDbCommitter committer(client->tctable);
    client->mTctableRequestCommitter = &committer;

    JSON processingJson = json;
    for (; processindex < cmds.size() && !stopProcessing; processindex++)
    {
        Command* cmd = cmds[processindex].get();

        client->restag = cmd->tag;

        cmd->client = client;

        auto cmdJSON = processingJson;
        bool parsedOk = true;

        if (*processingJson.pos == ',') ++processingJson.pos;

        if (cmd->mSeqtagArray && processingJson.enterarray())
        {
            // Some commands need to return seqtag and also some JSON,
            // in which case they are in an array with `st` first, and the JSON second
            // Some commands might or might not produce `st`.  And might return a string.
            // So in the case of success with a string return, but no `st`,
            // the array is [0, "returnValue"]
            // If the command failed, there is no array, just the error code
            assert(*processingJson.pos == '0' || *processingJson.pos == '\"');
            if (*processingJson.pos == '0' && *(processingJson.pos+1) == ',')
            {
                processingJson.pos += 2;
                parsedOk = processCmdJSON(cmd, false, processingJson);
            }
            else if (!processSeqTag(cmd, true, parsedOk, true, processingJson)) // executes the command's procresult if we match the seqtag
            {
                // we need to wait for sc processing to catch up with the seqtag we just read
                json = cmdJSON;
                return;
            }

            if (parsedOk && !processingJson.leavearray())
            {
                LOG_err << "Invalid seqtag array";
                parsedOk = false;
            }
        }
        else if (*processingJson.pos == '"')
        {
            // For v3 commands, a string result is a string which is a seqtag.
            if (!processSeqTag(cmd, false, parsedOk, false, processingJson))
            {
                // we need to wait for sc processing to catch up with the seqtag we just read
                json = cmdJSON;
                return;
            }
        }
        else
        {
            // straightforward case - plain JSON response, no seqtag
            parsedOk = processCmdJSON(cmd, true, processingJson);
        }

        if (!parsedOk)
        {
            LOG_err << "JSON for that command was not recognised/consumed properly, adjusting";
            processingJson = cmdJSON;
            processingJson.storeobject();

            // alert devs to the JSON problem (bad JSON from server, or bad parsing of it) immediately
            assert(false);
        }
        else
        {
#ifdef DEBUG
            // double check the command consumed the right amount of JSON
            cmdJSON.storeobject();
            if (processingJson.pos != cmdJSON.pos)
            {
                assert(processingJson.pos == cmdJSON.pos);
            }
#endif
        }

        // delete the command as soon as it's not needed anymore
        cmds[processindex].reset();
    }

    json = processingJson;
    if (processindex == cmds.size() || stopProcessing)
    {
        clear();
    }
    client->mTctableRequestCommitter = nullptr;
}

Command* Request::getCurrentCommand()
{
    assert(processindex < cmds.size());
    return cmds[processindex].get();
}

void Request::serverresponse(std::string&& movestring, MegaClient*)
{
    assert(processindex == 0);
    jsonresponse = std::move(movestring);
    json.begin(jsonresponse.c_str());

    if (!json.enterarray())
    {
        LOG_err << "Invalid response from server";
    }
}

void Request::servererror(const std::string& e, MegaClient* client)
{
    ostringstream s;

    s << "[";
    for (size_t i = cmds.size(); i--; )
    {
        s << e << (i ? "," : "");
    }
    s << "]";
    serverresponse(s.str(), client);
}

void Request::clear()
{
    cmds.clear();
    jsonresponse.clear();
    json.pos = NULL;
    processindex = 0;
    mJsonSplitter.clear();
    mChunkedProgress = 0;
    stopProcessing = false;
}

bool Request::empty() const
{
    return cmds.empty();
}

void Request::swap(Request& r)
{
    // we use swap to move between queues, but process only after it gets into the completedreqs
    cmds.swap(r.cmds);

    std::swap(cachedJSON, r.cachedJSON);
    std::swap(cachedIdempotenceId, r.cachedIdempotenceId);
    std::swap(cachedCounts, r.cachedCounts);

    // Although swap would usually swap all fields, these must be empty anyway
    // If swap was used when these were active, we would be moving needed info out of the request-in-progress
    assert(jsonresponse.empty() && r.jsonresponse.empty());
    assert(json.pos == NULL && r.json.pos == NULL);
    assert(processindex == 0 && r.processindex == 0);
}

RequestDispatcher::RequestDispatcher(PrnGen& rng)
{
    // initialize random API request sequence ID (server API is idempotent)
    resetId(reqid, sizeof reqid, rng);

    nextreqs.push_back(Request());
}

#if defined(MEGA_MEASURE_CODE) || defined(DEBUG)
void RequestDispatcher::sendDeferred()
{
    if (!nextreqs.back().empty())
    {
        LOG_debug << "sending deferred requests";
        nextreqs.push_back(Request());
    }
    nextreqs.back().swap(deferredRequests);
}
#endif

void RequestDispatcher::add(Command *c)
{
#if defined(MEGA_MEASURE_CODE) || defined(DEBUG)
    if (deferRequests && deferRequests(c))
    {
        LOG_debug << "deferring request";
        deferredRequests.add(c);
        return;
    }
#endif

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

bool RequestDispatcher::readyToSend() const
{
    if (!inflightreq.empty())
    {
        // retry of prior attempt. Otherwise, we are waiting response, so not ready
        return inflightFailReason != RETRY_NONE;
    }
    else
    {
        return nextreqs.empty() ? false :
              !nextreqs.front().empty();
    }
}

bool RequestDispatcher::cmdsInflight() const
{
    // stays true even through network errors, -3, retries, etc until we get that response
    return !inflightreq.empty();
}

Command* RequestDispatcher::getCurrentCommand(bool currSeqtagSeen)
{
    return currSeqtagSeen ? inflightreq.getCurrentCommand() : nullptr;
}

string RequestDispatcher::serverrequest(bool& includesFetchingNodes,
                                        MegaClient* client,
                                        string& idempotenceId)
{
    if (!inflightreq.empty() && inflightFailReason != RETRY_NONE)
    {
        // this is a retry after connection failure
        // everything is already set up, JSON is cached etc.
        LOG_debug << "cs Retrying the last request after code: " << inflightFailReason;
    }
    else
    {
        assert(inflightreq.empty());
        inflightreq.swap(nextreqs.front());
        nextreqs.pop_front();
        if (nextreqs.empty())
        {
            nextreqs.push_back(Request());
        }
    }
    string requestJSON = inflightreq.get(client, reqid, idempotenceId);
    includesFetchingNodes = inflightreq.isFetchNodes();
#ifdef MEGA_MEASURE_CODE
    csRequestsSent += inflightreq.size();
    csBatchesSent += 1;
#endif
    inflightFailReason = RETRY_NONE;
    return requestJSON;
}

void RequestDispatcher::inflightFailure(retryreason_t reason)
{
#ifdef MEGA_MEASURE_CODE
    csBatchesReceived += 1;
#endif
    assert(!inflightreq.empty());
    assert(!nextreqs.empty());

    // we keep inflightreq as it needs to be resent exactly as it was, for idempotence
    // just track whether we do need to resend, for cmdsInflight() signal
    assert(reason != RETRY_NONE);
    inflightFailReason = reason;
}

void RequestDispatcher::serverresponse(std::string&& movestring, MegaClient *client)
{
    CodeCounter::ScopeTimer ccst(client->performanceStats.csResponseProcessingTime);

#ifdef MEGA_MEASURE_CODE
    csBatchesReceived += 1;
    csRequestsCompleted += inflightreq.size();
#endif
    processing = true;
    inflightreq.serverresponse(std::move(movestring), client);
    inflightreq.process(client);
    processing = false;
    if (clearWhenSafe)
    {
        clear();
    }
}

size_t RequestDispatcher::serverChunk(const char *chunk, MegaClient *client)
{
    processing = true;
    size_t consumed = static_cast<size_t>(inflightreq.processChunk(chunk, client));
    processing = false;
    if (clearWhenSafe)
    {
        clear();
    }
    return consumed;
}

size_t RequestDispatcher::chunkedProgress()
{
    return static_cast<size_t>(inflightreq.totalChunkedProgress());
}

void RequestDispatcher::servererror(const std::string& e, MegaClient *client)
{
    // notify all the commands in the batch of the failure
    // so that they can deallocate memory, take corrective action etc.
    processing = true;
    inflightreq.servererror(e, client);
    inflightreq.process(client);
    assert(inflightreq.empty());
    inflightFailReason = RETRY_NONE;
    processing = false;
    if (clearWhenSafe)
    {
        clear();
    }
}

void RequestDispatcher::continueProcessing(MegaClient* client)
{
    assert(!inflightreq.empty());
    processing = true;
    inflightreq.process(client);
    processing = false;
    if (clearWhenSafe)
    {
        clear();
    }
}

void RequestDispatcher::clear()
{
    if (processing)
    {
        // we are being called from a command that is in progress (eg. logout) - delay wiping the data structure until that call ends.
        clearWhenSafe = true;
        inflightreq.stopProcessing = true;
    }
    else
    {
        inflightreq.clear();
        inflightFailReason = RETRY_NONE;
        for (auto& r : nextreqs)
        {
            r.clear();
        }
        nextreqs.clear();
        nextreqs.push_back(Request());
        processing = false;
        clearWhenSafe = false;
    }
}

} // namespace
