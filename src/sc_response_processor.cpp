#include "mega.h"
#include "mega/logging.h"
#include "mega/megaclient.h"

namespace mega
{
void MegaClient::clearSC()
{
    jsonsc_chunked.pos = NULL;
    mSCJsonSplitter.clear();
    mSCChunkedProgress = 0;
    processingSC=false;
}

void MegaClient::setupSCFilters()
{
    mSCFilters.emplace("{[a{",
                       [this](JSON* json)
                       {
                           
                        std::string tmp;
                           json->storeobject(&tmp);
                           LOG_debug << "Got action packet: " << tmp;
                           return true;
                        //    if (jsonsc_chunked.enterarray())
                        //    {
                        //        LOG_debug << "Processing action packets for "
                        //                  << string(sessionid, sizeof(sessionid));
                        //        // insca = true;

                        //        auto actionpacketStart = jsonsc_chunked.pos;
                        //        if (jsonsc_chunked.enterobject())
                        //        {
                        //            // Check if it is ok to process the current action packet.
                        //            if (!sc_checkActionPacket(lastAPDeletedNode.get()))
                        //            {
                        //                // We can't continue actionpackets until we know the next
                        //                // mCurrentSeqtag to match against, wait for the CS request
                        //                // to deliver it.
                        //                assert(reqs.cmdsInflight());
                        //                jsonsc.pos = actionpacketStart;
                        //                return false;
                        //            }
                        //        }
                        //        jsonsc.pos = actionpacketStart;

                        //        if (jsonsc.enterobject())
                        //        {
                        //            jsonsc.leaveobject();
                        //        }
                        //        else
                        //        {
                        //            // No more Actions Packets. Force it to advance and process all
                        //            // the remaining command responses until a new "st" is found, if
                        //            // any. It will also process the latest command response
                        //            // associated (by the Sequence Tag) with the latest AP processed
                        //            // here.
                        //            sc_checkSequenceTag(string());
                        //            jsonsc.leavearray();
                        //            // insca = false;
                        //        }

                        //        return true;
                        //    }
                        //    else
                        //    {
                        //        return false;
                        //    }
                       });
    mSCFilters.emplace("{w",
                       [this](JSON* json)
                       {
                        json->storeobject();
                           
                           return true;
                           return json->storeobject(&scnotifyurl);
                       });

    mSCFilters.emplace("{sn",
                       [this](JSON* json)
                       {
                        json->storeobject();
                           
                           return true;
                        
                           scsn.setScsn(json);
                           assert(!mCurrentSeqtagSeen);
                           notifypurge();
                           if (sctable) 
                           {
                               if (!pendingcs && !csretrying && !reqs.readyToSend())
                               {
                                   LOG_debug << "DB transaction COMMIT (sessionid: "
                                             << string(sessionid, sizeof(sessionid)) << ")";
                                   sctable->commit();
                                   sctable->begin();
                                   app->notify_dbcommit();
                                   pendingsccommit = false;
                               }
                               else
                               {
                                   LOG_debug << "Postponing DB commit until cs requests finish";
                                   pendingsccommit = true;
                               }
                           }

                           return true;
                       });
}

m_off_t MegaClient::processSCChunk(const char* chunk)
{
    m_off_t consumed = 0;

    bool start = !jsonsc_chunked.pos;
    jsonsc_chunked.begin(chunk);

    if (start)
    {
        if (*chunk != '{')
        {
            clearSC();
            return 0;
        }

        jsonsc_chunked.begin(chunk);
        // jsonsc_chunked.enterobject();

        // if (!jsonsc_chunked.enterarray())
        // {
        //     // Request-level error
        //     clearSC();
        //     return 0;
        // }
        // consumed++;
        assert(mSCJsonSplitter.isStarting());
    }

    consumed += mSCJsonSplitter.processChunk(&mSCFilters, jsonsc_chunked.pos);
    if (mSCJsonSplitter.hasFailed())
    {
        // stop the processing

        clearSC();
        return 0;
    }

    mSCChunkedProgress += static_cast<size_t>(consumed);
    jsonsc_chunked.begin(chunk + consumed);
    if (mSCJsonSplitter.hasFinished())
    {
        // if (!jsonsc_chunked.leavearray())
        // {
        //     LOG_err << "Unexpected end of JSON stream: " << jsonsc_chunked.pos;
        //     assert(false);
        // }
        // else
        // {
        //     consumed++;
        // }

        // if (!jsonsc_chunked.leaveobject())
        // {
        //     LOG_err << "Unexpected end of JSON stream: " << jsonsc_chunked.pos;
        //     assert(false);
        // }
        // else
        // {
        //     consumed++;
        // }

        assert(!chunk[consumed]);

        clearSC();
    }

    return consumed;
}

size_t MegaClient::chunkedSCProgress()
{
    return static_cast<size_t>(mSCChunkedProgress);
}
}