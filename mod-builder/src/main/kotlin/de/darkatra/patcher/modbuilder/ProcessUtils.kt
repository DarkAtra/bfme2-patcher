package de.darkatra.patcher.modbuilder

import java.nio.file.Path
import kotlin.io.path.absolutePathString

object ProcessUtils {

    fun runBatchFile(batchFile: Path, workingDirectory: Path, debug: Boolean): Process {
        return ProcessBuilder(listOf("cmd.exe", "/C", batchFile.absolutePathString())).apply {
            directory(workingDirectory.toFile())
            val redirectMode = when {
                debug -> ProcessBuilder.Redirect.INHERIT
                else -> ProcessBuilder.Redirect.DISCARD
            }
            redirectOutput(redirectMode)
            redirectError(redirectMode)
        }.start()
    }
}
