macro(load_sdklib_libraries)

    target_link_libraries(SDKlib PUBLIC ccronexpr)

    if(VCPKG_ROOT)
        find_package(cryptopp CONFIG REQUIRED)
        target_link_libraries(SDKlib PUBLIC cryptopp::cryptopp) # TODO: Private for SDK core

        find_package(unofficial-sodium REQUIRED)
        if(WIN32)
            target_link_libraries(SDKlib PUBLIC unofficial-sodium::sodium)  # TODO: Private for SDK core
        else()
            target_link_libraries(SDKlib PRIVATE unofficial-sodium::sodium unofficial-sodium::sodium_config_public)
        endif()

        find_package(unofficial-sqlite3 REQUIRED)
        target_link_libraries(SDKlib PRIVATE unofficial::sqlite3::sqlite3)

        find_package(CURL REQUIRED)
        target_link_libraries(SDKlib PRIVATE CURL::libcurl)

        if(USE_OPENSSL)
            if(USE_WEBRTC) # Defined in MEGAchat.
                # find_package(OpenSSL) works for BoringSSL but it does not load the flags from the .pc files. find_package() has
                # its own way to prepare the OpenSSL needed flags and configurations.
                # Linking gfxworker when using BoringSSL from WebRTC requires the -lX11 flag for WebRTC, despite we only need
                # the BoringSSL symbols.
                # Using pkg-config in that specific case, honors the "OpenSSL" .pc files used as a way to impersonate OpenSSL/BoringSSL
                # using the one contained in WebRTC library.
                find_package(PkgConfig REQUIRED)
                pkg_check_modules(openssl REQUIRED IMPORTED_TARGET openssl)
                target_link_libraries(SDKlib PRIVATE PkgConfig::openssl)
            else()
                find_package(OpenSSL REQUIRED)
                target_link_libraries(SDKlib PRIVATE OpenSSL::SSL OpenSSL::Crypto)
            endif()
        endif()

        if(USE_MEDIAINFO)
            # MediaInfo is not setting libzen dependency correctly. Preload it.
            find_package(ZenLib CONFIG REQUIRED)
            target_link_libraries(SDKlib PRIVATE zen)

            find_package(MediaInfoLib REQUIRED)
            target_link_libraries(SDKlib PRIVATE mediainfo)
        endif()

        if(USE_FREEIMAGE)
            find_package(freeimage REQUIRED)
            target_link_libraries(SDKlib PRIVATE freeimage::FreeImage)
        endif()

        if(USE_FFMPEG)
            find_package(FFMPEG REQUIRED)
            target_include_directories(SDKlib PRIVATE ${FFMPEG_INCLUDE_DIRS})
            target_link_directories(SDKlib PRIVATE ${FFMPEG_LIBRARY_DIRS})
            target_link_libraries(SDKlib PRIVATE ${FFMPEG_LIBRARIES})
            set(HAVE_FFMPEG 1)
        endif()

        if(USE_LIBUV)
            find_package(libuv REQUIRED)
            target_link_libraries(SDKlib PRIVATE $<IF:$<TARGET_EXISTS:uv_a>,uv_a,uv>)
            set(HAVE_LIBUV 1)
        endif()

        if(USE_PDFIUM)
            find_package(pdfium REQUIRED)
            target_link_libraries(SDKlib PRIVATE PDFIUM::pdfium)
            set(HAVE_PDFIUM 1)
        endif()

        find_package(ICU COMPONENTS uc data REQUIRED)
        target_link_libraries(SDKlib PRIVATE ICU::uc ICU::data)

        if(DEPRECATED_USE_C_ARES)
            find_package(c-ares REQUIRED)
            target_link_libraries(SDKlib PRIVATE c-ares::cares)
            set(MEGA_USE_C_ARES 1)
        endif()

        if(USE_READLINE)
            find_package(Readline-unix REQUIRED)
            target_link_libraries(SDKlib PRIVATE Readline::Readline)

            # Curses is needed by Readline
            set(CURSES_NEED_NCURSES TRUE)
            find_package(Curses REQUIRED)
            target_include_directories(SDKlib PRIVATE ${CURSES_INCLUDE_DIRS})
            target_compile_options(SDKlib PRIVATE ${CURSES_CFLAGS})
            target_link_libraries(SDKlib PRIVATE ${CURSES_LIBRARIES})
        else()
            set(NO_READLINE 1)
        endif()

    else() # No VCPKG usage. Use pkg-config

        find_package(PkgConfig REQUIRED) # For libraries loaded using pkg-config

        pkg_check_modules(cryptopp REQUIRED IMPORTED_TARGET libcrypto++)
        target_link_libraries(SDKlib PUBLIC PkgConfig::cryptopp) # TODO: Private for SDK core

        pkg_check_modules(sodium REQUIRED IMPORTED_TARGET libsodium)
        target_link_libraries(SDKlib PRIVATE PkgConfig::sodium)

        pkg_check_modules(sqlite3 REQUIRED IMPORTED_TARGET sqlite3)
        target_link_libraries(SDKlib PRIVATE PkgConfig::sqlite3)

        pkg_check_modules(curl REQUIRED IMPORTED_TARGET libcurl)
        target_link_libraries(SDKlib PRIVATE PkgConfig::curl)

        find_package(ICU COMPONENTS uc data REQUIRED)
        target_link_libraries(SDKlib PRIVATE ICU::uc ICU::data)

        if(USE_OPENSSL)
            find_package(OpenSSL REQUIRED)
            target_link_libraries(SDKlib PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        endif()

        if(USE_MEDIAINFO)
            pkg_check_modules(mediainfo REQUIRED IMPORTED_TARGET libmediainfo)
            target_link_libraries(SDKlib PRIVATE PkgConfig::mediainfo)
        endif()

        if(USE_FREEIMAGE)
            # FreeImage has no pkg-config file. Use out own FindFreeImage.cmake to find the library.
            find_package(FreeImage REQUIRED)
            target_link_libraries(SDKlib PRIVATE FreeImage::FreeImage)
        endif()

        if(USE_FFMPEG)
            pkg_check_modules(ffmpeg REQUIRED IMPORTED_TARGET libavformat libavutil libavcodec libswscale libswresample)
            target_link_libraries(SDKlib PRIVATE PkgConfig::ffmpeg)
            set(HAVE_FFMPEG 1)
        endif()

        if(USE_LIBUV)
            pkg_check_modules(uv REQUIRED IMPORTED_TARGET libuv)
            target_link_libraries(SDKlib PRIVATE PkgConfig::uv)
            set(HAVE_LIBUV 1)
        endif()

        if(USE_PDFIUM)
            pkg_check_modules(pdfium REQUIRED IMPORTED_TARGET pdfium)
            target_link_libraries(SDKlib PRIVATE PkgConfig::pdfium)
            set(HAVE_PDFIUM 1)
        endif()

        if(DEPRECATED_USE_C_ARES)
            pkg_check_modules(cares REQUIRED IMPORTED_TARGET libcares)
            target_link_libraries(SDKlib PRIVATE PkgConfig::cares)
            set(MEGA_USE_C_ARES 1)
        endif()

        if(USE_READLINE)
            pkg_check_modules(readline REQUIRED IMPORTED_TARGET readline)
            target_link_libraries(SDKlib PRIVATE PkgConfig::readline)
        else()
            set(NO_READLINE 1)
        endif()

    endif()

endmacro()
