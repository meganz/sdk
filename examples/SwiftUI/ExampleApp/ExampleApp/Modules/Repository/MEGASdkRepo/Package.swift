// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "MEGASdkRepo",
    platforms: [
        .macOS(.v10_15), .iOS(.v15)
    ],
    products: [
        .library(
            name: "MEGASdkRepo",
            targets: ["MEGASdkRepo"]
        ),
    ],
    dependencies: [
        .package(path: "../../../../../../../MEGASdk"),
        .package(path: "../../MEGADomain")
    ],
    targets: [
        .target(
            name: "MEGASdkRepo",
            dependencies: ["MEGASdk", "MEGADomain"]
        )
    ]
)
