package de.darkatra.patcher.updatebuilder

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.databind.SerializationFeature
import java.io.File
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardOpenOption
import java.time.Instant
import java.util.stream.Collectors
import java.util.zip.GZIPOutputStream
import kotlin.io.path.inputStream

fun main() {
	UpdateBuilderNoUI.build()
}

object UpdateBuilderNoUI {

	private val objectMapper: ObjectMapper = ObjectMapper()
		.findAndRegisterModules()
		.configure(SerializationFeature.WRITE_DATES_AS_TIMESTAMPS, false)
	private val obsoleteFilesPath = Path.of("./obsolete-files.json")
	private val filesRequireBackup = setOf("asset.dat", "game.dat")
	private val hashingService = HashingService

	fun build() {

		val startTime = System.nanoTime()

		println("Creating new patch...")

		println("Reading version.json...")
		val versionJsonPath = Path.of("./output/version.json")
		val lastPatch: Patch? = when {
			versionJsonPath.toFile().exists() -> objectMapper.readValue(versionJsonPath.toFile(), Patch::class.java)
			else -> null
		}

		println("Applying obsolete files from: " + obsoleteFilesPath.toFile().path)
		val obsoleteFiles: Set<ObsoleteFile> = objectMapper.readValue(obsoleteFilesPath.toFile(), Array<ObsoleteFile>::class.java).toSet()

		var added = 0
		var archived = 0
		val packets = mutableSetOf<Packet>()
		for (directory in Directory.values()) {
			val basePath = Path.of("./" + directory.dirName)
			for (filePath in readFilesInDirectory(basePath)) {
				println("* Adding file: " + filePath.toFile().path)
				if (addFilesToPatch(packets, lastPatch, directory, filePath)) {
					archived++
				}
				added++
			}
		}
		println("Files added: $added, Files archived: $archived")

		println("Writing version.json...")
		Files.writeString(
			versionJsonPath,
			objectMapper.writerWithDefaultPrettyPrinter().writeValueAsString(Patch(
				obsoleteFiles = obsoleteFiles,
				packets = packets.toSet()
			)),
			StandardOpenOption.WRITE,
			StandardOpenOption.TRUNCATE_EXISTING,
			StandardOpenOption.CREATE
		)

		val endTime = System.nanoTime()
		println("Success! Took: " + (endTime - startTime) / 1000000 + "ms")
	}

	private fun addFilesToPatch(packets: MutableSet<Packet>, lastPatch: Patch?, directory: Directory, filePath: Path): Boolean {

		// creating the archive takes a lot of time, check if it's really necessary by comparing checksums
		// this only compares the checksum of the source file and will not ensure that the archive is identical!
		// manually deleting the version.json will always trigger the creation of a new archive
		val checksum = hashingService.getSHA3Checksum(filePath.toFile())
			.orElseThrow { IllegalStateException("Could not calculate the hash for: " + filePath.toFile()) }

		val dest = Path.of(directory.contextVariable).resolve(
			Path.of(directory.dirName).relativize(filePath)
		).normalize().toString().replace("\\", "/")

		var fileChanged = true
		if (lastPatch != null) {
			val lastPatchChecksum: String? = lastPatch.packets.firstOrNull { p -> p.dest == dest }?.checksum
			if (lastPatchChecksum != null && checksum == lastPatchChecksum) {
				fileChanged = false
			}
		}

		val output = File(Path.of("./output/", Path.of(directory.dirName).relativize(filePath).toString()).normalize().toString() + ".gz")
		val outputExists: Boolean = output.exists()
		val archive: Boolean = fileChanged || !outputExists
		if (archive) {
			println("** Archiving file (changed: " + fileChanged + ", exists: " + outputExists + "): " + output.path)
			createGzipArchive(filePath, output)
		}

		packets.add(Packet(
			src = Path.of("\${serverUrl}/bfmemod2/").resolve(
				Path.of(directory.dirName).relativize(Path.of("$filePath.gz"))
			).normalize().toString().replace("\\", "/"),
			dest = dest,
			packetSize = filePath.toFile().length(),
			dateTime = Instant.now(),
			checksum = checksum,
			backupExisting = filesRequireBackup.contains(filePath.toFile().name),
			compression = Compression.ZIP
		))

		return archive
	}

	private fun createGzipArchive(input: Path, output: File) {
		output.parentFile.mkdirs()
		input.inputStream().use { inputStream ->
			GZIPOutputStream(output.outputStream()).use { outputStream ->
				inputStream.transferTo(outputStream)
			}
		}
	}

	private fun readFilesInDirectory(directory: Path): Set<Path> {
		return Files.walk(directory).use { stream ->
			stream
				.filter { path: Path -> Files.isRegularFile(path) }
				.collect(Collectors.toSet())
		}
	}

}
