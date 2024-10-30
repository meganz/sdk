function(find_visual_studio_path)
    set(VISUAL_STUDIO_PATHS
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise"
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional"
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community"
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Enterprise"
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional"
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community"
    )

    set(VISUAL_STUDIO_FOUND FALSE)

    foreach(VISUAL_STUDIO_PATH ${VISUAL_STUDIO_PATHS})
        if(EXISTS ${VISUAL_STUDIO_PATH})
            set(VISUAL_STUDIO_PATH ${VISUAL_STUDIO_PATH} PARENT_SCOPE)
            set(VISUAL_STUDIO_FOUND TRUE)
            break()
        endif()
    endforeach()

    if(NOT VISUAL_STUDIO_FOUND)
        message(FATAL_ERROR "No compatible Visual Studio 2019 or 2022 installation found.")
    endif()
endfunction()
