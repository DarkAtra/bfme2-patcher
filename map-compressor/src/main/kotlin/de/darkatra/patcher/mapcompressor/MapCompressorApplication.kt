package de.darkatra.patcher.mapcompressor

import de.darkatra.bfme2.toLittleEndianBytes
import java.nio.file.Files
import java.nio.file.Path
import java.util.stream.Collectors
import java.util.zip.DeflaterOutputStream
import kotlin.io.path.fileSize
import kotlin.io.path.inputStream
import kotlin.io.path.name
import kotlin.io.path.outputStream
import kotlin.io.path.pathString

const val MAP_DIR = "maps-camera-fix"

fun main() {
    MapCompressorApplication.build()
}

object MapCompressorApplication {

    fun build() {

        println("Compressing maps...")

        val inputDir = Path.of(MAP_DIR)
        println("Reading files from: ${inputDir.pathString}")

        readFilesInDirectory(inputDir)
            .filter { file -> file.name.endsWith(".map.uncompressed") }
            .forEach { file ->
                println("* Compressing map: ${inputDir.relativize(file).pathString}")
                file.inputStream().buffered().use { input ->
                    val outPath = file.parent.resolve(file.name.replace(".map.uncompressed", ".map"))
                    println("* Writing to: ${inputDir.relativize(outPath).pathString}")

                    val output = outPath.outputStream().buffered()
                    // write four character code and file size
                    output.write("ZL5\u0000".toByteArray())
                    output.write(file.fileSize().toInt().toLittleEndianBytes())
                    output.flush()

                    DeflaterOutputStream(output).use { input.transferTo(it) }
                }
            }
        println("Success!")
    }

    // max file depth is 2
    private fun readFilesInDirectory(directory: Path): Set<Path> {
        return Files.walk(directory, 2).use { stream ->
            stream
                .filter { path: Path -> Files.isRegularFile(path) }
                .collect(Collectors.toSet())
        }
    }
}
