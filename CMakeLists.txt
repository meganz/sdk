cmake_minimum_required(VERSION 2.6)
project (mega)
set (CMAKE_CXX_STANDARD 11)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug" FORCE)
  message("XYXYXYXYXYX ${CMAKE_BUILD_TYPE} mode XYXYXYXYX")
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Release" FORCE)
  message("XYXYXYXYXYX ${CMAKE_BUILD_TYPE} mode XYXYXYXYX")
else()
     set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug" FORCE)
     message("XYXYXYXYXYX ${CMAKE_BUILD_TYPE} mode XYXYXYXYX")
endif()
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(build_type "_d")
    set(build_type2 "d")
else()
    set(build_type "")
    set(build_type2 "")
endif()

#file(GLOB SOURCES ${CMAKE_SOURCE_DIR}/${PROJECT_SOURCE_DIR}/src/*.cpp )
if(NOT DEFINED THIRDPARTY)
    SET(THIRDPARTY ${PROJECT_SOURCE_DIR}/3rdparty)
endif()
set(COMMON_SRC 
${PROJECT_SOURCE_DIR}/src/attrmap.cpp  
${PROJECT_SOURCE_DIR}/src/attrmap.cpp
    ${PROJECT_SOURCE_DIR}/src/backofftimer.cpp
    ${PROJECT_SOURCE_DIR}/src/base64.cpp
    ${PROJECT_SOURCE_DIR}/src/command.cpp
    ${PROJECT_SOURCE_DIR}/src/commands.cpp
    ${PROJECT_SOURCE_DIR}/src/crypto/cryptopp.cpp
    ${PROJECT_SOURCE_DIR}/src/db.cpp
    ${PROJECT_SOURCE_DIR}/src/gfx/external.cpp
    ${PROJECT_SOURCE_DIR}/src/file.cpp
    ${PROJECT_SOURCE_DIR}/src/fileattributefetch.cpp
    ${PROJECT_SOURCE_DIR}/src/filefingerprint.cpp
    ${PROJECT_SOURCE_DIR}/src/filesystem.cpp
    ${PROJECT_SOURCE_DIR}/src/gfx/freeimage.cpp
    ${PROJECT_SOURCE_DIR}/src/mediafileattribute.cpp
    ${PROJECT_SOURCE_DIR}/src/mega_http_parser.cpp
    ${PROJECT_SOURCE_DIR}/src/mega_utf8proc.cpp
    ${PROJECT_SOURCE_DIR}/src/mega_ccronexpr.cpp
    ${PROJECT_SOURCE_DIR}/src/mega_evt_tls.cpp
    ${PROJECT_SOURCE_DIR}/src/mega_zxcvbn.cpp
    ${PROJECT_SOURCE_DIR}/src/posix/net.cpp
    ${PROJECT_SOURCE_DIR}/src/thread/win32thread.cpp
    ${PROJECT_SOURCE_DIR}/src/win32/fs.cpp
    ${PROJECT_SOURCE_DIR}/src/gfx.cpp
    ${PROJECT_SOURCE_DIR}/src/http.cpp
    ${PROJECT_SOURCE_DIR}/src/json.cpp
    ${PROJECT_SOURCE_DIR}/src/logging.cpp
    ${PROJECT_SOURCE_DIR}/src/megaapi.cpp
    ${PROJECT_SOURCE_DIR}/src/megaapi_impl.cpp
    ${PROJECT_SOURCE_DIR}/src/megaclient.cpp
    ${PROJECT_SOURCE_DIR}/src/node.cpp
    ${PROJECT_SOURCE_DIR}/src/pendingcontactrequest.cpp
    ${PROJECT_SOURCE_DIR}/src/proxy.cpp
    ${PROJECT_SOURCE_DIR}/src/pubkeyaction.cpp
    ${PROJECT_SOURCE_DIR}/src/request.cpp
    ${PROJECT_SOURCE_DIR}/src/serialize64.cpp
    ${PROJECT_SOURCE_DIR}/src/share.cpp
    ${PROJECT_SOURCE_DIR}/src/sharenodekeys.cpp
    ${PROJECT_SOURCE_DIR}/src/crypto/sodium.cpp
    ${PROJECT_SOURCE_DIR}/src/db/sqlite.cpp
    ${THIRDPARTY}/libs/sqlite3.c
    ${PROJECT_SOURCE_DIR}/src/sync.cpp
    ${PROJECT_SOURCE_DIR}/src/transfer.cpp
    ${PROJECT_SOURCE_DIR}/src/transferslot.cpp
    ${PROJECT_SOURCE_DIR}/src/treeproc.cpp
    ${PROJECT_SOURCE_DIR}/src/user.cpp
    ${PROJECT_SOURCE_DIR}/src/useralerts.cpp
    ${PROJECT_SOURCE_DIR}/src/utils.cpp
    ${PROJECT_SOURCE_DIR}/src/win32/waiter.cpp
    ${PROJECT_SOURCE_DIR}/src/waiterbase.cpp
)



include_directories(
    
${PROJECT_SOURCE_DIR}/
${PROJECT_SOURCE_DIR}/include
${PROJECT_SOURCE_DIR}/include/mega/wincurl 
${PROJECT_SOURCE_DIR}/third_party/utf8proc 
${THIRDPARTY}/third_party/ccronexpr 
${THIRDPARTY}/third_party/utf8proc
${THIRDPARTY}/include/zlib 
${THIRDPARTY}/include 
${THIRDPARTY}/include/libuv 
${THIRDPARTY}/include/mega/wincurl
${THIRDPARTY}/include/cryptopp
${THIRDPARTY}/include/cares
${THIRDPARTY}/include/libsodium
 ${PROJECT_SOURCE_DIR}/examples/win32
#[[    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/mega  
    ${PROJECT_SOURCE_DIR}/include/mega/win32 
    ${THIRDPARTY}/include/
    ${THIRDPARTY}/include/zlib
    ${THIRDPARTY}/include/cryptopp
    #${THIRDPARTY}/include/sodium/src/libsodium/include#]]

    )


if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    link_directories(${THIRDPARTY}/libs/x32d)
else()
    link_directories(${THIRDPARTY}/libs/x32)
endif()


add_library(sqlite STATIC ${THIRDPARTY}/libs/sqlite3.c)
add_library(mega STATIC  ${COMMON_SRC})
#add_executable(megacli ${PROJECT_SOURCE_DIR}/examples/megacli.cpp)
add_executable(testmega ${PROJECT_SOURCE_DIR}/examples/win32/testmega/main.cpp)

target_compile_definitions(mega PUBLIC -DWIN32_LEAN_AND_MEAN -DZLIB_WINAPI -D_WINDOWS -DUNICODE -DWIN32 -DUSE_FREEIMAGE -DUSE_SQLITE -DUSE_CRYPTOPP -DUSE_SODIUM -DUSE_CURL -DUSE_OPENSSL -DENABLE_SYNC -DSODIUM_STATIC -DPCRE_STATIC -DHAVE_LIBUV)

target_link_libraries(mega cryptopp sodium winhttp Crypt32 ws2_32 user32  zlibstatic )
#target_link_libraries(megacli mega)
target_link_libraries(testmega mega cryptopp sodium FreeImage${build_type2} winhttp ws2_32 crypt32 user32 zlibstatic shlwapi kernel32 gdi32 winspool comdlg32 advapi32 shell32 ole32 oleaut32 uuid odbc32 odbccp32 cares ssleay32 libeay32 libcurl libuv Iphlpapi Psapi Userenv )
target_compile_definitions(testmega PUBLIC -DWIN32  -D_CONSOLE  -DNDEBUG  -DWIN32_LEAN_AND_MEAN  -DZLIB_WINAPI  -DUNICODE  -DUSE_FREEIMAGE  -DUSE_SQLITE  -DUSE_CRYPTOPP  -DUSE_SODIUM  -DUSE_CURL  -DUSE_OPENSSL  -DENABLE_SYNC  -DSODIUM_STATIC  -DPCRE_STATIC  -DHAVE_LIBUV )

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    file(GLOB DLLFiles ${THIRDPARTY}/libs/x32d/*.dll)
else()
    file(GLOB DLLFiles ${THIRDPARTY}/libs/x32/*.dll)
endif()
foreach(DLLFile ${DLLFiles})
add_custom_command(TARGET testmega POST_BUILD
COMMAND ${CMAKE_COMMAND} -E
    copy_if_different  ${DLLFile} $<TARGET_FILE_DIR:testmega>
           COMMENT "copying  ${DLLFile}\n"
           )
endforeach()   