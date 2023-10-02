add_library(libgfxworker STATIC
            ${GFXWORKER_DIR}/include/gfxworker/logger.h
            ${GFXWORKER_DIR}/include/gfxworker/utils.h
            ${GFXWORKER_DIR}/include/gfxworker/comms_server_win32.h
            ${GFXWORKER_DIR}/include/gfxworker/server.h
            ${GFXWORKER_DIR}/include/gfxworker/threadpool.h
            ${GFXWORKER_DIR}/include/gfxworker/client.h
            ${GFXWORKER_DIR}/src/logger.cpp
            ${GFXWORKER_DIR}/src/utils.cpp
            ${GFXWORKER_DIR}/src/comms_server_win32.cpp
            ${GFXWORKER_DIR}/src/server.cpp
            ${GFXWORKER_DIR}/src/threadpool.cpp
            ${GFXWORKER_DIR}/src/client.cpp
            )

target_include_directories(libgfxworker PRIVATE ${GFXWORKER_DIR}/include)
target_include_directories(libgfxworker PUBLIC ${GFXWORKER_DIR}/include)
## target_include_directories(Mega PUBLIC ${MegaDir}/include)

target_link_libraries(libgfxworker Mega)