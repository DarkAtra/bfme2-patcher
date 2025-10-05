import edu.sc.seis.launch4j.tasks.DefaultLaunch4jTask
import org.gradle.jvm.tasks.Jar

plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.launch4j)
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation(libs.jna.platform)
}

tasks {

    withType<Jar> {
        manifest {
            attributes("Implementation-Version" to project.version)
            attributes("Main-Class" to "de.darkatra.patcher.ifeo.MainKt")
        }

        from(configurations.runtimeClasspath.get().map(::zipTree))
        duplicatesStrategy = DuplicatesStrategy.EXCLUDE
    }

    withType<DefaultLaunch4jTask> {
        outfile.value("${project.name}.exe")
        manifest.value("$projectDir/launch4j.manifest")
        jreMinVersion.value("17")
    }
}
