add_library(ccronexpr STATIC
    ccronexpr.h
    ccronexpr.cpp
)

target_include_directories(ccronexpr SYSTEM
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
