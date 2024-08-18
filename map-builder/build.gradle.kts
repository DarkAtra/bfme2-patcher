import org.gradle.jvm.tasks.Jar

plugins {
    alias(libs.plugins.kotlin.jvm)
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation(libs.bfme2.modding.utils.big)
    implementation(libs.bfme2.modding.utils.map)
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
