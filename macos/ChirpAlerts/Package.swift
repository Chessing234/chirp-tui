// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "ChirpAlerts",
    platforms: [.macOS(.v13)],
    products: [
        .executable(name: "ChirpAlerts", targets: ["ChirpAlerts"])
    ],
    targets: [
        .executableTarget(
            name: "ChirpAlerts",
            path: "Sources/ChirpAlerts"
        )
    ]
)
