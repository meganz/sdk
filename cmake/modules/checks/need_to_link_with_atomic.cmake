function(check_if_atomic_is_needed)
    try_compile(NEEDS_ATOMIC_LIB
                "${CMAKE_BINARY_DIR}"
                "${CMAKE_CURRENT_LIST_DIR}/checks/needs_atomic_lib.cpp"
                LINK_LIBRARIES atomic)

    if (NEEDS_ATOMIC_LIB)
        set(NEEDS_ATOMIC_LIB TRUE PARENT_SCOPE)
    else()
        set(NEEDS_ATOMIC_LIB FALSE PARENT_SCOPE)
    endif()
endfunction()