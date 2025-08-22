# It allows to add different flags for a specific target depending on the architecture.
# Values are passed to target_compile_options(), so any format accepted by target_compile_options() should work.
#
#      target_arch_compile_options(
#                                TARGET <target>
#                                X86
#                                X86_64
#                                ARM
#                                ARM64
#                                )
#
# Example:
#    target_arch_compile_options(
#        TARGET SDKlib
#        X86   -msse2
#        X86_64 -mavx2
#        ARM   -mno-unaligned-access
#        ARM64 -march=armv8-a
#        )
#
function(target_arch_compile_options)

set(options "")
set(oneValueArgs TARGET)
set(multiValueArgs X86 ARM)

cmake_parse_arguments("target_arch_compile_options"
"${options}"
"${oneValueArgs}"
"${multiValueArgs}"
${ARGN}
)

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" proc)

if(proc MATCHES "(86|x86|amd64|x64)" AND target_arch_compile_options_X86)
    target_compile_options(${target_arch_compile_options_TARGET} PRIVATE ${target_arch_compile_options_X86})
endif()

if((proc MATCHES "arm" OR proc MATCHES "aarch64") AND target_arch_compile_options_ARM)
    target_compile_options(${target_arch_compile_options_TARGET} PRIVATE ${target_arch_compile_options_ARM})
endif()

endfunction()
