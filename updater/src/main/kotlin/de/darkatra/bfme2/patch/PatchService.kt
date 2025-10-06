package de.darkatra.bfme2.patch

import de.darkatra.bfme2.LOGGER
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.checksum.HashingService
import de.darkatra.bfme2.download.DownloadService
import de.darkatra.bfme2.registry.RegistryService
import de.darkatra.bfme2.util.ProcessUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.URI
import java.net.URL
import java.nio.file.Files
import java.nio.file.Path
import java.time.Instant
import java.util.Base64
import kotlin.io.path.deleteExisting
import kotlin.io.path.deleteIfExists
import kotlin.io.path.exists
import kotlin.io.path.inputStream
import kotlin.io.path.moveTo
import kotlin.io.path.name
import kotlin.io.path.pathString

private const val DISABLED_FEATURE_SUFFIX = ".bak"

object PatchService {

    suspend fun patch(progressListener: PatchProgressListener?, updaterVersion: String, features: Set<Feature> = emptySet()) = withContext(Dispatchers.IO) {

        progressListener?.onPatchStarted()

        updateRegistryVersionIfNecessary()

        ensureActive()

        val requirements = DownloadService.getContent(URI.create(PatchConstants.REQUIREMENTS_URL).toURL(), Requirements::class)
        if (!meetsAllRequirements(requirements.minUpdaterVersion, updaterVersion)) {
            progressListener?.onRequirementsNotMet()
            return@withContext
        }

        ensureActive()

        val patch = DownloadService.getContent(URI.create(PatchConstants.PATCH_LIST_URL).toURL(), Patch::class)
        patch.applyContext(UpdaterContext.context)

        ensureActive()

        progressListener?.onRestoringFiles()

        patch.obsoleteFiles.forEach { obsoleteFile ->
            val dest = Path.of(obsoleteFile.dest)
            restoreDisabledFile(dest)
        }

        patch.packets.forEach { packet ->
            val dest = Path.of(packet.dest)
            restoreDisabledFile(dest)
        }

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

                launch(Dispatchers.Main.immediate) {
                    progressListener?.onPatchProgress(
                        PatchProgress(
                            currentNetwork = currentNetwork,
                            currentDisk = currentDisk,
                            totalNetwork = totalNetwork,
                            totalDisk = totalDisk
                        )
                    )
                }
            }

            ensureActive()

            if (packet.checksum != HashingService.calculateSha3Checksum(dest.inputStream())) {
                error("The checksum of local file '${dest.pathString}' does not match the servers checksum.")
            }

            ensureActive()
        }

        progressListener?.onApplyFeatures()

        patch.packets.forEach { packet ->
            if (packet.feature != null && !features.contains(packet.feature)) {
                Path.of(packet.dest).moveTo(Path.of(packet.dest + DISABLED_FEATURE_SUFFIX))
            }
        }

        progressListener?.onPatchFinished()
    }

    private fun meetsAllRequirements(minUpdaterVersion: String?, updaterVersion: String): Boolean {

        if (!UpdaterContext.isRunningAsJar() || minUpdaterVersion == null) {
            return true
        }

        return SemanticVersion.ofString(minUpdaterVersion) <= SemanticVersion.ofString(updaterVersion)
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

    private fun restoreDisabledFile(dest: Path) {
        val disabledDest = Path.of(dest.pathString + DISABLED_FEATURE_SUFFIX)
        if (disabledDest.exists()) {
            dest.deleteIfExists()
            disabledDest.moveTo(dest)
        }
    }

    /**
     * This is required so that the game doesn't ask you to install the latest official patch in order to play online.
     * The patcher already takes care of installing it, and now it also updates the appropriate registry key.
     * Writing to the registry requires elevated permissions so we delegate to `updater-ifeo.exe`.
     */
    private fun updateRegistryVersionIfNecessary() {

        val latestOfficialPatchVersion = 0x20001
        if (RegistryService.getExpansionVersion() == latestOfficialPatchVersion) {
            LOGGER.info("Registry version is already up to date. No changes required.")
            return
        }

        LOGGER.info("Setting registry version to latest official patch version: $latestOfficialPatchVersion")
        val exitCode = ProcessUtils.runElevated(
            UpdaterContext.ifeoHome,
            arrayOf(
                "filelog",
                "setVersion",
                Base64.getEncoder().encodeToString(latestOfficialPatchVersion.toString().toByteArray())
            )
        ).waitFor()

        when (exitCode) {
            0 -> LOGGER.info("Successfully updated registry version for the expansion. New value: $latestOfficialPatchVersion")
            else -> error("Could not update registry version for the expansion. Exit code: $exitCode")
        }
    }
}
