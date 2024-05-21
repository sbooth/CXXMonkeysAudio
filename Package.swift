// swift-tools-version: 5.6
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
	name: "CXXMonkeysAudio",
	products: [
		// Products define the executables and libraries a package produces, making them visible to other packages.
		.library(
			name: "MAC",
			targets: [
				"MAC",
			]),
	],
	targets: [
		// Targets are the basic building blocks of a package, defining a module or a test suite.
		// Targets can depend on other targets in this package and products from dependencies.
		.target(
			name: "MAC",
			cSettings: [
				.define("PLATFORM_APPLE"),
				.headerSearchPath("include/MAC"),
				.headerSearchPath("MACLib"),
				.headerSearchPath("Shared"),
			]),
		.testTarget(
			name: "CXXMonkeysAudioTests",
			dependencies: [
				"MAC",
			]),
	]
)
