// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "MEGADomain",
    platforms: [
        .macOS(.v10_15), .iOS(.v14)
    ],
    products: [
        .library(
            name: "MEGADomain",
            targets: ["MEGADomain"]),
    ],
    targets: [
        .target(
            name: "MEGADomain")
    ]
)
