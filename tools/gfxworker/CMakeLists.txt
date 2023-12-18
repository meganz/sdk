set(GFXWORKER_DIR ${MegaDir}/tools/gfxworker)

if (WIN32)
    # libgfxworker library and gfxworker executable
    include(${GFXWORKER_DIR}/src/CMakeLists.txt)

    # integration test
    include(${GFXWORKER_DIR}/tests/integration/CMakeLists.txt)
endif()
