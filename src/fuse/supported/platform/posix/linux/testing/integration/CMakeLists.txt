if (NOT TARGET test_integration)
    return()
endif()

target_sources(test_integration PRIVATE platform_tests.cpp)
