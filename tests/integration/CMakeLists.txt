add_executable(test_integration)

target_sources(test_integration
    PRIVATE
    easy_curl.h
    env_var_accounts.h
    SdkTest_test.h
    SdkTestNodesSetUp.h
    SdkTestSyncNodesOperations.h
    SdkTestSyncPrevalidation.h
    test.h
    api_request_hook.h
    integration_test_utils.h
    mock_listeners.h
    sdk_test_share.h

    easy_curl.cpp
    env_var_accounts.cpp
    main.cpp
    SdkTest_test.cpp
    SdkTestGetRecentActions_test.cpp
    SdkTestNodesSetUp.cpp
    SdkTestSyncNodesOperations.cpp
    SdkTestFilter_test.cpp
    SdkTestCreateAccount_test.cpp
    SdkTestTransferStats_test.cpp
    sdk_test_user_alerts.cpp
    sdk_test_user_attributes.cpp
    SdkTestSyncRootOperations_test.cpp
    SdkTestSyncUploadThrottling_test.cpp
    SdkTestChangeLocalSyncRoot_test.cpp
    SdkTestSyncPrevalidation.cpp
    SdkTestSyncPrevalidation_test.cpp
    Sync_test.cpp
    one_question_survey_test.cpp
    sdk_test_purge_notification.cpp
    api_request_hook.cpp
    integration_test_utils.cpp
    DisableBackupSync_test.cpp
    SdkTestPersistConnections_test.cpp
    SdkTestTransferMaxSpeeds_test.cpp
    sdk_test_file_path.cpp
    sdk_test_node_tags.cpp
    sdk_test_node_tags.h
    sdk_test_network.cpp
    sdk_test_sync_upload_operations_test.cpp
    backup_test_utils.h
    sdk_test_backup_upload_operations_test.cpp
    SdkTestDeviceCenter_test.cpp
    SdkTestSyncLocalOperations_test.cpp
    SdkTestSyncNodeAttribute_test.cpp
    SdkTestSyncOverquotaReason_test.cpp
    SdkTestSyncVersionedNodeDeletion_test.cpp
    SdkTestCaseInsensitiveCollision_test.cpp
    backup_sync_operations_test.cpp
    sdk_test_lockless_cs_channel.cpp
    sdk_test_pitag.cpp
    sdk_test_sc_channel.cpp
    SdkTestFolderController_test.cpp
    SdkTestGetChildrenLexicographic_test.cpp
    SdkTestListAllNodesByPage_test.cpp
    SdkTestTransferResume_test.cpp
    SdkTestNodeDateSections_test.cpp
    sdk_test_share.cpp
    sdk_test_share_nested.cpp
    SdkTestMassiveActionPackets_test.cpp
    sdk_test_shares.cpp
    SdkTestStreamingNodeKey_test.cpp
    sdk_test_vault_access.cpp

    passwordManager/SdkTestPasswordManager.cpp
    passwordManager/SdkTestPasswordManager.h
    passwordManager/SdkTestPasswordManagerBaseNode_test.cpp
    passwordManager/SdkTestPasswordManagerImport_test.cpp
    passwordManager/SdkTestPasswordManagerPassFolderCRUD_test.cpp
    passwordManager/SdkTestPasswordManagerPassNodeCRUD_test.cpp
    passwordManager/SdkTestPasswordManagerCreditCardNodeCRUD_test.cpp
)

target_sources_conditional(test_integration
    FLAG USE_LIBUV
    PRIVATE
    sdk_server_test_utils.h
    sdk_server_test_utils.cpp
    sdk_test_file_service.cpp
    sdk_test_ftp_server.cpp
    sdk_test_http_server.cpp
)

target_sources_conditional(test_integration
    FLAG ENABLE_ISOLATED_GFX
    PRIVATE
    IsolatedGfx_test.cpp
)

# Load common test sources.
include(common/common.cmake)

# Load File Service test sources.
include(file_service/file_service.cmake)

# Load bench_framework module (gated by MEGA_BENCH_FRAMEWORK_ENABLED).
include(bench_framework/bench_framework.cmake)

# Link with SDKlib
target_link_libraries(test_integration PRIVATE MEGA::SDKlib)

# The SdkTestPersistConnections fixture opens the device-wide prefs DB
# directly via sqlite3_open to tamper an out-of-range value the public API can
# never write, exercising MegaClient::loadPersistedConnections' guard. Mirror
# the two-branch SDKlib pattern in cmake/modules/sdklib_libraries.cmake.
if(VCPKG_ROOT)
    if(IOS)
        find_package(SQLite3 REQUIRED)
        target_link_libraries(test_integration PRIVATE SQLite::SQLite3)
    else()
        find_package(unofficial-sqlite3 REQUIRED)
        target_link_libraries(test_integration PRIVATE unofficial::sqlite3::sqlite3)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(sqlite3_it REQUIRED IMPORTED_TARGET sqlite3)
    target_link_libraries(test_integration PRIVATE PkgConfig::sqlite3_it)
endif()

# Link with the common and tools interface libraries for the tests.
target_link_libraries(test_integration
    PRIVATE
    MEGA::test_tools
    MEGA::test_common
)

target_include_directories(test_integration
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Adjust compilation flags for warnings and errors
target_platform_compile_options(
    TARGET test_integration
    WINDOWS /bigobj /we4800 # Implicit conversion from 'type' to bool. Possible information loss
    UNIX $<$<CONFIG:Debug>:-ggdb3> -Wall -Wextra -Wconversion
)

if(ENABLE_SDKLIB_WERROR)
    target_platform_compile_options(
        TARGET test_integration
        WINDOWS /WX
        UNIX  $<$<CONFIG:Debug>: -Werror>
    )
endif()

# Integration tests require the following files to work
file(GLOB TESTING_AUX_FILES "test-data/*")
add_custom_command(
    TARGET test_integration POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${TESTING_AUX_FILES} $<TARGET_FILE_DIR:test_integration>
    COMMENT "Copying test files for integration tests."
)

if(TARGET gfxworker)
    add_custom_command(
        TARGET test_integration POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gfxworker> $<TARGET_FILE_DIR:test_integration>
        COMMENT "Copying gfxworker for integration tests."
    )
endif()
