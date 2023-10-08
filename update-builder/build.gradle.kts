import org.gradle.jvm.tasks.Jar

plugins {
    kotlin("jvm")
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation("com.fasterxml.jackson.module:jackson-module-kotlin:${project.extra["jackson.version"]}")
    implementation("com.fasterxml.jackson.datatype:jackson-datatype-jsr310:${project.extra["jackson.version"]}")
    implementation("org.bouncycastle:bcprov-jdk18on:${project.extra["bouncycastle.version"]}")
    implementation("de.darkatra.bfme2:big:${project.extra["bfme2-modding-utils.version"]}")
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
