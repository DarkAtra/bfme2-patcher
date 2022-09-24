package de.darkatra.patcher.modbuilder

import de.darkatra.bfme2.big.BigArchive
import de.darkatra.bfme2.big.BigArchiveVersion
import java.nio.file.Files
import java.nio.file.Path
import java.util.stream.Collectors
import kotlin.io.path.copyTo
import kotlin.io.path.pathString

const val OUTPUT_FILE_NAME = "update-builder/rotwk/!mod.big"
const val OUTPUT_FILE_LANG_EN_NAME = "update-builder/rotwk/lang/englishstringsmod.big"
const val OUTPUT_FILE_LANG_DE_NAME = "update-builder/rotwk/lang/germanstringsmod.big"
const val MOD_DIR = "bfme2-ep1-mod"
const val ENGLISH_TRANSLATION_FILE = "data/english-lotr.str"
const val GERMAN_TRANSLATION_FILE = "data/german-lotr.str"
const val LOTR_STR_NAME = "data/lotr.str"
const val ASSET_FILE_NAME = "update-builder/bfme2/asset.dat"

fun main() {
    ModBuilderApplication.build()
}

object ModBuilderApplication {

    private val bfmeLocationService = BfmeLocationService
    private val excludedDirs = listOf(".git", ".gitignore", ".idea", ENGLISH_TRANSLATION_FILE, GERMAN_TRANSLATION_FILE)

    fun build() {

        println("Building mod archives...")

        val inputDir = Path.of(MOD_DIR)
        println("Reading files from: ${inputDir.pathString}")

        val outFile = Path.of(OUTPUT_FILE_NAME)
        outFile.toFile().also {
            it.mkdirs()
            it.delete()
        }

        println("* Main archive: ${outFile.pathString}")
        val bigArchive = BigArchive(BigArchiveVersion.BIG_F, outFile)
        readFilesInDirectory(inputDir)
            .filter { file -> excludedDirs.none { prefix -> file.startsWith("$MOD_DIR/$prefix") } }
            .forEach { file ->
                println("** Adding file to archive: ${inputDir.relativize(file).pathString}")
                bigArchive.createEntry(inputDir.relativize(file).toString()).outputStream().use {
                    it.write(Files.readAllBytes(file))
                }
            }
        bigArchive.writeToDisk()

        println("** Completed Main archive!")

        buildTranslation(inputDir, ENGLISH_TRANSLATION_FILE, OUTPUT_FILE_LANG_EN_NAME)
        buildTranslation(inputDir, GERMAN_TRANSLATION_FILE, OUTPUT_FILE_LANG_DE_NAME)

        println("Success! Output: ${outFile.pathString}")

        println("Installing mod...")
        val bfme2HomeDir = bfmeLocationService.findBfME2HomeDirectory().orElseThrow()
        val rotwkHomeDir = bfmeLocationService.findBfME2RotWKHomeDirectory().orElseThrow()

        // copy asset.dat
        val assertFinalLocation = bfme2HomeDir.resolve("asset.dat")
        Path.of(ASSET_FILE_NAME).copyTo(assertFinalLocation, true)
        println("* Moved asset.dat to ${assertFinalLocation.pathString}")

        // copy mod.big
        val modFinalLocation = rotwkHomeDir.resolve("!mod.big")
        Path.of(OUTPUT_FILE_NAME).copyTo(modFinalLocation, true)
        println("* Moved asset.dat to ${modFinalLocation.pathString}")

        // copy englishstringsmod.big
        val enStringsLocation = rotwkHomeDir.resolve("lang/englishstringsmod.big")
        Path.of(OUTPUT_FILE_LANG_EN_NAME).copyTo(enStringsLocation, true)
        println("* Moved englishstringsmod.big to ${enStringsLocation.pathString}")

        // copy germanstringsmod.big
        val deStringsLocation = rotwkHomeDir.resolve("lang/germanstringsmod.big")
        Path.of(OUTPUT_FILE_LANG_DE_NAME).copyTo(deStringsLocation, true)
        println("* Moved germanstringsmod.big to ${deStringsLocation.pathString}")

        println("Installed mod.")
    }

    private fun buildTranslation(inputDir: Path, translationFile: String, outputFile: String) {

        val resolvedTranslationFile = inputDir.resolve(translationFile)
        println("* Translation archive: ${resolvedTranslationFile.pathString}")
        val outFile = Path.of(outputFile)
        outFile.toFile().also {
            it.mkdirs()
            it.delete()
        }

        BigArchive(BigArchiveVersion.BIG_F, outFile).also { bigArchive ->
            println("** Adding file to archive: ${resolvedTranslationFile.pathString}")
            bigArchive.createEntry(LOTR_STR_NAME).outputStream().use {
                it.write(Files.readAllBytes(resolvedTranslationFile))
            }
        }.also {
            it.writeToDisk()
        }
        println("** Completed Translation archive!")
    }

    private fun readFilesInDirectory(directory: Path): Set<Path> {
        return Files.walk(directory).use { stream ->
            stream
                .filter { path: Path -> Files.isRegularFile(path) }
                .collect(Collectors.toSet())
        }
    }
}
