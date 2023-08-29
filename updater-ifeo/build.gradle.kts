import edu.sc.seis.launch4j.tasks.DefaultLaunch4jTask
import org.gradle.jvm.tasks.Jar
import org.jetbrains.kotlin.gradle.tasks.KotlinCompile

plugins {
    kotlin("jvm")
    id("edu.sc.seis.launch4j")
}

kotlin {
    sourceSets {
        val main by getting {
            dependencies {
                implementation("net.java.dev.jna:jna-platform:${extra["jna.version"]}")
            }
        }
    }
}

tasks {
    withType<JavaCompile> {
        sourceCompatibility = "11"
        targetCompatibility = "11"
    }

    withType<Jar> {
        manifest {
            attributes("Implementation-Version" to project.version)
            attributes("Main-Class" to "de.darkatra.patcher.ifeo.MainKt")
        }

        from(configurations.runtimeClasspath.get().map(::zipTree))
        duplicatesStrategy = DuplicatesStrategy.EXCLUDE
    }

    withType<KotlinCompile> {
        kotlinOptions {
            jvmTarget = "11"
        }
    }

    withType<DefaultLaunch4jTask> {
        outfile.value("${project.name}.exe")
        manifest.value("$projectDir/launch4j.manifest")
    }
}
