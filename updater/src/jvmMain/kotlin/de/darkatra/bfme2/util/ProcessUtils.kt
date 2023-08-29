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

    fun runElevated(executable: Path, args: Array<String> = emptyArray()): Process {
        return Runtime.getRuntime().exec(
            "cmd /c \"${executable.absolutePathString()}\" ${args.joinToString(" ")}"
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
