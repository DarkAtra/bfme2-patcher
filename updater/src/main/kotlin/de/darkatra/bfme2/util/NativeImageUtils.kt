package de.darkatra.bfme2.util

import de.darkatra.bfme2.LOGGER
import io.goodforgod.graalvm.hint.annotation.JniHint
import java.awt.Insets
import java.awt.Toolkit
import java.nio.file.DirectoryNotEmptyException
import java.nio.file.Path
import java.util.logging.Level
import kotlin.io.path.absolutePathString
import kotlin.io.path.copyTo
import kotlin.io.path.exists
import kotlin.io.path.listDirectoryEntries
import kotlin.io.path.name

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

    /**
     * Ensures that `java.home` and similar system properties are set so that AWT and Skiko initialize correctly.
     */
    fun setupNativeImageEnvironmentIfNecessary() {

        val javaHome: String? = System.getProperty("java.home")
        val nativeImageCode: String? = System.getProperty("org.graalvm.nativeimage.imagecode")

        if ((nativeImageCode == "buildtime" || nativeImageCode == "runtime") && javaHome.isNullOrEmpty()) {

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
        }
    }
}
