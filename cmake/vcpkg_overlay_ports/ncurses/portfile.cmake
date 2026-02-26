vcpkg_download_distfile(
    ARCHIVE_PATH
    URLS
        "https://invisible-mirror.net/archives/ncurses/ncurses-${VERSION}.tar.gz"
        "ftp://ftp.invisible-island.net/ncurses/ncurses-${VERSION}.tar.gz"
        "https://ftp.gnu.org/gnu/ncurses/ncurses-${VERSION}.tar.gz"
    FILENAME "ncurses-${VERSION}.tgz"
    SHA512 fc5a13409d2a530a1325776dcce3a99127ddc2c03999cfeb0065d0eee2d68456274fb1c7b3cc99c1937bc657d0e7fca97016e147f93c7821b5a4a6837db821e8
)

vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${ARCHIVE_PATH}"
)

vcpkg_list(SET OPTIONS)

if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    list(APPEND OPTIONS
        --with-cxx-shared
        --with-shared    # "lib model"
        --without-normal # "lib model"
    )
endif()

if(NOT "wide-char" IN_LIST FEATURES)
    list(APPEND OPTIONS
        --disable-widec # Enabled by default starting at 6.5. Disabled by default here for compatibility.
    )
endif()

if(NOT VCPKG_TARGET_IS_MINGW)
    list(APPEND OPTIONS
        --enable-mixed-case
    )
endif()

if(VCPKG_TARGET_IS_MINGW)
    list(APPEND OPTIONS
        --disable-home-terminfo
        --enable-term-driver
        --disable-termcap
    )
endif()

if(VCPKG_TARGET_IS_LINUX)
    list(APPEND OPTIONS "CFLAGS=-std=gnu17")
endif()

vcpkg_configure_make(
    SOURCE_PATH "${SOURCE_PATH}"
    DETERMINE_BUILD_TRIPLET
    NO_ADDITIONAL_PATHS
    OPTIONS
        ${OPTIONS}
        --disable-db-install
        --enable-pc-files
        --without-ada
        --without-debug # "lib model"
        --without-manpages
        --without-progs
        --without-tack
        --without-tests
        --with-pkg-config-libdir=libdir
)
vcpkg_install_make()

vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/bin")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
