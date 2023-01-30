package de.darkatra.bfme2.selfupdate

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
import kotlin.io.path.deleteIfExists
import kotlin.io.path.inputStream
import kotlin.io.path.isRegularFile
import kotlin.io.path.moveTo
import kotlin.io.path.outputStream

object SelfUpdateService {

    const val UPDATER_NAME = "updater.jar"
    const val UPDATER_TEMP_NAME = "_updater.jar"
    const val UPDATER_OLD_NAME = "updater.old.jar"

    const val UNINSTALL_CURRENT_PARAMETER = "--uninstall-current"
    const val INSTALL_PARAMETER = "--install"

    private val linkLocation: Path = Path.of(System.getProperty("user.home"), "Desktop", "BfME Mod Launcher.lnk")
    private val patcherUserDir: Path = UpdaterContext.context.getPatcherUserDir()
    private val updaterTempLocation: Path = patcherUserDir.resolve(UPDATER_TEMP_NAME)
    private val currentUpdaterLocation: Path = patcherUserDir.resolve(UPDATER_NAME)
    private val oldUpdaterLocation: Path = patcherUserDir.resolve(UPDATER_OLD_NAME)
    private val linkIconLocation: Path = patcherUserDir.resolve("icon.ico")

    fun isInCorrectLocation(): Boolean {
        return !isRunningAsJar() || UpdaterContext.applicationHome.startsWith(patcherUserDir)
    }

    fun moveToCorrectLocation() {

        if (!isRunningAsJar()) {
            return
        }

        UpdaterContext.applicationHome.inputStream().use { input ->
            currentUpdaterLocation.outputStream().use { output ->
                input.copyTo(output)
            }
        }

        if (!linkLocation.toFile().exists()) {
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
        }

        ProcessUtils.runJar(currentUpdaterLocation)
    }

    fun applyUpdate() {
        ProcessUtils.runJar(updaterTempLocation, arrayOf(UNINSTALL_CURRENT_PARAMETER))
    }

    fun uninstallPreviousVersion() = runBlocking(Dispatchers.IO) {
        attemptRename(currentUpdaterLocation, oldUpdaterLocation, true)
        ProcessUtils.runJar(oldUpdaterLocation, arrayOf(INSTALL_PARAMETER))
    }

    fun installNewVersion() = runBlocking(Dispatchers.IO) {
        attemptRename(updaterTempLocation, currentUpdaterLocation, true)
        ProcessUtils.runJar(currentUpdaterLocation)
    }

    fun performCleanup() = runCatching {
        oldUpdaterLocation.deleteIfExists()
    }

    suspend fun isNewVersionAvailable(): Boolean = withContext(Dispatchers.IO) {

        if (!isRunningAsJar()) {
            return@withContext true
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
            Compression.NONE,
            null
        )
    }

    private fun isRunningAsJar(): Boolean {
        return UpdaterContext.applicationHome.isRegularFile()
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
