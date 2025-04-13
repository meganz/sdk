if (NOT TARGET test_unit)
    return()
endif()

target_link_libraries(test_unit PRIVATE FileServiceHeaders)

target_sources(test_unit PRIVATE
                         type_traits.cpp
)

