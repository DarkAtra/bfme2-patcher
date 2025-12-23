package de.darkatra.patcher.modbuilder

import de.darkatra.bfme2.big.BigArchive
import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.absolutePathString
import kotlin.io.path.listDirectoryEntries
import kotlin.io.path.name
import kotlin.io.path.outputStream
import kotlin.io.path.pathString

private const val INPUT_FOLDER = "original-big"
private const val OUTPUT_FOLDER = "extracted"

fun main() {
    ModBaselineExtractorApplication.extract()
}

object ModBaselineExtractorApplication {

    fun extract() {

        println("Extracting big files...")

        val inputDir = Path.of(INPUT_FOLDER)
        val outputDir = Path.of(OUTPUT_FOLDER)

        println("Reading files from: ${inputDir.absolutePathString()}")
        println("Extracting files to: ${outputDir.absolutePathString()}")

        inputDir.listDirectoryEntries()
            .filter { path: Path -> Files.isRegularFile(path) }
            .sortedByDescending { it.name.lowercase() }
            .filter { file -> file.pathString.endsWith(".big") }
            .forEach { file ->

                println("* Processing archive: ${file.pathString}")
                val bigArchive = BigArchive.from(file)

                bigArchive.entries.forEach { entry ->

                    val archivePath = Path.of(entry.name)
                    val diskPath = outputDir.resolve(archivePath)
                    println("** Extracting archive file '${archivePath.pathString}' to '${diskPath.pathString}'")

                    diskPath.toFile().parentFile.mkdirs()

                    diskPath.outputStream().buffered().use { out ->
                        entry.inputStream().use { `in` ->
                            `in`.transferTo(out)
                        }
                    }
                }
            }
    }
}
