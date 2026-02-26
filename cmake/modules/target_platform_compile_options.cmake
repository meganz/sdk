# It allows to add different flags for a specific target depending on the OS.
# Since order matters to apply the flags, note that UNIX is applied before APPLE.
# Values are passed to target_compile_options(), so any format accepted by target_compile_options() should work.
#
#      target_platform_compile_options(
#                                TARGET <target>
#                                WINDOWS
#                                UNIX
#                                APPLE
#                                )
#
# Example:
#    target_platform_compile_options(
#        TARGET SDKlib
#        WINDOWS /WX
#        UNIX -Wall $<$<CONFIG:Debug>:-Werror>
#        APPLE $<$<CONFIG:Debug>: -Wno-sign-conversion>
#        )
#
function(target_platform_compile_options)

set(options "")
set(oneValueArgs TARGET)
set(multiValueArgs WINDOWS UNIX APPLE)

cmake_parse_arguments("target_platform_compile_options"
"${options}"
"${oneValueArgs}"
"${multiValueArgs}"
${ARGN}
)

if(WIN32 AND target_platform_compile_options_WINDOWS)
    target_compile_options(${target_platform_compile_options_TARGET} PRIVATE ${target_platform_compile_options_WINDOWS})
endif()

if(UNIX AND target_platform_compile_options_UNIX)
    target_compile_options(${target_platform_compile_options_TARGET} PRIVATE ${target_platform_compile_options_UNIX})
endif()

if(APPLE AND target_platform_compile_options_APPLE)
    target_compile_options(${target_platform_compile_options_TARGET} PRIVATE ${target_platform_compile_options_APPLE})
endif()

endfunction()
