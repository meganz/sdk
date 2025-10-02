# Linux definitely has the O_PATH flag.
target_compile_definitions(SDKlib PUBLIC HAS_OPEN_PATH)

# Linux may or may not have the RENAME_(EXCHANGE|NOREPLACE) flags.
try_compile(HAS_RENAME_FLAGS
            "${CMAKE_BINARY_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/checks/has_rename_flags.cpp"
)

if (HAS_RENAME_FLAGS)
    target_compile_definitions(SDKlib PUBLIC HAS_RENAME_FLAGS)
endif()

target_include_directories(SDKlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(SDKlib PRIVATE utility.cpp)

add_subdirectory(mega)
#add_subdirectory(testing)
