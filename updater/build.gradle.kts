import edu.sc.seis.launch4j.tasks.DefaultLaunch4jTask
import org.gradle.jvm.tasks.Jar
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.compose)
    alias(libs.plugins.compose.compiler)
    alias(libs.plugins.launch4j)
    alias(libs.plugins.kapt)
    alias(libs.plugins.graalvm.native)
}

dependencies {
    implementation(compose.desktop.currentOs)
    implementation(libs.compose.resources)
    implementation(libs.coroutines.core)
    implementation(libs.coroutines.swing)
    implementation(libs.jackson.kotlin.module)
    implementation(libs.jackson.datatype.jsr310)
    implementation(libs.bouncycastle)
    implementation(libs.jna.platform)
    implementation(libs.mslinks)
    implementation(libs.kotlin.dll.injector)

    kapt(libs.graalvm.hint.processor)
    compileOnly(libs.graalvm.hint.annotations)

    testImplementation(kotlin("test"))
    testImplementation(libs.assertj)
    testImplementation(libs.coroutines.test)
    testImplementation(libs.wiremock)
}

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

kotlin {

    jvmToolchain {
        languageVersion = JavaLanguageVersion.of(21)
        vendor = JvmVendorSpec.ADOPTIUM
    }

    compilerOptions {
        jvmTarget = JvmTarget.JVM_17
        freeCompilerArgs.add("-Xcontext-parameters")
    }

    sourceSets {
        all {
            languageSettings.apply {
                progressiveMode = true
            }
        }
    }
}

graalvmNative {
    binaries {
        all {
            useFatJar = true
            javaLauncher = javaToolchains.launcherFor {
                languageVersion = JavaLanguageVersion.of(21)
                vendor = JvmVendorSpec.GRAAL_VM
            }
        }

        named("main") {
            imageName.set("updater")
            mainClass.set("de.darkatra.bfme2.MainKt")

            buildArgs.add("--no-fallback")
            buildArgs.add("-O3")
            buildArgs.add("-H:+AddAllCharsets")
            buildArgs.add("-Djava.awt.headless=false")
            buildArgs.add("--enable-https")

            // hide the console window, see: https://github.com/oracle/graal/issues/2256
            buildArgs.add("-H:NativeLinkerOption=/SUBSYSTEM:WINDOWS")
            buildArgs.add("-H:NativeLinkerOption=/ENTRY:mainCRTStartup")
        }
    }
}

tasks {

    withType<Test> {
        useJUnitPlatform()
    }

    withType<Jar> {
        exclude("META-INF/*.RSA", "META-INF/*.SF", "META-INF/*.DSA")

        manifest {
            attributes("Implementation-Version" to project.version)
        }
    }
}

compose.desktop {
    application {
        mainClass = "de.darkatra.bfme2.MainKt"
        nativeDistributions {
            packageName = "updater"
            packageVersion = "${project.version}"
        }
    }
}

afterEvaluate {
    tasks {
        withType<DefaultLaunch4jTask> {
            val packageUberJarForCurrentOS by getting
            dependsOn(packageUberJarForCurrentOS)
            setJarTask(packageUberJarForCurrentOS)

            outfile.value("${project.name}.exe")
            icon.value("$projectDir/icon.ico")
            manifest.value("$projectDir/launch4j.manifest")
            jreMinVersion.value("17")
        }
    }
}
