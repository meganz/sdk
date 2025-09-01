if (NOT TARGET test_unit)
    return()
endif()

target_link_libraries(test_unit PRIVATE FileServiceHeaders)

target_sources(test_unit PRIVATE
                         avl_tree_tests.cpp
                         avl_tree_trait_tests.cpp
                         file_range_trait_tests.cpp
                         file_range_tree_tests.cpp
                         file_range_tree_trait_tests.cpp
                         type_trait_tests.cpp
)

