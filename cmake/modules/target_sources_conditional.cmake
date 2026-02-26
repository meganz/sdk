# It allows to add sources which will be built only if the FLAG is true. Otherwise, the
# files will be visible in the IDE but will not be part of the target build.
# Useful for sources for different platforms, configuration files, .md files, ...
# Except for the "FLAG <condition>" field, syntax is similar to target_sources().
# If the files may be processed by the Qt preprocessors add the QT_AWARE flag, so the
# files are also excluded from AUTMOC, AUTOUIC and AUTORCC when the FLAG is false.
#
#      target_sources_conditional(<target> FLAG <condition> [QT_AWARE]
#                                <INTERFACE|PUBLIC|PRIVATE> [items1...]
#                                [<INTERFACE|PUBLIC|PRIVATE> [items2...] ...])
#
# Example:
#    target_sources_conditional(SDKlib
#        FLAG ENABLE_FEATURE AND NOT (APPLE OR WIN32)
#        QT_AWARE
#        PRIVATE
#        first_source.h
#        first_source.cpp
#        MainWindow.ui
#        PUBLIC
#        whatever.h
#    )

function(target_sources_conditional)

    set(options QT_AWARE)
    set(oneValueArgs "")
    set(multiValueArgs FLAG INTERFACE PUBLIC PRIVATE)

    set(TARGET ${ARGV0})

    cmake_parse_arguments(PARSE_ARGV 1 "target_sources_conditional"
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
    )

    target_sources(${TARGET}
        INTERFACE ${target_sources_conditional_INTERFACE}
        PUBLIC ${target_sources_conditional_PUBLIC}
        PRIVATE ${target_sources_conditional_PRIVATE}
    )

    if(NOT (${target_sources_conditional_FLAG}))
        set_source_files_properties(${target_sources_conditional_INTERFACE} PROPERTIES HEADER_FILE_ONLY TRUE)
        set_source_files_properties(${target_sources_conditional_PUBLIC} PROPERTIES HEADER_FILE_ONLY TRUE)
        set_source_files_properties(${target_sources_conditional_PRIVATE} PROPERTIES HEADER_FILE_ONLY TRUE)
        if(${target_sources_conditional_QT_AWARE})
            set_source_files_properties(${target_sources_conditional_INTERFACE} PROPERTIES SKIP_AUTOGEN ON)
            set_source_files_properties(${target_sources_conditional_PUBLIC} PROPERTIES SKIP_AUTOGEN ON)
            set_source_files_properties(${target_sources_conditional_PRIVATE} PROPERTIES SKIP_AUTOGEN ON)
        endif()
    endif()

endfunction()
