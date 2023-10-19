// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "MEGASDK",
    platforms: [
        .iOS(.v14)
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
            exclude: ["examples",
                      "tests",
                      "doc",
                      "contrib",
                      "bindings",
                      "src/win32",
                      "src/wincurl",
                      "src/mega_utf8proc_data.c",
                      "src/thread/libuvthread.cpp",
                      "src/osx/fs.cpp"],
            cxxSettings: [
                .headerSearchPath("bindings/ios"),
                .headerSearchPath("include/mega/posix"),
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
                .linkedFramework("UIKit"),
                .linkedFramework("UniformTypeIdentifiers"),
                // Libraries
                .linkedLibrary("resolv"),
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
                .headerSearchPath("Private")
            ]
        ),
        .binaryTarget(
            name: "libcryptopp",
            url: "https://s3.g.s4.mega.io/010996547823786/ios-xcframeworks/libcryptopp_xcframework.zip",
            checksum: "250c07fe6c1f071ebf455f4e4d2b2d15a3cdbd2aebcf36e2fd9c68c0c58e08ef"
        ),
        .binaryTarget(
            name: "libmediainfo",
            url: "https://s3.g.s4.mega.io/010996547823786/ios-xcframeworks/libmediainfo_xcframework.zip",
            checksum: "24fdbe1e3df3642799b1c890d5c810c88088bbe2a1d7bd34c766587f7405b0fa"
        ),
        .binaryTarget(
            name: "libuv",
            url: "https://s3.g.s4.mega.io/010996547823786/ios-xcframeworks/libuv_xcframework.zip",
            checksum: "d8e478ccdfc454e8636a7a65add7e4d0199cecdcdbb6e46eddc6f536df1d023e"
        ),
        .binaryTarget(
            name: "libcurl",
            url: "https://s3.g.s4.mega.io/010996547823786/ios-xcframeworks/libcurl_xcframework.zip",
            checksum: "599cfcb782f3bd0839746eb56b9e45f29c0d9961f0512531744b11145773f5de"
        ),
        .binaryTarget(
            name: "libsodium",
            url: "https://s3.g.s4.mega.io/010996547823786/ios-xcframeworks/libsodium_xcframework.zip",
            checksum: "2d8d325921110ba7175573623095f56fb3ed2f8044ad6053a966b825710b853d"
        ),
        .binaryTarget(
            name: "libzen",
            url: "https://s3.g.s4.mega.io/010996547823786/ios-xcframeworks/libzen_xcframework.zip",
            checksum: "f3759e829d921875c4d9ec55354b225e8c5354488ca492d777bf10b0e9d38ebb"
        )
    ],
    cxxLanguageStandard: .cxx14
)
