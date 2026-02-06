add_library(evt-tls STATIC
    evt-tls/queue.h
    evt-tls/evt_tls.h
    src/evt_tls.cpp
)

if(TARGET PkgConfig::openssl)
    target_link_libraries(evt-tls PRIVATE PkgConfig::openssl)
elseif(TARGET OpenSSL::SSL)
    target_link_libraries(evt-tls PRIVATE OpenSSL::SSL)
else()
    message(FATAL_ERROR "evt-tls requires OpenSSL. (No PkgConfig::openssl or OpenSSL::SSL found).")
endif()

target_include_directories(evt-tls SYSTEM
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if (ENABLE_JAVA_BINDINGS)
    set_target_properties(evt-tls PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()
