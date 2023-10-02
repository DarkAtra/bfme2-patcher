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
