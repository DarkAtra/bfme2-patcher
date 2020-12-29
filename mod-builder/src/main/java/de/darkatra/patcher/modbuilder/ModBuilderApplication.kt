package de.darkatra.patcher.modbuilder

import de.darkatra.patcher.modbuilder.model.BigArchive
import java.nio.file.Files
import java.nio.file.Path
import java.util.stream.Collectors

const val OUTPUT_FILE_NAME = "update-builder/rotwk/!mod.big"
const val OUTPUT_FILE_LANG_EN_NAME = "update-builder/rotwk/lang/englishstringsmod.big"
const val OUTPUT_FILE_LANG_DE_NAME = "update-builder/rotwk/lang/germanstringsmod.big"
const val MOD_DIR = "bfme2-ep1-mod"
const val ENGLISH_TRANSLATION_FILE = "data/english-lotr.str"
const val GERMAN_TRANSLATION_FILE = "data/german-lotr.str"
const val LOTR_STR_NAME = "data/lotr.str"

class ModBuilderApplication {
	companion object {
		@JvmStatic
		fun main(args: Array<String>) {
			ModBuilderApplication().build()
		}
	}

	private val excludedDirs = listOf(".git", ENGLISH_TRANSLATION_FILE, GERMAN_TRANSLATION_FILE)

	fun build() {

		val outFile = Path.of(OUTPUT_FILE_NAME).toFile()
		outFile.mkdirs()
		outFile.delete()

		val inputDir = Path.of(MOD_DIR)

		val bigArchive = BigArchive(outFile.outputStream())
		readFilesInDirectory(inputDir)
			.filter { file -> excludedDirs.none { prefix -> file.startsWith("$MOD_DIR/$prefix") } }
			.forEach { file ->
				bigArchive.addFile(file, inputDir.relativize(file).toString())
			}
		bigArchive.writeToDisk()

		buildTranslation(inputDir, ENGLISH_TRANSLATION_FILE, OUTPUT_FILE_LANG_EN_NAME)
		buildTranslation(inputDir, GERMAN_TRANSLATION_FILE, OUTPUT_FILE_LANG_DE_NAME)
	}

	private fun buildTranslation(inputDir: Path, translationFile: String, outputFile: String) {
		val germanTranslationFile = inputDir.resolve(translationFile)
		val outFile = Path.of(outputFile).toFile()
		outFile.mkdirs()
		outFile.delete()

		BigArchive(outFile.outputStream()).also {
			it.addFile(germanTranslationFile, LOTR_STR_NAME)
		}.writeToDisk()
	}

	private fun readFilesInDirectory(directory: Path): Set<Path> {
		return Files.walk(directory).use { stream ->
			stream
				.filter { path: Path -> Files.isRegularFile(path) }
				.collect(Collectors.toSet())
		}
	}
}
