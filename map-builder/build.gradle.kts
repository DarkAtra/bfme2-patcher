import org.gradle.jvm.tasks.Jar

plugins {
    kotlin("jvm")
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation("de.darkatra.bfme2:big:${project.extra["bfme2-modding-utils.version"]}")
    implementation("de.darkatra.bfme2:map:${project.extra["bfme2-modding-utils.version"]}")
}

tasks {

    withType<Jar> {
        manifest {
            attributes("Implementation-Version" to project.version)
            attributes("Main-Class" to "de.darkatra.patcher.mapbuilder.MapBuilderApplicationKt")
        }

        from(configurations.runtimeClasspath.get().map(::zipTree))
        duplicatesStrategy = DuplicatesStrategy.EXCLUDE
    }
}
