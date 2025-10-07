function(find_visual_studio_path)
    set(VISUAL_STUDIO_PATHS
        "C:\\Program Files\\Microsoft Visual Studio\\2026\\Enterprise"
        "C:\\Program Files\\Microsoft Visual Studio\\2026\\Professional" 
        "C:\\Program Files\\Microsoft Visual Studio\\2026\\Community"
        "C:\\Program Files\\Microsoft Visual Studio\\18\\Insiders"
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise"
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional"
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community"
    )

    set(VISUAL_STUDIO_FOUND FALSE)

    foreach(VISUAL_STUDIO_PATH ${VISUAL_STUDIO_PATHS})
        if(EXISTS ${VISUAL_STUDIO_PATH})
            set(VISUAL_STUDIO_PATH ${VISUAL_STUDIO_PATH} PARENT_SCOPE)
            set(VISUAL_STUDIO_FOUND TRUE)
            message(STATUS "Found Visual Studio at: ${VISUAL_STUDIO_PATH}")
            break()
        endif()
    endforeach()

    if(NOT VISUAL_STUDIO_FOUND)
        message(FATAL_ERROR "No compatible Visual Studio 2026 or 2022 installation found.")
    endif()
endfunction()