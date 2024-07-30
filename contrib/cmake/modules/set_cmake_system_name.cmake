# It sets CMAKE_SYSTEM_NAME based on target triplet.
#
# By convention, it is based the second part of the triplet name. For example,
# the triplet arm64-ios's system name is ios.
#
# This function only sets for iOS
function(set_cmake_system_name)
    if(VCPKG_TARGET_TRIPLET)
        # To list
        string(REPLACE "-" ";" TARGET_LIST ${VCPKG_TARGET_TRIPLET})

        list(LENGTH TARGET_LIST LEN)

        if (${LEN} GREATER_EQUAL 2)
            list(GET TARGET_LIST 1 SYSTEM_PART)

            if(SYSTEM_PART MATCHES "^[Ii][Oo][Ss]$")
                set(CMAKE_SYSTEM_NAME iOS CACHE STRING "")
            endif()
        endif()
    endif()
endfunction()
