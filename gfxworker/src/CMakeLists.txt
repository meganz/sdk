add_library(libgfxworker STATIC
            ${GFXWORKER_DIR}/src/logger.h
            ${GFXWORKER_DIR}/src/comms_server_win32.h
            ${GFXWORKER_DIR}/src/server.h
            ${GFXWORKER_DIR}/src/threadpool.h
            ${GFXWORKER_DIR}/src/logger.cpp
            ${GFXWORKER_DIR}/src/comms_server_win32.cpp
            ${GFXWORKER_DIR}/src/server.cpp
            ${GFXWORKER_DIR}/src/threadpool.cpp
            )

target_include_directories(libgfxworker PUBLIC ${GFXWORKER_DIR}/src)

target_link_libraries(libgfxworker Mega)

add_executable(gfxworker ${GFXWORKER_DIR}/src/gfxworker_main.cpp)

target_link_libraries(gfxworker Mega libgfxworker)