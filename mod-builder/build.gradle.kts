import org.gradle.jvm.tasks.Jar

plugins {
    kotlin("jvm")
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation("de.darkatra.bfme2:big:${project.extra["bfme2-modding-utils.version"]}")
    implementation("net.java.dev.jna:jna-platform:${project.extra["jna.version"]}")
}

tasks {

    withType<Jar> {
        manifest {
            attributes("Implementation-Version" to project.version)
            attributes("Main-Class" to "de.darkatra.patcher.modbuilder.ModBuilderApplicationKt")
        }

        from(configurations.runtimeClasspath.get().map(::zipTree))
        duplicatesStrategy = DuplicatesStrategy.EXCLUDE
    }
}
