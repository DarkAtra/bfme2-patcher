package de.darkatra.bfme2.util

import java.nio.file.Path
import kotlin.io.path.name

object ProcessUtils {

    fun runJar(jar: Path, args: Array<String> = emptyArray()): Process {
        return run(
            listOf("java", "-jar", jar.name, *args),
            jar.parent
        )
    }

    fun run(command: List<String>, workingDirectory: Path): Process {
        return ProcessBuilder(command).apply {
            directory(workingDirectory.toFile())
        }.start()
    }
}
