add_library(libgfxworker STATIC
            ${GFXWORKER_DIR}/src/logger.h
            ${GFXWORKER_DIR}/src/server_win32.h
            ${GFXWORKER_DIR}/src/processor.h
            ${GFXWORKER_DIR}/src/thread_pool.h
            ${GFXWORKER_DIR}/src/logger.cpp
            ${GFXWORKER_DIR}/src/server_win32.cpp
            ${GFXWORKER_DIR}/src/processor.cpp
            ${GFXWORKER_DIR}/src/thread_pool.cpp
            )

target_include_directories(libgfxworker PUBLIC ${GFXWORKER_DIR}/src)

target_link_libraries(libgfxworker PUBLIC Mega)

add_executable(gfxworker ${GFXWORKER_DIR}/src/main.cpp)

target_link_libraries(gfxworker PRIVATE libgfxworker)
