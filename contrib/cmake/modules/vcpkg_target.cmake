# It sets CMAKE_SYSTEM_NAME based on target triplet
# By convention, it is based the second part of the triplet
# name. For example, the triplet arm64-ios's system name
# is ios.
#
# This function only sets for iOS
function(check_vcpkg_target_triplet)
    if(VCPKG_TARGET_TRIPLET)
        # To list
        string(REPLACE "-" ";" TARGET_LIST ${VCPKG_TARGET_TRIPLET})

        list(LENGTH TARGET_LIST LEN)

        if (${LEN} GREATER_EQUAL 2)
            list(GET TARGET_LIST 1 OS_PART)

            if(OS_PART MATCHES "^[Ii][Oo][Ss]$")
                set(CMAKE_SYSTEM_NAME iOS CACHE STRING "")
            endif()
        endif()
    endif()
endfunction()
