package de.darkatra.bfme2.selfupdate

import de.darkatra.bfme2.LOGGER
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.checksum.HashingService
import de.darkatra.bfme2.download.DownloadService
import de.darkatra.bfme2.patch.Compression
import de.darkatra.bfme2.patch.PatchConstants
import de.darkatra.bfme2.util.ProcessUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import mslinks.ShellLink
import java.net.URI
import java.nio.file.Path
import kotlin.io.path.absolutePathString
import kotlin.io.path.copyTo
import kotlin.io.path.deleteExisting
import kotlin.io.path.deleteIfExists
import kotlin.io.path.exists
import kotlin.io.path.inputStream
import kotlin.io.path.moveTo

object SelfUpdateService {

    const val UNINSTALL_CURRENT_PARAMETER = "--uninstall-current"
    const val INSTALL_PARAMETER = "--install"

    private val linkLocation: Path = Path.of(System.getProperty("user.home"), "Desktop", "BfME Mod Launcher.lnk")
    private val patcherUserDir: Path = UpdaterContext.context.getPatcherUserDir()
    private val updaterTempLocation: Path = patcherUserDir.resolve(PatchConstants.UPDATER_TEMP_NAME)
    private val fallbackUpdaterTempLocation: Path = patcherUserDir.resolve(PatchConstants.FALLBACK_UPDATER_TEMP_NAME)
    private val currentUpdaterLocation: Path = patcherUserDir.resolve(PatchConstants.UPDATER_NAME)
    private val fallbackCurrentUpdaterLocation: Path = patcherUserDir.resolve(PatchConstants.FALLBACK_UPDATER_NAME)
    private val oldUpdaterLocation: Path = patcherUserDir.resolve(PatchConstants.UPDATER_OLD_NAME)
    private val fallbackOldUpdaterLocation: Path = patcherUserDir.resolve(PatchConstants.FALLBACK_UPDATER_OLD_NAME)
    private val linkIconLocation: Path = patcherUserDir.resolve("icon.ico")

    fun isInCorrectLocation(): Boolean {
        return !UpdaterContext.isRunningAsJar() || UpdaterContext.applicationHome.startsWith(patcherUserDir)
    }

    fun moveToCorrectLocation() {

        if (!UpdaterContext.isRunningAsJar()) {
            return
        }

        UpdaterContext.applicationHome.copyTo(currentUpdaterLocation, overwrite = true)

        ProcessUtils.run(currentUpdaterLocation)
        LOGGER.info("Moved updater to correct location, relaunching now.")
    }

    fun updateLinkLocationIfNecessary() {

        if (!UpdaterContext.isRunningAsJar()) {
            return
        }

        if (!linkLocation.toFile().exists()) {
            // create new link
            runBlocking {
                DownloadService.download(
                    URI.create(PatchConstants.UPDATER_ICON_URL).toURL(),
                    linkIconLocation,
                    Compression.NONE
                )
            }

            ShellLink.createLink(currentUpdaterLocation.absolutePathString())
                .setWorkingDir(currentUpdaterLocation.parent.absolutePathString())
                .setIconLocation(linkIconLocation.absolutePathString())
                .saveTo(linkLocation.absolutePathString())

            LOGGER.info("Created Desktop shortcut.")
            return
        }

        val existingLink = ShellLink(linkLocation)
        if (existingLink.resolveTarget() != currentUpdaterLocation.absolutePathString()) {
            existingLink
                .setTarget(currentUpdaterLocation.absolutePathString())
                .setWorkingDir(currentUpdaterLocation.parent.absolutePathString())
                .setIconLocation(linkIconLocation.absolutePathString())
                .saveTo(linkLocation.absolutePathString())
            LOGGER.info("Updated Desktop shortcut.")
        }
    }

    fun cleanupUpdaterIfeoIfNecessary() {

        if (!UpdaterContext.isRunningAsJar()) {
            return
        }

        if (UpdaterContext.ifeoHome.exists()) {
            LOGGER.info("Deleting obsolete updater-ifeo.exe.")
            UpdaterContext.ifeoHome.deleteExisting()
        }
    }

    fun applyUpdate() {
        if (updaterTempLocation.exists()) {
            ProcessUtils.run(updaterTempLocation, arrayOf(UNINSTALL_CURRENT_PARAMETER))
        } else {
            ProcessUtils.runJar(fallbackUpdaterTempLocation, arrayOf(UNINSTALL_CURRENT_PARAMETER))
        }
    }

    fun uninstallPreviousVersion() = runBlocking(Dispatchers.IO) {
        if (currentUpdaterLocation.exists()) {
            attemptRename(currentUpdaterLocation, oldUpdaterLocation, true)
        } else {
            attemptRename(fallbackCurrentUpdaterLocation, fallbackOldUpdaterLocation, true)
        }
        if (oldUpdaterLocation.exists()) {
            ProcessUtils.run(oldUpdaterLocation, arrayOf(INSTALL_PARAMETER))
        } else {
            ProcessUtils.runJar(fallbackOldUpdaterLocation, arrayOf(INSTALL_PARAMETER))
        }
    }

    fun installNewVersion() = runBlocking(Dispatchers.IO) {
        if (updaterTempLocation.exists()) {
            attemptRename(updaterTempLocation, currentUpdaterLocation, true)
            ProcessUtils.run(currentUpdaterLocation)
        } else {
            ProcessUtils.runJar(fallbackCurrentUpdaterLocation)
        }
    }

    fun performCleanup() = runCatching {
        oldUpdaterLocation.deleteIfExists()
        fallbackOldUpdaterLocation.deleteIfExists()
        fallbackUpdaterTempLocation.deleteIfExists()
    }

    suspend fun isNewVersionAvailable(): Boolean = withContext(Dispatchers.IO) {

        if (!UpdaterContext.isRunningAsJar()) {
            return@withContext false
        }

        val latestUpdaterChecksum: String = HashingService.calculateSha3Checksum(URI.create(PatchConstants.UPDATER_URL).toURL().openStream())
        val currentUpdaterChecksum: String = HashingService.calculateSha3Checksum(UpdaterContext.applicationHome.inputStream())

        return@withContext currentUpdaterChecksum != latestUpdaterChecksum
    }

    suspend fun downloadLatestUpdaterVersion() = withContext(Dispatchers.IO) {

        updaterTempLocation.deleteIfExists()

        DownloadService.download(
            URI.create(PatchConstants.UPDATER_URL).toURL(),
            updaterTempLocation,
            Compression.NONE
        )
    }

    private suspend fun attemptRename(
        from: Path,
        to: Path,
        overwrite: Boolean,
        retryAttempts: Int = 7,
        initialDelay: Long = 100,
        maxDelay: Long = 1000,
        delayMultiplier: Double = 2.0
    ) = withContext(Dispatchers.IO) {

        var currentDelay: Long = initialDelay
        repeat(retryAttempts) {

            runCatching {
                from.moveTo(to, overwrite)
            }.onSuccess {
                return@withContext
            }

            currentDelay = (currentDelay * delayMultiplier).toLong().coerceAtMost(maxDelay)
            delay(currentDelay)
        }
    }
}
