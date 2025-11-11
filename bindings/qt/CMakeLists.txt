add_library(SDKQtBindings)
add_library(MEGA::SDKQtBindings ALIAS SDKQtBindings)

target_sources(SDKQtBindings
    PRIVATE
    QTMegaEvent.h
    QTMegaGlobalListener.h
    QTMegaListener.h
    QTMegaRequestListener.h
    QTMegaTransferListener.h
    QTMegaApiManager.h

    QTMegaEvent.cpp
    QTMegaGlobalListener.cpp
    QTMegaListener.cpp
    QTMegaRequestListener.cpp
    QTMegaTransferListener.cpp
    QTMegaApiManager.cpp
)

# Activate the meta-object code generator for Qt.
set_target_properties(SDKQtBindings PROPERTIES AUTOMOC ON)

## Required for the Qt generated files, to be able to include other headers
target_include_directories(SDKQtBindings
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> # For the top level projects.
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}> # For the external projects.
)

find_package(Qt5 REQUIRED COMPONENTS Core)
message(STATUS "Building the SDK Qt Bindings with Qt v${Qt5_VERSION} from ${Qt5_DIR}")

target_link_libraries(SDKQtBindings
    PRIVATE
    Qt5::Core
    MEGA::SDKlib
)

## Adjust compilation flags for warnings and errors ##
target_platform_compile_options(
    TARGET SDKQtBindings
    UNIX $<$<CONFIG:Debug>:-ggdb3> -Wall -Wextra -Wconversion
)

if(ENABLE_CHATLIB_WERROR)
    target_platform_compile_options(
        TARGET SDKQtBindings
        UNIX  $<$<CONFIG:Debug>: -Werror
                                 -Wno-error=deprecated-declarations> # Kept as a warning, do not promote to error.
    )
endif()
