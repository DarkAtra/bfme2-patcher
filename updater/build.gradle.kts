import edu.sc.seis.launch4j.tasks.DefaultLaunch4jTask
import org.gradle.jvm.tasks.Jar

plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.compose)
    alias(libs.plugins.compose.compiler)
    alias(libs.plugins.launch4j)
}

dependencies {
    implementation(compose.desktop.currentOs)
    implementation(compose.components.resources)
    implementation(libs.coroutines.core)
    implementation(libs.coroutines.swing)
    implementation(libs.jackson.kotlin.module)
    implementation(libs.jackson.datatype.jsr310)
    implementation(libs.bouncycastle)
    implementation(libs.commons.io)
    implementation(libs.jna.platform)
    implementation(libs.mslinks)
    implementation(libs.kotlin.dll.injector)

    testImplementation(kotlin("test"))
    testImplementation(libs.assertj)
    testImplementation(libs.coroutines.test)
    testImplementation(libs.wiremock)
}

kotlin {

    jvmToolchain(17)

    sourceSets {
        all {
            languageSettings.apply {
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

compose.resources {
    customDirectory(
        sourceSetName = "main",
        directoryProvider = provider {
            layout.projectDirectory.dir("src/main/resources")
        }
    )
}

afterEvaluate {
    tasks {
        withType<DefaultLaunch4jTask> {
            setJarTask(project.tasks.findByName("packageUberJarForCurrentOS"))

            outfile.value("${project.name}.exe")
            icon.value("$projectDir/icon.ico")
            manifest.value("$projectDir/launch4j.manifest")
            jreMinVersion.value("17")
        }
    }
}
