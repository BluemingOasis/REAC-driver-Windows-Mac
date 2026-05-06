// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "ReacConfigApp",
    platforms: [.macOS(.v13)],
    products: [
        .executable(name: "ReacConfigApp", targets: ["ReacConfigApp"])
    ],
    targets: [
        .executableTarget(name: "ReacConfigApp")
    ]
)
