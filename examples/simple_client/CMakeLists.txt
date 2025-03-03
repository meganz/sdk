
add_executable(simple_client
    simple_client.cpp
)

# Link with SDKlib.
target_link_libraries(simple_client PRIVATE MEGA::SDKlib)

# Adjust compilation flags for warnings and errors
target_platform_compile_options(
    TARGET simple_client
    WINDOWS /we4800 # Implicit conversion from 'type' to bool. Possible information loss
    UNIX $<$<CONFIG:Debug>:-ggdb3> -Wall -Wextra -Wconversion
)

if(ENABLE_SDKLIB_WERROR)
    target_platform_compile_options(
        TARGET simple_client
        WINDOWS /WX
        UNIX  $<$<CONFIG:Debug>: -Werror>
    )
endif()

# Starting on Xcode 15: https://gitlab.kitware.com/cmake/cmake/-/issues/25297
if(APPLE AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS "15.0")
    target_link_options(simple_client PRIVATE LINKER:-no_warn_duplicate_libraries)
endif()

# Copy files required by this app
add_custom_command(
    TARGET simple_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/MEGA.png
            ${CMAKE_CURRENT_BINARY_DIR}/MEGA.png
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/MEGA.png
    COMMENT "Copying files required by simple_client."
)
