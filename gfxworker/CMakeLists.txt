set(GFXWORKER_DIR "${MegaDir}/gfxworker")

if (WIN32)
   # libgfxworker library
   include(${GFXWORKER_DIR}/src/CMakeLists.txt)

   # unit test
   include(${GFXWORKER_DIR}/tests/CMakeLists.txt)

   # executable: gfxworker
   include(${GFXWORKER_DIR}/standalone/CMakeLists.txt)
endif()
