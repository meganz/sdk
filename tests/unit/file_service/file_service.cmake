target_include_directories(test_unit PRIVATE file_service)

target_link_libraries(test_unit PRIVATE MEGA::FileServiceHeaderPaths)

target_sources(test_unit PRIVATE
                         file_service/avl_tree_tests.cpp
                         file_service/avl_tree_trait_tests.cpp
                         file_service/file_range_trait_tests.cpp
                         file_service/file_range_tree_tests.cpp
                         file_service/file_range_tree_trait_tests.cpp
                         file_service/type_trait_tests.cpp
)
