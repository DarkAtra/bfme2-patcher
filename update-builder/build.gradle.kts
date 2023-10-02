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
