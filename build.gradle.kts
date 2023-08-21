allprojects {
    group = "de.darkatra.bfme2"
    version = "0.8.0"

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
    kotlin("multiplatform").apply(false)
    kotlin("jvm").apply(false)
    id("org.jetbrains.compose").apply(false)
    id("edu.sc.seis.launch4j").apply(false)
}
