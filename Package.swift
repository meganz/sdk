// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "MEGASDK",
    platforms: [
        .iOS(.v15),
        .macOS(.v10_15)
    ],
    products: [
        .library(
            name: "MEGASdkCpp",
            targets: ["MEGASdkCpp"]),
        .library(
            name: "MEGASdk",
            targets: ["MEGASdk"])
    ],
    dependencies: [
    ],
    targets: [
        .target(
            name: "MEGASdkCpp",
            dependencies: ["libcryptopp",
                           "libmediainfo",
                           "libuv",
                           "libcurl",
                           "libsodium",
                           "libzen"],
            path: "./",
            exclude: [
                "bindings",
                "cmake",
                "contrib",
                "examples",
                "src/android",
                "src/common/client_adapter_with_sync.cpp",
                "src/fuse/common/database_builder.cpp",
                "src/fuse/common/directory_inode.cpp",
                "src/fuse/common/file_cache.cpp",
                "src/fuse/common/file_extension_db.cpp",
                "src/fuse/common/file_info.cpp",
                "src/fuse/common/file_inode.cpp",
                "src/fuse/common/file_io_context.cpp",
                "src/fuse/common/inode.cpp",
                "src/fuse/common/inode_cache.cpp",
                "src/fuse/common/inode_db.cpp",
                "src/fuse/common/mega",
                "src/fuse/common/mount.cpp",
                "src/fuse/common/mount_db.cpp",
                "src/fuse/common/testing",
                "src/fuse/supported",
                "src/mega_utf8proc_data.c",
                "src/win32",
                "tests",
                "tools"
            ],
            cxxSettings: [
                .headerSearchPath("bindings/ios"),
                .headerSearchPath("include/mega/posix"),
                .headerSearchPath("src/fuse/unsupported"),
                .headerSearchPath("third_party/ccronexpr"),
                .define("ENABLE_CHAT"),
                .define("HAVE_LIBUV"),
                .define("NDEBUG", .when(configuration: .release))
            ],
            linkerSettings: [
                // Frameworks
                .linkedFramework("QuickLookThumbnailing"),
                .linkedFramework("CoreFoundation"),
                .linkedFramework("AVFoundation"),
                .linkedFramework("CoreImage"),
                .linkedFramework("CoreGraphics"),
                .linkedFramework("Foundation"),
                .linkedFramework("ImageIO"),
                .linkedFramework("Security"),
                .linkedFramework("UIKit", .when(platforms: [.iOS, .macCatalyst])),
                .linkedFramework("UniformTypeIdentifiers"),
                .linkedFramework("SystemConfiguration", .when(platforms: [.macOS])),
                // Libraries
                .linkedLibrary("z"),
                .linkedLibrary("sqlite3"),
                .linkedLibrary("icucore")
            ]
        ),
        .target(
            name: "MEGASdk",
            dependencies: ["MEGASdkCpp"],
            path: "bindings/ios",
            cxxSettings: [
                .headerSearchPath("../../include"),
                .define("ENABLE_CHAT"),
                .define("HAVE_LIBUV"),
                .headerSearchPath("Private")
            ]
        ),
        .binaryTarget(
            name: "libcryptopp",
            url: "https://s3.g.s4.mega.io/dmlaaezwz52y37atz56mfvmrvltfagrltbgpr/xcframeworks-macos-support/libcryptopp.xcframework.zip",
            checksum: "f7483596a4a682fbdf38a2a0c919c6407bdbd8c4f3cef1877c105820ae9f9896"
        ),
        .binaryTarget(
            name: "libcurl",
            url: "https://s3.g.s4.mega.io/dmlaaezwz52y37atz56mfvmrvltfagrltbgpr/xcframeworks-macos-support/libcurl.xcframework.zip",
            checksum: "ab3c685d9c20bf22a8f63105bbe3410bf06edf10d3f164a59a81c5bb0a0e4dd3"
        ),
        .binaryTarget(
            name: "libsodium",
            url: "https://s3.g.s4.mega.io/dmlaaezwz52y37atz56mfvmrvltfagrltbgpr/xcframeworks-macos-support/libsodium.xcframework.zip",
            checksum: "edf385ce2b693f864a5879559c9e61c84d4209e62e3e6e37bcd01cd23c0c311c"
        ),
        .binaryTarget(
            name: "libuv",
            url: "https://s3.g.s4.mega.io/dmlaaezwz52y37atz56mfvmrvltfagrltbgpr/xcframeworks-macos-support/libuv.xcframework.zip",
            checksum: "97e387c71773766d0673634a3688550226b0ca5ea0ce0fb0c4a66a7e99ddb6a7"
        ),
        .binaryTarget(
            name: "libmediainfo",
            url: "https://s3.g.s4.mega.io/dmlaaezwz52y37atz56mfvmrvltfagrltbgpr/xcframeworks-macos-support/libmediainfo.xcframework.zip",
            checksum: "d6fa1c5feb6282057a9b4313e77dec9a4d40d5b4a49c62a6e209fb46951a351c"
        ),
        .binaryTarget(
            name: "libzen",
            url: "https://s3.g.s4.mega.io/dmlaaezwz52y37atz56mfvmrvltfagrltbgpr/xcframeworks-macos-support/libzen.xcframework.zip",
            checksum: "520bd9579d6174c7e4b2eb989b48429961e6bb10e057119db17f8967dfe9b5a2"
        )
    ],
    cxxLanguageStandard: .cxx17
)
