package de.darkatra.patcher.updatebuilder

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule
import com.fasterxml.jackson.module.kotlin.jacksonMapperBuilder
import java.io.File
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardOpenOption
import java.time.Instant
import java.util.stream.Collectors
import java.util.zip.GZIPOutputStream
import kotlin.io.path.exists
import kotlin.io.path.inputStream
import kotlin.io.path.pathString

fun main() {
    UpdateBuilderNoUI.build()
}

object UpdateBuilderNoUI {

    private val objectMapper: ObjectMapper = jacksonMapperBuilder().addModule(JavaTimeModule()).build()
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

        println("Applying obsolete files from: ${obsoleteFilesPath.toFile().path}")
        val obsoleteFiles: Set<ObsoleteFile> = objectMapper.readValue(obsoleteFilesPath.toFile(), Array<ObsoleteFile>::class.java).toSet()

        var added = 0
        var archived = 0
        val packets = mutableSetOf<Packet>()
        for (directory in Directory.values()) {
            val basePath = Path.of("./" + directory.dirName)
            for (filePath in readFilesInDirectory(basePath)) {
                println("* Adding file: ${filePath.pathString}")
                if (addFilesToPatch(packets, lastPatch, directory, filePath)) {
                    archived++
                }
                added++
            }
        }
        println("Files added: $added, Files archived: $archived")

        val patch = Patch(
            obsoleteFiles = obsoleteFiles,
            packets = packets.toSet()
        )

        println("Deleting obsolete files from output folder...")

        var deleted = 0
        readFilesInDirectory(Path.of("./output")).forEach { distributionFile ->
            if (patch.packets.none { packet -> Files.isSameFile(packet.gzipPath!!, distributionFile) }) {
                println("* Deleting file: ${distributionFile.pathString}")
                Files.delete(distributionFile)
                deleted++
            }
        }

        println("Removing empty folders...")

        var deletedEmptyFolders = 0
        readFoldersInDirectory(Path.of("./output"))
            .filter { folder -> folder.toFile().list()?.isEmpty() ?: false }
            .forEach { folder ->
                deletedEmptyFolders += deleteFolder(Path.of("./output"), folder)
            }
        println("Files deleted: $deleted, empty Folders deleted: $deletedEmptyFolders")

        println("Writing version.json...")
        Files.writeString(
            versionJsonPath,
            objectMapper.writerWithDefaultPrettyPrinter().writeValueAsString(patch),
            StandardOpenOption.WRITE,
            StandardOpenOption.TRUNCATE_EXISTING,
            StandardOpenOption.CREATE
        )

        val endTime = System.nanoTime()
        println("Success! Took: ${(endTime - startTime) / 1000000}ms")
    }

    private fun addFilesToPatch(packets: MutableSet<Packet>, lastPatch: Patch?, directory: Directory, filePath: Path): Boolean {

        // creating the archive takes a lot of time, check if it's really necessary by comparing checksums
        // this only compares the checksum of the source file and will not ensure that the archive is identical!
        // manually deleting the version.json will always trigger the creation of a new archive
        val checksum = hashingService.getSHA3Checksum(filePath.toFile())
            .orElseThrow { IllegalStateException("Could not calculate the hash for: ${filePath.toFile()}") }

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
            println("** Archiving file (changed: $fileChanged, exists: $outputExists): ${output.path}")
            createGzipArchive(filePath, output)
        }

        packets.add(
            Packet(
                src = Path.of("\${serverUrl}/bfmemod2/").resolve(
                    Path.of(directory.dirName).relativize(Path.of("$filePath.gz"))
                ).normalize().toString().replace("\\", "/"),
                dest = dest,
                packetSize = filePath.toFile().length(),
                compressedSize = output.length(),
                dateTime = Instant.now(),
                checksum = checksum,
                backupExisting = filesRequireBackup.contains(filePath.toFile().name),
                compression = Compression.ZIP,
                gzipPath = output.toPath()
            )
        )

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
        return when (directory.exists()) {
            true -> Files.walk(directory).use { stream ->
                stream
                    .filter { path: Path -> Files.isRegularFile(path) }
                    .collect(Collectors.toSet())
            }

            else -> emptySet()
        }
    }

    private fun readFoldersInDirectory(directory: Path): Set<Path> {
        return when (directory.exists()) {
            true -> Files.walk(directory).use { stream ->
                stream
                    .filter { path: Path -> Files.isDirectory(path) }
                    .collect(Collectors.toSet())
            }

            else -> emptySet()
        }
    }

    private fun deleteFolder(topLevelDir: Path, currentPath: Path): Int {
        var deleted = 0
        if (topLevelDir != currentPath && currentPath.startsWith(topLevelDir)) {
            val parent = currentPath.parent
            Files.delete(currentPath)
            deleted += 1 + deleteFolder(topLevelDir, parent)
        }
        return deleted
    }
}
