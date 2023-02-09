package de.darkatra.bfme2.patch

import de.darkatra.bfme2.LOGGER
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.checksum.HashingService
import de.darkatra.bfme2.download.DownloadService
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import java.net.URI
import java.net.URL
import java.nio.file.Files
import java.nio.file.Path
import java.time.Instant
import kotlin.io.path.deleteExisting
import kotlin.io.path.exists
import kotlin.io.path.inputStream
import kotlin.io.path.name
import kotlin.io.path.pathString

object PatchService {

    suspend fun patch(progressListener: PatchProgressListener?) = withContext(Dispatchers.IO) {

        progressListener?.onPatchStarted()

        ensureActive()

        val patch = DownloadService.getContent(URI.create(PatchConstants.PATCH_LIST_URL).toURL(), Patch::class)
        patch.applyContext(UpdaterContext.context)

        ensureActive()

        progressListener?.onDeletingObsoleteFiles()

        patch.obsoleteFiles.forEach { obsoleteFile ->

            val toDelete = Path.of(obsoleteFile.dest)

            if (toDelete.exists()) {
                LOGGER.info("Deleting obsolete file: ${obsoleteFile.dest}")
                toDelete.deleteExisting()
            }

            ensureActive()
        }

        progressListener?.onCalculatingDifferences()

        val differences = calculateDifferences(patch)

        var currentNetwork = 0L
        var currentDisk = 0L
        val totalNetwork = differences.compressedSize
        val totalDisk = differences.size

        LOGGER.info("${differences.packets.size} files need updates (total disk: ${totalDisk}, total network: ${totalNetwork}).")

        differences.packets.forEach { packet ->

            LOGGER.info("Downloading '${packet.src}' to '${packet.dest}' (size disk: ${packet.size}, size network: ${packet.compressedSize}, backup existing: ${packet.backupExisting})")

            backupExistingFileIfRequired(packet)

            val dest = Path.of(packet.dest)
            DownloadService.download(URL(packet.src), dest, packet.compression) { downloadProgress ->

                currentNetwork += downloadProgress.countNetwork
                currentDisk += downloadProgress.countDisk

                progressListener?.onPatchProgress(
                    PatchProgress(
                        currentNetwork = currentNetwork,
                        currentDisk = currentDisk,
                        totalNetwork = totalNetwork,
                        totalDisk = totalDisk
                    )
                )
            }

            ensureActive()

            if (packet.checksum != HashingService.calculateSha3Checksum(dest.inputStream())) {
                error("The checksum of local file '${dest.pathString}' does not match the servers checksum.")
            }

            ensureActive()
        }

        progressListener?.onPatchFinished()
    }

    private suspend fun calculateDifferences(patch: Patch): Patch = withContext(Dispatchers.IO) {

        val packets = mutableSetOf<Packet>()

        patch.packets.forEach { packet ->

            val destPath = Path.of(packet.dest)

            if (!destPath.exists() || HashingService.calculateSha3Checksum(destPath.inputStream()) != packet.checksum) {
                packets.add(packet)
            }

            ensureActive()
        }

        return@withContext Patch(
            packets = packets,
            obsoleteFiles = patch.obsoleteFiles.toSet()
        )
    }

    private fun backupExistingFileIfRequired(packet: Packet) {
        val pathToFile: Path = Path.of(packet.dest)
        if (pathToFile.toFile().exists() && packet.backupExisting) {
            Files.move(
                pathToFile,
                Path.of(pathToFile.parent.pathString, String.format("%s-%s.bak", pathToFile.name, Instant.now().toString().replace(":", "-")))
            )
        }
    }
}
