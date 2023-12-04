set(GFXWORKER_DIR "${MegaDir}/gfxworker")

if (WIN32)
    # libgfxworker library and gfxworker executable
    include(${GFXWORKER_DIR}/src/CMakeLists.txt)

    # unit test
    include(${GFXWORKER_DIR}/tests/CMakeLists.txt)
endif()
