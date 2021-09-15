#pragma once

#if (__cplusplus >= 201703L) && (!defined(__GNUC__) || (__GNUC__ * 100 + __GNUC_MINOR__) >= 800)
    #include <filesystem>
    namespace fs = std::filesystem;
    #define USE_FILESYSTEM
#elif (__cplusplus >= 201700L)
    #include <experimental/filesystem>
    namespace fs = std::experimental::filesystem;
    #define USE_FILESYSTEM
#elif (__clang_major__ >= 11)
    #include <filesystem>
    namespace fs = std::__fs::filesystem;
    #define USE_FILESYSTEM
#elif !defined(__MINGW32__) && !defined(__ANDROID__) && (!defined(__GNUC__) || (__GNUC__*100+__GNUC_MINOR__) >= 503)
    #define USE_FILESYSTEM
    #ifdef WIN32
        #include <filesystem>
        namespace fs = std::experimental::filesystem;
    #else
        #include <experimental/filesystem>
        namespace fs = std::experimental::filesystem;
    #endif
#endif

