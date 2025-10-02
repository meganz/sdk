add_subdirectory(mega)

target_include_directories(SDKlib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(SDKlib PRIVATE session.cpp session_base.cpp)
