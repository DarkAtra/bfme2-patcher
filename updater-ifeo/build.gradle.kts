import edu.sc.seis.launch4j.tasks.DefaultLaunch4jTask
import org.gradle.jvm.tasks.Jar

plugins {
    kotlin("jvm")
    id("edu.sc.seis.launch4j")
}

kotlin {
    jvmToolchain(11)
}

dependencies {
    implementation("net.java.dev.jna:jna-platform:${project.extra["jna.version"]}")
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
        jreMinVersion.value("11")
    }
}
