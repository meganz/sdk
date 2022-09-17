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
        std::function<bool(Transfer*, TransferDbCommitter&)> onUploadChunkSucceeded;
        std::function<void(error e)> onDownloadFailed;
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

    // watch out for download issues
    #define DEBUG_TEST_HOOK_DOWNLOAD_FAILED(X)  { if (globalMegaTestHooks.onDownloadFailed) globalMegaTestHooks.onDownloadFailed(X); }


#else
    #define DEBUG_TEST_HOOK_HTTPREQ_POST(x)
    #define DEBUG_TEST_HOOK_RAIDBUFFERMANAGER_SETISRAID(x)
    #define DEBUG_TEST_HOOK_UPLOADCHUNK_FAILED(X)
    #define DEBUG_TEST_HOOK_UPLOADCHUNK_SUCCEEDED(transfer, committer)
    #define DEBUG_TEST_HOOK_DOWNLOAD_FAILED(X)
#endif


} // namespace

#endif
