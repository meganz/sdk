add_library(evt-tls OBJECT
    queue.h
    evt_tls.h
    evt_tls.cpp
)

if(TARGET PkgConfig::openssl)
    target_link_libraries(evt-tls PRIVATE PkgConfig::openssl)
elseif(TARGET OpenSSL::SSL)
    target_link_libraries(evt-tls PRIVATE OpenSSL::SSL)
else()
    message(FATAL_ERROR "evt-tls requires OpenSSL. (No PkgConfig::openssl or OpenSSL::SSL found).")
endif()

set(EVT_TLS_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include/evt-tls")
file(MAKE_DIRECTORY "${EVT_TLS_PUBLIC_INCLUDE_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/evt_tls.h"
    "${EVT_TLS_PUBLIC_INCLUDE_DIR}/evt_tls.h"
    COPYONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/queue.h"
    "${EVT_TLS_PUBLIC_INCLUDE_DIR}/queue.h"
    COPYONLY
)

target_include_directories(evt-tls SYSTEM
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
)

if(ENABLE_JAVA_BINDINGS)
    set_target_properties(evt-tls PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()
