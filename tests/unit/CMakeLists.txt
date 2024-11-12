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
    hashcash_test.cpp
    Logging_test.cpp
    MediaProperties_test.cpp
    MegaApi_test.cpp
    name_collision_test.cpp
    PayCrypter_test.cpp
    PendingContactRequest_test.cpp
    Scoped_timer_test.cpp
    Serialization_test.cpp
    Share_test.cpp
    Sync_conflict_test.cpp
    Sync_test.cpp
    TextChat_test.cpp
    Transfer_test.cpp
    Transferstats_test.cpp
    User_test.cpp
    user_attributes_test.cpp
    utils.cpp
    utils_test.cpp
    pwm_file_parser_test.cpp
    pwm_import_validation.cpp
)

target_sources_conditional(test_unit
    FLAG ENABLE_ISOLATED_GFX
    PRIVATE
    Isolatedprocess_test.cpp
    GfxCommands_test.cpp
)

# Link with SDKlib
target_link_libraries(test_unit PRIVATE MEGA::SDKlib)

# Link with the common interface library for the tests.
target_link_libraries(test_unit PRIVATE MEGA::test_tools MEGA::test_common)

# Adjust compilation flags for warnings and errors
target_platform_compile_options(
    TARGET test_unit
    WINDOWS /we4800 # Implicit conversion from 'type' to bool. Possible information loss
    UNIX $<$<CONFIG:Debug>:-ggdb3> -Wall -Wextra -Wconversion
)

if(ENABLE_SDKLIB_WERROR)
    target_platform_compile_options(
        TARGET test_unit
        WINDOWS /WX
        UNIX  $<$<CONFIG:Debug>: -Werror>
    )
endif()
