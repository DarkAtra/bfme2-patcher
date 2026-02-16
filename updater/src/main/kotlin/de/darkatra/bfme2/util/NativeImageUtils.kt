package de.darkatra.bfme2.util

import de.darkatra.bfme2.LOGGER
import io.goodforgod.graalvm.hint.annotation.JniHint
import io.goodforgod.graalvm.hint.annotation.ResourceHint
import java.awt.Insets
import java.awt.Toolkit
import java.io.File
import java.nio.file.DirectoryNotEmptyException
import java.nio.file.Files
import java.nio.file.Path
import java.util.logging.Level
import kotlin.io.path.ExperimentalPathApi
import kotlin.io.path.absolutePathString
import kotlin.io.path.copyTo
import kotlin.io.path.createTempDirectory
import kotlin.io.path.deleteRecursively
import kotlin.io.path.exists
import kotlin.io.path.listDirectoryEntries
import kotlin.io.path.name

private const val FONT_CONFIG_FILE_NAME: String = "empty.fontconfig.properties.src"
private const val FONTCONFIG_PROPERTY_NAME: String = "sun.awt.fontconfig"

@ResourceHint(
    include = [
        "\\\\QcomposeResources/de.darkatra.bfme2.updater.generated.resources/drawable/check.svg\\\\E",
        "\\\\QcomposeResources/de.darkatra.bfme2.updater.generated.resources/drawable/icon.png\\\\E",
        "\\\\QcomposeResources/de.darkatra.bfme2.updater.generated.resources/drawable/splash\\\\E[0-9]+_[0-9]+x[0-9]+.(jpg|png)",
    ]
)
@JniHint(
    types = [
        Toolkit::class,
        Insets::class,
    ],
    typeNames = [
        "sun.awt.windows.WToolkit",
        "sun.java2d.SurfaceData",
        "sun.java2d.InvalidPipeException",
    ]
)
object NativeImageUtils {

    fun isInNativeImage(): Boolean {
        val nativeImageCode: String? = System.getProperty("org.graalvm.nativeimage.imagecode")
        return nativeImageCode == "buildtime" || nativeImageCode == "runtime"
    }

    /**
     * Ensures that `java.home` and similar system properties are set so that AWT and Skiko initialize correctly.
     */
    @OptIn(ExperimentalPathApi::class)
    fun setupNativeImageEnvironmentIfNecessary() {

        val javaHome: String? = System.getProperty("java.home")

        if (isInNativeImage() && javaHome.isNullOrEmpty()) {

            val currentDir = Path.of(System.getProperty("user.dir"))
            val binDir = currentDir.resolve("bin")

            System.setProperty("java.home", currentDir.absolutePathString())
            System.setProperty("java.library.path", binDir.absolutePathString())
            System.setProperty("sun.boot.library.path", binDir.absolutePathString())

            LOGGER.info(
                """------------------------------
                |Native image detected. Paths:
                |  java.home: ${currentDir.absolutePathString()}
                |  java.library.path: ${binDir.absolutePathString()}
                |  sun.boot.library.path: ${binDir.absolutePathString()}
                |------------------------------
                """.trimMargin()
            )

            if (binDir.exists()) {
                binDir.deleteRecursively()
            }

            if (!binDir.exists() && !binDir.toFile().mkdirs()) {
                LOGGER.severe("Failed to create required bin directory '${binDir.absolutePathString()}'.")
            }

            val dllFilesToCopy = currentDir.listDirectoryEntries("*.dll")
            dllFilesToCopy.forEach { dllFile ->
                val targetDllFile = binDir.resolve(dllFile.name)
                try {
                    dllFile.copyTo(targetDllFile, overwrite = true)
                } catch (e: DirectoryNotEmptyException) {
                    LOGGER.log(Level.SEVERE, "Could not copy '${dllFile.absolutePathString()}' to '${targetDllFile.absolutePathString()}'", e)
                }
            }

            createCustomFontConfigIfNecessary()
        }
    }

    private fun createCustomFontConfigIfNecessary() {

        val jdkFontConfigPath: String? = System.getProperty(FONTCONFIG_PROPERTY_NAME)
        if (!jdkFontConfigPath.isNullOrBlank() && File(jdkFontConfigPath.trim()).exists()) {
            LOGGER.fine("JDK fontconfig is present. Will not apply workaround.")
            return
        }

        val emptyConfigPath: Path = createTempDirectory().resolve(FONT_CONFIG_FILE_NAME)
        if (emptyConfigPath.exists()) {
            LOGGER.fine("Custom fontconfig already exists.")
            return
        }

        LOGGER.fine("Writing custom fontconfig to '${emptyConfigPath.absolutePathString()}'...")

        System.setProperty(FONTCONFIG_PROPERTY_NAME, emptyConfigPath.absolutePathString())
        Files.writeString(
            emptyConfigPath,
            """
                version=1
                sequence.allfonts=

                """.trimIndent()
        )

        LOGGER.info("Successfully written custom fontconfig to '${emptyConfigPath.absolutePathString()}'.")
    }
}
