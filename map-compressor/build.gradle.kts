import org.jetbrains.kotlin.gradle.tasks.KotlinCompile

plugins {
    kotlin("jvm")
}

kotlin {
    sourceSets {
        val main by getting {
            dependencies {
                implementation("de.darkatra.bfme2:core:${extra["bfme2-modding-utils.version"]}")
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
