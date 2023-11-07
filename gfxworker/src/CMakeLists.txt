add_library(libgfxworker STATIC
            ${GFXWORKER_DIR}/include/gfxworker/logger.h
            ${GFXWORKER_DIR}/include/gfxworker/comms_server_win32.h
            ${GFXWORKER_DIR}/include/gfxworker/server.h
            ${GFXWORKER_DIR}/include/gfxworker/threadpool.h
            ${GFXWORKER_DIR}/include/gfxworker/arguments.h
            ${GFXWORKER_DIR}/src/logger.cpp
            ${GFXWORKER_DIR}/src/comms_server_win32.cpp
            ${GFXWORKER_DIR}/src/server.cpp
            ${GFXWORKER_DIR}/src/threadpool.cpp
            ${GFXWORKER_DIR}/src/arguments.cpp
            )

target_include_directories(libgfxworker PUBLIC ${GFXWORKER_DIR}/include)

target_link_libraries(libgfxworker Mega)