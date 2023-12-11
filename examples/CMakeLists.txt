
add_executable(megacli
    megacli.h
    megacli.cpp
)

# Link with SDKlib.
# megacli does not use the intermediate layer.
target_link_libraries(megacli PRIVATE MEGA::SDKlib)

if (WIN32)
    target_compile_definitions(megacli
        PRIVATE
            NOMINMAX # TODO Fix locally
    )
endif()

## Adjust compilation flags for warnigns and errors ##
if(WIN32)

    if(ENABLE_SDKLIB_WERROR)
        target_compile_options(megacli PRIVATE /WX)
    endif()

else()

    target_compile_options(megacli
        PRIVATE
        $<$<CONFIG:Debug>:-ggdb3>
        -Wall         -Wextra
        -Wconversion  -Wno-unused-parameter
    )

    if(ENABLE_SDKLIB_WERROR AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(megacli PRIVATE -Werror)
        if(APPLE)
            target_compile_options(megacli
                PRIVATE
                -Wno-sign-conversion    -Wno-overloaded-virtual
                -Wno-string-conversion
            )
        endif()
    endif()

endif()

# Starting on Xcode 15: https://gitlab.kitware.com/cmake/cmake/-/issues/25297
if(APPLE AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS "15.0")
    target_link_options(megacli PRIVATE LINKER:-no_warn_duplicate_libraries)
endif()
