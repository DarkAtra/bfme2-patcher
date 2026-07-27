pluginManagement {
    repositories {
        mavenCentral()
        gradlePluginPortal()
        maven("https://maven.pkg.jetbrains.space/public/p/compose/dev")
        google()
    }
}

rootProject.name = "bfme2-patcher"
include("asset-builder")
include("game-patcher")
include("map-builder")
include("mod-builder")
include("update-builder")
include("updater")
