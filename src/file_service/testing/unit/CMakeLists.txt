if (NOT TARGET test_unit)
    return()
endif()

target_link_libraries(test_unit PRIVATE FileServiceHeaders)

target_sources(test_unit PRIVATE
                         avl_tree.cpp
                         avl_tree_traits.cpp
                         file_range_traits.cpp
                         file_range_tree.cpp
                         file_range_tree_traits.cpp
                         type_traits.cpp
)

