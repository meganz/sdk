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
                      "src/thread/libuvthread.cpp"],
            cxxSettings: [
                .headerSearchPath("bindings/ios"),
                .headerSearchPath("include/mega/posix"),
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
                .define("ENABLE_CHAT"),
                .define("HAVE_LIBUV"),
                .headerSearchPath("Private")
            ]
        ),
        .binaryTarget(
            name: "libcryptopp",
            url: "https://s3.g.s4.mega.io/010996547823786/xcframeworks-with-catalyst-support/libcryptopp.xcframework.zip",
            checksum: "c4e3d72b36038ad7d05853aaa4d7c811638bc53a4488fa4fed5e788b8b7c0fc0"
        ),
        .binaryTarget(
            name: "libcurl",
            url: "https://s3.g.s4.mega.io/010996547823786/xcframeworks-with-catalyst-support/libcurl.xcframework.zip",
            checksum: "1c07647a29ab012f3aa596295ebf749044b9efe6402c891cf78332b4692ff00f"
        ),
        .binaryTarget(
            name: "libsodium",
            url: "https://s3.g.s4.mega.io/010996547823786/xcframeworks-with-catalyst-support/libsodium.xcframework.zip",
            checksum: "622568a5aca7b34b145a11b3dc0d8e48eb540c88fee159d48120fb032f83ecf3"
        ),
        .binaryTarget(
            name: "libuv",
            url: "https://s3.g.s4.mega.io/010996547823786/xcframeworks-with-catalyst-support/libuv.xcframework.zip",
            checksum: "d9afca567e9c99e9b64be05e5f7f28f764995ddb2459b83eb4c4897eb46107f3"
        ),
        .binaryTarget(
            name: "libmediainfo",
            url: "https://s3.g.s4.mega.io/010996547823786/xcframeworks-with-catalyst-support/libmediainfo.xcframework.zip",
            checksum: "1d77ae0c91988e3ef0ba6dde223b51e92f571232ab3de38549410d633eab16ee"
        ),
        .binaryTarget(
            name: "libzen",
            url: "https://s3.g.s4.mega.io/010996547823786/xcframeworks-with-catalyst-support/libzen.xcframework.zip",
            checksum: "f70d339890e00ea5e1e49e5e749652528fc3442621a657c19cecdff81c05f7be"
        )
    ],
    cxxLanguageStandard: .cxx14
)
