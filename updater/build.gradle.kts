import edu.sc.seis.launch4j.tasks.DefaultLaunch4jTask
import org.gradle.jvm.tasks.Jar

plugins {
    kotlin("jvm")
    id("org.jetbrains.compose")
    id("edu.sc.seis.launch4j")
}

dependencies {
    implementation(compose.desktop.currentOs)
    implementation("com.arkivanov.decompose:decompose:${project.extra["decompose.version"]}")
    implementation("com.arkivanov.decompose:extensions-compose-jetbrains:${project.extra["decompose.version"]}")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:${project.extra["kotlin-coroutine.version"]}")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-swing:${project.extra["kotlin-coroutine.version"]}")
    implementation("com.fasterxml.jackson.module:jackson-module-kotlin:${project.extra["jackson.version"]}")
    implementation("com.fasterxml.jackson.datatype:jackson-datatype-jsr310:${project.extra["jackson.version"]}")
    implementation("org.bouncycastle:bcprov-jdk18on:${project.extra["bouncycastle.version"]}")
    implementation("commons-io:commons-io:${project.extra["commons-io.version"]}")
    implementation("net.java.dev.jna:jna-platform:${project.extra["jna.version"]}")
    implementation("com.github.vatbub:mslinks:${project.extra["mslinks.version"]}")

    testImplementation(kotlin("test"))
    testImplementation("org.assertj:assertj-core:${project.extra["assertj.version"]}")
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:${project.extra["kotlin-coroutine.version"]}")
    testImplementation("com.github.tomakehurst:wiremock-jre8:${project.extra["wiremock.version"]}")
}

kotlin {

    jvmToolchain(11)

    sourceSets {
        all {
            languageSettings.apply {
                optIn("kotlin.RequiresOptIn")
                progressiveMode = true
            }
        }
    }
}

tasks {

    withType<Test> {
        useJUnitPlatform()
    }

    withType<Jar> {
        exclude("META-INF/*.RSA", "META-INF/*.SF", "META-INF/*.DSA")

        manifest {
            attributes("Implementation-Version" to project.version)
        }
    }
}

compose.desktop {
    application {
        mainClass = "de.darkatra.bfme2.MainKt"
        nativeDistributions {
            packageName = "updater"
            packageVersion = "${project.version}"
        }
    }
}

afterEvaluate {
    tasks {
        withType<DefaultLaunch4jTask> {
            setJarTask(project.tasks.findByName("packageUberJarForCurrentOS"))

            outfile.value("${project.name}.exe")
            icon.value("$projectDir/icon.ico")
            manifest.value("$projectDir/launch4j.manifest")
            jreMinVersion.value("11")
        }
    }
}
