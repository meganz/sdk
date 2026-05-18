# wsupload integration-test module.
#
# WS-upload test bodies and supporting helpers extracted from SdkTest_test.cpp
# (G3, fu7-7). Tests rename from SdkTest.SdkWsUploadX to SdkWsUploadTest.X.
#
# Module is gated by MEGA_USE_WSUPLOAD because every test here exercises the
# WS upload path; consumers of test_integration with MEGA_USE_WSUPLOAD=OFF would
# never run these.

target_sources_conditional(test_integration
    FLAG MEGA_USE_WSUPLOAD
    PRIVATE
    wsupload/headers/SdkWsUploadTest.h
    wsupload/SdkWsUploadTest.cpp
    wsupload/headers/ScopedUploadSpeedLimit.h
    wsupload/headers/SecondTimer.h
    wsupload/headers/TransferTempErrorTracker.h
    wsupload/headers/WsChunkSendOverquotaCapture.h
    wsupload/headers/WsUploadDebugHelpers.h
    wsupload/headers/WsUploadHookGate.h
    wsupload/headers/WsUploadRetryTracker.h
    wsupload/headers/WsUploadTestHelpers.h
    wsupload/headers/WsUploadTransitionCapture.h
    wsupload/headers/WsUscCommand.h
    wsupload/WsUscCommand.cpp
)

target_include_directories(test_integration PRIVATE
    $<$<BOOL:${MEGA_USE_WSUPLOAD}>:${CMAKE_CURRENT_SOURCE_DIR}>
)
