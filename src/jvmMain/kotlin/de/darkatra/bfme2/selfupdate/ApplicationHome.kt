package de.darkatra.bfme2.selfupdate

import java.io.File
import java.net.JarURLConnection
import java.net.URL
import java.nio.file.Path
import java.util.jar.JarFile

class ApplicationHome {

    val source: Path?

    init {
        source = when (isRunningAsJar()) {
            true -> findSource(ApplicationHome::class.java)
            else -> null
        }
    }

    fun isRunningAsJar(): Boolean {
        return javaClass.getResource(javaClass.name + ".class")?.protocol == "jar"
    }

    private fun findSource(sourceClass: Class<*>): Path? {
        try {
            val domain = sourceClass.protectionDomain
            val codeSource = domain?.codeSource
            val location = codeSource?.location
            val source = location?.let { findSource(it) }
            if (source != null && source.exists()) {
                return source.absoluteFile.toPath()
            }
        } catch (ex: Exception) {
            // ignore
        }
        return null
    }

    private fun findSource(location: URL): File {
        return when (val connection = location.openConnection()) {
            is JarURLConnection -> {
                getRootJarFile(connection.jarFile)
            }

            else -> {
                File(location.toURI())
            }
        }
    }

    private fun getRootJarFile(jarFile: JarFile): File {
        var name = jarFile.name
        val separator = name.indexOf("!/")
        if (separator > 0) {
            name = name.substring(0, separator)
        }
        return File(name)
    }
}
