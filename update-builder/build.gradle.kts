import org.gradle.jvm.tasks.Jar

plugins {
    alias(libs.plugins.kotlin.jvm)
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation(libs.bfme2.modding.utils.big)
    implementation(libs.jackson.kotlin.module)
    implementation(libs.jackson.datatype.jsr310)
    implementation(libs.bouncycastle)
}

tasks {

    withType<Jar> {
        exclude("META-INF/*.RSA", "META-INF/*.SF", "META-INF/*.DSA")

        manifest {
            attributes("Implementation-Version" to project.version)
            attributes("Main-Class" to "de.darkatra.patcher.updatebuilder.UpdateBuilderNoUIKt")
        }

        from(configurations.runtimeClasspath.get().map(::zipTree))
        duplicatesStrategy = DuplicatesStrategy.EXCLUDE
    }
}
