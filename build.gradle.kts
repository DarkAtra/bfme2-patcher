allprojects {
    group = "de.darkatra.bfme2"
    version = "0.17.0"

    repositories {
        mavenCentral()
        maven("https://maven.pkg.jetbrains.space/public/p/compose/dev")
        maven("https://maven.pkg.github.com/DarkAtra/*") {
            credentials {
                username = "${extra["github.username"]}"
                password = "${extra["github.password"]}"
            }
        }
        google()
        mavenLocal()
    }
}

plugins {
    alias(libs.plugins.kotlin.jvm) apply false
    alias(libs.plugins.compose) apply false
    alias(libs.plugins.compose.compiler) apply false
    alias(libs.plugins.launch4j) apply false
}
