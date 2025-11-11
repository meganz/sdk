# It allows to add different flags for a specific target depending on the architecture.
# Values are passed to target_compile_options(), so any format accepted by target_compile_options() should work.
#
#      target_arch_compile_options(
#                                TARGET <target>
#                                X86
#                                ARM32
#                                ARM64
#                                )
#
# Example:
#    target_arch_compile_options(
#        TARGET SDKlib
#        X86   -msse2
#        ARM   -mno-unaligned-access
#        ARM64 -march=armv8-a
#        )
#
function(target_arch_compile_options)

set(options "")
set(oneValueArgs TARGET)
set(multiValueArgs X86 ARM32 ARM64)

cmake_parse_arguments("target_arch_compile_options"
"${options}"
"${oneValueArgs}"
"${multiValueArgs}"
${ARGN}
)

set(proc "")

if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_GENERATOR_PLATFORM)
    set(proc "${CMAKE_GENERATOR_PLATFORM}")

elseif(CMAKE_SYSTEM_NAME STREQUAL "Android" AND DEFINED ANDROID_ABI)
    set(proc "${ANDROID_ABI}")

elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(GET CMAKE_OSX_ARCHITECTURES 0 proc)

else()
    if(CMAKE_SYSTEM_PROCESSOR)
        set(proc "${CMAKE_SYSTEM_PROCESSOR}")
    else()
        set(proc "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
endif()

string(TOLOWER "${proc}" proc)
message(STATUS "Detected architecture: ${proc}")

if(proc MATCHES "(86|x86|amd64|x64)" AND target_arch_compile_options_X86)
    target_compile_options(${target_arch_compile_options_TARGET} PRIVATE ${target_arch_compile_options_X86})
endif()

# Prefer 64-bit match first to avoid accidental 32-bit matches
if(proc MATCHES "^(aarch64|arm64|arm64e|armv8.*)$" AND target_arch_compile_options_ARM64)
    target_compile_options(${target_arch_compile_options_TARGET} PRIVATE ${target_arch_compile_options_ARM64})
elseif(proc MATCHES "^(arm$|armv[4-7].*|armhf|armeabi.*|armel)$" AND target_arch_compile_options_ARM32)
    target_compile_options(${target_arch_compile_options_TARGET} PRIVATE ${target_arch_compile_options_ARM32})
endif()

endfunction()
