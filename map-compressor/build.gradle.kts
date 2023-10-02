plugins {
    kotlin("jvm")
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    implementation("de.darkatra.bfme2:core:${project.extra["bfme2-modding-utils.version"]}")
}
