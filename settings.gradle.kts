pluginManagement {
    repositories {
        mavenCentral()
        gradlePluginPortal()
        google()
        maven("https://maven.pkg.jetbrains.space/public/p/compose/dev")
    }

    plugins {
        kotlin("jvm").version(extra["kotlin.version"] as String)
        id("org.jetbrains.compose").version(extra["compose.version"] as String)
        id("edu.sc.seis.launch4j").version(extra["launch4j.version"] as String)
    }
}

rootProject.name = "bfme2-patcher"
include("game-patcher")
include("map-builder")
include("mod-builder")
include("update-builder")
include("updater")
include("updater-ifeo")
