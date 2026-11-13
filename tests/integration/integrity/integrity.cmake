# Integrity integration-test module (SDK-6795).
#
# The upload integrity core: every upload these tests make must end with a node key whose meta-MAC
# covers every chunk of the file, checked strictly (no download gap tolerance). Built with and
# without MEGA_USE_WSUPLOAD: with it the uploads go through the WS engine, without it through the
# HTTP upload, so both token paths are covered.

target_sources(test_integration
    PRIVATE
    integrity/headers/MetaMacCheck.h
    integrity/SdkUploadIntegrityTest.cpp
)
