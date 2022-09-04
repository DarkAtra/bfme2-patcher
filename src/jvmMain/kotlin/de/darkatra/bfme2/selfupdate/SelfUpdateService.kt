package de.darkatra.bfme2.selfupdate

import de.darkatra.bfme2.APPLICATION_NAME
import de.darkatra.bfme2.checksum.HashingService
import de.darkatra.bfme2.download.DownloadService
import de.darkatra.bfme2.patch.Compression
import de.darkatra.bfme2.patch.Context
import de.darkatra.bfme2.patch.PatchConstants
import de.darkatra.bfme2.util.ProcessUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import mslinks.ShellLink
import java.net.URI
import java.nio.file.Path
import kotlin.io.path.absolutePathString
import kotlin.io.path.deleteIfExists
import kotlin.io.path.exists
import kotlin.io.path.inputStream
import kotlin.io.path.moveTo
import kotlin.io.path.outputStream

class SelfUpdateService(
    private val context: Context,
    private val downloadService: DownloadService,
    private val hashingService: HashingService,
    private val applicationHome: ApplicationHome = ApplicationHome()
) {

    companion object {
        const val UPDATER_NAME = "updater.jar"
        const val UPDATER_TEMP_NAME = "_updater.jar"
        const val UPDATER_OLD_NAME = "updater.old.jar"

        const val UNINSTALL_CURRENT_PARAMETER = "--uninstall-current"
        const val INSTALL_PARAMETER = "--install"
    }

    private val patcherUserDir: Path = context.getPatcherUserDir()
    private val updaterTempLocation: Path = patcherUserDir.resolve(UPDATER_TEMP_NAME)
    private val currentUpdaterLocation: Path = patcherUserDir.resolve(UPDATER_NAME)
    private val oldUpdaterLocation: Path = patcherUserDir.resolve(UPDATER_OLD_NAME)
    private val linkLocation: Path = Path.of(System.getProperty("user.home"), "Desktop", "$APPLICATION_NAME.lnk")
    private val linkIconLocation: Path = patcherUserDir.resolve("icon.ico")

    fun isInCorrectLocation(): Boolean {
        return !applicationHome.isRunningAsJar() || applicationHome.source.startsWith(patcherUserDir)
    }

    fun moveToCorrectLocation() {

        if (!applicationHome.isRunningAsJar()) {
            return
        }

        if (!patcherUserDir.exists()) {
            check(patcherUserDir.toFile().mkdirs()) {
                "Could not create ${Context.PATCHER_USER_DIR_IDENTIFIER}."
            }
        }

        applicationHome.source.inputStream().use { input ->
            currentUpdaterLocation.outputStream().use { output ->
                input.copyTo(output)
            }
        }

        if (!linkLocation.toFile().exists()) {
            runBlocking {
                downloadService.download(
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

    fun uninstallPreviousVersion() {
        currentUpdaterLocation.moveTo(oldUpdaterLocation, true)
        ProcessUtils.runJar(oldUpdaterLocation, arrayOf(INSTALL_PARAMETER))
    }

    fun installNewVersion() {
        updaterTempLocation.moveTo(currentUpdaterLocation)
        ProcessUtils.runJar(currentUpdaterLocation)
    }

    fun performCleanup() {
        oldUpdaterLocation.deleteIfExists()
    }

    fun isNewVersionAvailable(): Boolean {

        if (!applicationHome.isRunningAsJar()) {
            return true
        }

        return runBlocking(Dispatchers.IO) {
            val latestUpdaterChecksum: String = hashingService.calculateSha3Checksum(URI.create(PatchConstants.UPDATER_URL).toURL().openStream())
            val currentUpdaterChecksum: String = hashingService.calculateSha3Checksum(applicationHome.source.inputStream())

            currentUpdaterChecksum != latestUpdaterChecksum
        }
    }

    fun downloadLatestUpdaterVersion() {

        updaterTempLocation.deleteIfExists()

        runBlocking {
            downloadService.download(
                URI.create(PatchConstants.UPDATER_URL).toURL(),
                updaterTempLocation,
                Compression.NONE,
                null
            )
        }
    }
}
