pluginManagement {
    repositories {
        mavenCentral()
        gradlePluginPortal()
        google()
        maven("https://maven.pkg.jetbrains.space/public/p/compose/dev")
    }
}

rootProject.name = "bfme2-patcher"
include("game-patcher")
include("map-builder")
include("mod-builder")
include("update-builder")
include("updater")
