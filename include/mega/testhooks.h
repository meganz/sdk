/**
 * @file mega/testhooks.h
 * @brief helper classes for test cases to simulate various errors and special conditions
 *
 * (c) 2013-2017 by Mega Limited, Auckland, New Zealand
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

#ifndef MEGA_TESTHOOKS_H
#define MEGA_TESTHOOKS_H 1

#include "types.h"
#include <functional>


namespace mega {

    // These hooks allow the sdk_test project to simulate some error / retry conditions, or cause smaller download block sizes for quicker tests
    // However they do require some (minimal) extra code in the SDK.
    // The preprocessor is used to ensure that code is not present for release builds, so it can't cause problems.
    // Additionally the hooks use std::function so a suitable compiler and library are needed to leverage those tests.

#ifndef NDEBUG
    #define MEGASDK_DEBUG_TEST_HOOKS_ENABLED
#endif

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED

    struct MEGA_API HttpReq;
    class MEGA_API RaidBufferManager;
    class DebugTestHook;
    struct Transfer;
    class TransferDbCommitter;

    struct MegaTestHooks
    {
        std::function<bool(HttpReq*)> onHttpReqPost;
        std::function<void(RaidBufferManager*)> onSetIsRaid;
        std::function<void(error e)> onUploadChunkFailed;
        std::function<void(const m_off_t)> onProgressCompletedUpdate;
        std::function<void(const m_off_t)> onProgressContiguousUpdate;
        std::function<bool(Transfer*, TransferDbCommitter&)> onUploadChunkSucceeded;
        std::function<void(const double, const m_off_t, const m_off_t)> onTransferReportProgress;
        std::function<void(error e)> onDownloadFailed;
        std::function<void(std::unique_ptr<HttpReq>&)> interceptSCRequest;
        std::function<void(m_off_t&)> onLimitMaxReqSize;
        std::function<void(int&, unsigned)> onHookNumberOfConnections;
        std::function<void(bool&)> onHookDownloadRequestSingleUrl;
        std::function<void(m_time_t&)> onHookResetTransferLastAccessTime;
    };

    extern MegaTestHooks globalMegaTestHooks;

    // allow the test client to skip an actual http request, and set the results directly.  The return statement, if activated, skips the http post()
    #define DEBUG_TEST_HOOK_HTTPREQ_POST(HTTPREQPTR)  { if (globalMegaTestHooks.onHttpReqPost && globalMegaTestHooks.onHttpReqPost(HTTPREQPTR)) return; }

    // allow the test client to confirm raid/nonraid is happening, or adjust the parameters of a raid download for smaller chunks etc
    #define DEBUG_TEST_HOOK_RAIDBUFFERMANAGER_SETISRAID(RAIDBUFMGRPTR)  { if (globalMegaTestHooks.onSetIsRaid) globalMegaTestHooks.onSetIsRaid(RAIDBUFMGRPTR); }

    // watch out for upload issues
    #define DEBUG_TEST_HOOK_UPLOADCHUNK_FAILED(X)  { if (globalMegaTestHooks.onUploadChunkFailed) globalMegaTestHooks.onUploadChunkFailed(X); }

    // option to simulate something after an uploaded chunk
    #define DEBUG_TEST_HOOK_UPLOADCHUNK_SUCCEEDED(transfer, committer)  {  \
        if (globalMegaTestHooks.onUploadChunkSucceeded)  \
        {                                                \
            if (!globalMegaTestHooks.onUploadChunkSucceeded(transfer, committer)) return; \
        }}

    // get transfer progress completed updates
#define DEBUG_TEST_HOOK_ON_PROGRESS_COMPLETED_UPDATE(p) \
    { \
        if (globalMegaTestHooks.onProgressCompletedUpdate) \
        { \
            globalMegaTestHooks.onProgressCompletedUpdate(p); \
        } \
    }

    // get transfer progress contiguous updates
#define DEBUG_TEST_HOOK_ON_PROGRESS_CONTIGUOUS_UPDATE(p) \
    { \
        if (globalMegaTestHooks.onProgressContiguousUpdate) \
        { \
            globalMegaTestHooks.onProgressContiguousUpdate(p); \
        } \
    }

    // get reports counts updates
#define DEBUG_TEST_HOOK_ON_TRANSFER_REPORT_PROGRESS(p, fp, pb) \
    { \
        if (globalMegaTestHooks.onTransferReportProgress) \
        { \
            globalMegaTestHooks.onTransferReportProgress(p, fp, pb); \
        } \
    }

    // watch out for download issues
    #define DEBUG_TEST_HOOK_DOWNLOAD_FAILED(X)  { if (globalMegaTestHooks.onDownloadFailed) globalMegaTestHooks.onDownloadFailed(X); }

    // limit max request size for TransferBufferManager (non-raid) or new RaidReq
    #define DEBUG_TEST_HOOK_LIMIT_MAX_REQ_SIZE(X) { if (globalMegaTestHooks.onLimitMaxReqSize) globalMegaTestHooks.onLimitMaxReqSize(X); }

    // Ensure new RaidReq number of connections is taken from the client's number of connections
    #define DEBUG_TEST_HOOK_NUMBER_OF_CONNECTIONS(connectionsInOutVar, clientNumberOfConnections) { if (globalMegaTestHooks.onHookNumberOfConnections) globalMegaTestHooks.onHookNumberOfConnections(connectionsInOutVar, clientNumberOfConnections); }

    // For CommandGetFile, so a raided file can request the unraided copy.
#define DEBUG_TEST_HOOK_DOWNLOAD_REQUEST_SINGLEURL(singleUrlFlag) \
    { \
        if (globalMegaTestHooks.onHookDownloadRequestSingleUrl) \
            globalMegaTestHooks.onHookDownloadRequestSingleUrl(singleUrlFlag); \
    }

#define DEBUG_TEST_HOOK_RESET_TRANSFER_LASTACCESSTIME(lastAccessTime) \
    { \
        if (globalMegaTestHooks.onHookResetTransferLastAccessTime) \
            globalMegaTestHooks.onHookResetTransferLastAccessTime(lastAccessTime); \
    }

#else
    #define DEBUG_TEST_HOOK_HTTPREQ_POST(x)
    #define DEBUG_TEST_HOOK_RAIDBUFFERMANAGER_SETISRAID(x)
    #define DEBUG_TEST_HOOK_UPLOADCHUNK_FAILED(X)
    #define DEBUG_TEST_HOOK_UPLOADCHUNK_SUCCEEDED(transfer, committer)
    #define DEBUG_TEST_HOOK_DOWNLOAD_FAILED(X)
    #define DEBUG_TEST_HOOK_LIMIT_MAX_REQ_SIZE(X)
    #define DEBUG_TEST_HOOK_NUMBER_OF_CONNECTIONS(connectionsInOutVar, clientNumberOfConnections)
#define DEBUG_TEST_HOOK_ON_PROGRESS_COMPLETED_UPDATE(p)
#define DEBUG_TEST_HOOK_ON_PROGRESS_CONTIGUOUS_UPDATE(p)
#define DEBUG_TEST_HOOK_ON_TRANSFER_REPORT_PROGRESS(p, fp)
#define DEBUG_TEST_HOOK_DOWNLOAD_REQUEST_SINGLEURL(singleUrlFlag)
#define DEBUG_TEST_HOOK_RESET_TRANSFER_LASTACCESSTIME(lastAccessTime)
#endif


} // namespace

#endif
