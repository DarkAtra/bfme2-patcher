import org.jetbrains.kotlin.gradle.tasks.KotlinCompile

plugins {
    kotlin("jvm")
}

kotlin {
    sourceSets {
        val main by getting {
            dependencies {
                implementation("com.fasterxml.jackson.module:jackson-module-kotlin:${extra["jackson.version"]}")
                implementation("com.fasterxml.jackson.datatype:jackson-datatype-jsr310:${extra["jackson.version"]}")
                implementation("org.bouncycastle:bcprov-jdk18on:${extra["bouncycastle.version"]}")
                implementation("de.darkatra.bfme2:big:${extra["bfme2-modding-utils.version"]}")
                implementation("net.java.dev.jna:jna-platform:${extra["jna.version"]}")
            }
        }
    }
}

tasks {
    withType<KotlinCompile> {
        kotlinOptions {
            jvmTarget = "11"
        }
    }
}
