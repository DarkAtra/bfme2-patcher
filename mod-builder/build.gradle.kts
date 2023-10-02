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
