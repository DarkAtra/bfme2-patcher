import org.jetbrains.kotlin.gradle.tasks.KotlinCompile

plugins {
    kotlin("jvm")
}

kotlin {
    sourceSets {
        val main by getting {
            dependencies {
                implementation("de.darkatra.bfme2:big:${extra["bfme2-modding-utils.version"]}")
                implementation("net.java.dev.jna:jna-platform:${extra["jna.version"]}")
            }
        }
    }
}

tasks {
    withType<KotlinCompile> {
        kotlinOptions {
            jvmTarget = "17"
        }
    }
}
