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
    AttrMap_test.cpp
    ChunkMacMap_test.cpp
    Commands_test.cpp
    Crypto_test.cpp
    FileFingerprint_test.cpp
    File_test.cpp
    FsNode.cpp
    Logging_test.cpp
    MediaProperties_test.cpp
    MegaApi_test.cpp
    PayCrypter_test.cpp
    PendingContactRequest_test.cpp
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

# Link with the common interface library for the tests.
target_link_libraries(test_unit PRIVATE test_common)

# Adjust compilation flags for warnigns and errors
if(WIN32)

    if(ENABLE_SDKLIB_WERROR)
        target_compile_options(test_unit PRIVATE /WX)
    endif()

else()

    target_compile_options(test_unit
        PRIVATE
        $<$<CONFIG:Debug>:-ggdb3>
        -Wall         -Wextra
        -Wconversion  -Wno-unused-parameter
    )

    if(ENABLE_SDKLIB_WERROR AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(test_unit PRIVATE -Werror)
        if(APPLE)
            target_compile_options(test_unit
                PRIVATE
                -Wno-sign-conversion                 -Wno-overloaded-virtual
                -Wno-inconsistent-missing-override   -Wno-unqualified-std-cast-call
            )
        endif()
    endif()

endif()
