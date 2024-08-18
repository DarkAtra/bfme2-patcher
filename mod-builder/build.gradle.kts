import org.gradle.jvm.tasks.Jar

plugins {
    alias(libs.plugins.kotlin.jvm)
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation(libs.bfme2.modding.utils.big)
    implementation(libs.jna.platform)
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
