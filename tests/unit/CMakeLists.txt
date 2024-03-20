add_executable(test_unit)

target_sources(test_unit
    PRIVATE
    constants.h
    DefaultedDbTable.h
    DefaultedDirAccess.h
    DefaultedFileAccess.h
    DefaultedFileSystemAccess.h
    FsNode.h
    NotImplemented.h
    utils.h

    main.cpp
    Arguments_test.cpp
    AttrMap_test.cpp
    CacheLRU_test.cpp
    ChunkMacMap_test.cpp
    Commands_test.cpp
    Crypto_test.cpp
    FileFingerprint_test.cpp
    File_test.cpp
    FsNode.cpp
    GfxCommands_test.cpp
    Logging_test.cpp
    MediaProperties_test.cpp
    MegaApi_test.cpp
    PayCrypter_test.cpp
    PendingContactRequest_test.cpp
    Scoped_timer_test.cpp
    Serialization_test.cpp
    Share_test.cpp
    Sync_conflict_test.cpp
    Sync_test.cpp
    TextChat_test.cpp
    Transfer_test.cpp
    User_test.cpp
    utils.cpp
    utils_test.cpp
)

target_sources_conditional(test_unit
    FLAG ENABLE_ISOLATED_GFX
    PRIVATE
    Isolatedprocess_test.cpp
)

# Link with SDKlib
target_link_libraries(test_unit PRIVATE MEGA::SDKlib)

# Link with the common interface library for the tests.
target_link_libraries(test_unit PRIVATE MEGA::test_common)

# Adjust compilation flags for warnings and errors
target_platform_compile_options(
    TARGET test_unit
    UNIX $<$<CONFIG:Debug>:-ggdb3> -Wall -Wextra -Wconversion -Wno-unused-parameter
)

if(ENABLE_SDKLIB_WERROR)
    target_platform_compile_options(
        TARGET test_unit
        WINDOWS /WX
        UNIX  $<$<CONFIG:Debug>: -Werror>
        APPLE $<$<CONFIG:Debug>: -Wno-sign-conversion -Wno-overloaded-virtual -Wno-inconsistent-missing-override
                                 -Wno-unqualified-std-cast-call>
    )
endif()
