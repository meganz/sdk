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
    wsupload/SdkWsUploadTest.h
    wsupload/SdkWsUploadTest.cpp
    wsupload/WsUploadTransitionCapture.h
)

target_include_directories(test_integration PRIVATE
    $<$<BOOL:${MEGA_USE_WSUPLOAD}>:${CMAKE_CURRENT_SOURCE_DIR}>
)
