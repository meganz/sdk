## Module to find the FreeImage library.
## There is no one in cmake and the FreeImage library don't usually have a .pc file

# Try to find the header
find_path(FreeImage_INCLUDE_DIR
    NAMES FreeImage.h
)
mark_as_advanced(FreeImage_INCLUDE_DIR)

# Try to find the library
find_library(FreeImage_LIBRARY
    NAMES freeimage libfreeimage
)
mark_as_advanced(FreeImage_LIBRARY)

file(READ "${FreeImage_INCLUDE_DIR}/FreeImage.h" FILE_CONTENTS)
string(REGEX MATCH "#define FREEIMAGE_MAJOR_VERSION *([0-9]*)" _ ${FILE_CONTENTS})
set(FreeImage_VERSION ${CMAKE_MATCH_1})
string(REGEX MATCH "#define FREEIMAGE_MINOR_VERSION *([0-9]*)" _ ${FILE_CONTENTS})
set(FreeImage_VERSION ${FreeImage_VERSION}.${CMAKE_MATCH_1})
string(REGEX MATCH "#define FREEIMAGE_RELEASE_SERIAL *([0-9]*)" _ ${FILE_CONTENTS})
set(FreeImage_VERSION ${FreeImage_VERSION}.${CMAKE_MATCH_1})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FreeImage
    REQUIRED_VARS FreeImage_INCLUDE_DIR FreeImage_LIBRARY
    VERSION_VAR FreeImage_VERSION
)

# Create target
if(FreeImage_FOUND)
    set(FreeImage_INCLUDE_DIRS ${FreeImage_INCLUDE_DIR})
    set(FreeImage_LIBRARIES ${FreeImage_LIBRARY})

    if(NOT TARGET FreeImage::FreeImage)
        add_library(FreeImage::FreeImage UNKNOWN IMPORTED)
        set_target_properties(FreeImage::FreeImage
            PROPERTIES
            IMPORTED_LOCATION  "${FreeImage_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${FreeImage_INCLUDE_DIR}")
    endif()
endif()
