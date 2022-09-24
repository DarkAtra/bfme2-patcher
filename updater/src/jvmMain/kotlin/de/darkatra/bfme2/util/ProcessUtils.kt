package de.darkatra.bfme2.util

import java.nio.file.Path
import kotlin.io.path.absolutePathString
import kotlin.io.path.name

object ProcessUtils {

    fun run(executable: Path, args: Array<String> = emptyArray()): Process {
        return run(
            listOf(executable.absolutePathString(), *args),
            executable.parent
        )
    }

    fun runJar(jar: Path, args: Array<String> = emptyArray()): Process {
        return run(
            listOf("java", "-jar", jar.name, *args),
            jar.parent
        )
    }

    private fun run(command: List<String>, workingDirectory: Path): Process {
        return ProcessBuilder(command).apply {
            directory(workingDirectory.toFile())
        }.start()
    }
}
