import org.gradle.jvm.tasks.Jar
import org.jetbrains.compose.compose

plugins {
    kotlin("multiplatform")
    id("org.jetbrains.compose")
}

val appVersion = "0.5.0"

group = "de.darkatra.bfme2"
version = appVersion

repositories {
    google()
    mavenCentral()
    maven("https://maven.pkg.jetbrains.space/public/p/compose/dev")
}

kotlin {
    jvm {
        compilations.all {
            kotlinOptions.jvmTarget = "11"
        }

        testRuns["test"].executionTask.configure {
            useJUnitPlatform()
        }
    }
    sourceSets {
        all {
            languageSettings.apply {
                optIn("kotlin.RequiresOptIn")
                progressiveMode = true
            }
        }

        val jvmMain by getting {
            dependencies {
                implementation(compose.desktop.currentOs)
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:${extra["kotlin-coroutine.version"]}")
                implementation("com.fasterxml.jackson.module:jackson-module-kotlin:${extra["jackson.version"]}")
                implementation("com.fasterxml.jackson.datatype:jackson-datatype-jsr310:${extra["jackson.version"]}")
                implementation("org.bouncycastle:bcprov-jdk18on:${extra["bouncycastle.version"]}")
                implementation("commons-io:commons-io:${extra["commons-io.version"]}")
                implementation("net.java.dev.jna:jna-platform:${extra["jna.version"]}")
                implementation("com.github.vatbub:mslinks:${extra["mslinks.version"]}")
            }
        }

        val jvmTest by getting {
            dependencies {
                implementation(kotlin("test"))
                implementation("org.assertj:assertj-core:${extra["assertj.version"]}")
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:${extra["kotlin-coroutine.version"]}")
                implementation("com.github.tomakehurst:wiremock-jre8:${extra["wiremock.version"]}")
            }
        }
    }
}

tasks {
    withType<Jar> {
        exclude("META-INF/*.RSA", "META-INF/*.SF", "META-INF/*.DSA")

        manifest {
            attributes("Implementation-Version" to appVersion)
        }
    }
}

compose.desktop {
    application {
        mainClass = "de.darkatra.bfme2.MainKt"
        nativeDistributions {
            packageName = "patcher"
            packageVersion = appVersion
        }
    }
}
