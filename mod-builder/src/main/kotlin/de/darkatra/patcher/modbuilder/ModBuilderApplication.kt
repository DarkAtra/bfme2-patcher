package de.darkatra.patcher.modbuilder

import de.darkatra.bfme2.big.BigArchive
import de.darkatra.bfme2.big.BigArchiveVersion
import java.nio.file.Files
import java.nio.file.Path
import java.util.stream.Collectors
import kotlin.io.path.pathString

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
