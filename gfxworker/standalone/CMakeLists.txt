add_executable(gfxworker ${GFXWORKER_DIR}/standalone/gfxworker_main.cpp)

target_link_libraries(gfxworker Mega libgfxworker)