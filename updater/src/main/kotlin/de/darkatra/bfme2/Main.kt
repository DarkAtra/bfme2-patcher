package de.darkatra.bfme2

import de.darkatra.bfme2.selfupdate.SelfUpdateService
import de.darkatra.bfme2.util.NativeImageUtils
import org.jetbrains.skiko.setSystemLookAndFeel
import java.util.Locale
import java.util.logging.FileHandler
import java.util.logging.Level
import java.util.logging.Logger
import java.util.logging.SimpleFormatter
import javax.swing.JOptionPane
import kotlin.io.path.absolutePathString

val LOGGER: Logger = Logger.getLogger("updater")

fun main(args: Array<String>) {

    Locale.setDefault(Locale.ENGLISH)

    if ("--filelog" in args || SelfUpdateService.isInCorrectLocation()) {
        LOGGER.addHandler(
            FileHandler(
                UpdaterContext.applicationHome.parent.toAbsolutePath().resolve("log-%g.txt").absolutePathString(),
                50000,
                1,
                true
            ).apply {
                formatter = SimpleFormatter()
            }
        )
    }

    LOGGER.info(
        """------------------------------
        |Starting Updater with:
        |  applicationVersion: ${UpdaterContext.applicationVersion}
        |  applicationHome: ${UpdaterContext.applicationHome.absolutePathString()}
        |  args: [${args.joinToString(", ")}]
        |------------------------------
        """.trimMargin()
    )

    try {
        appMain(args)
    } catch (e: Exception) {
        LOGGER.log(Level.SEVERE, "Unexpected error invoking appMain", e)
    }
}

private fun appMain(args: Array<String>) {

    NativeImageUtils.setupNativeImageEnvironmentIfNecessary()

    UpdaterApplication.applyLookAndFeel()

    if (!UpdaterContext.context.isValid()) {
        LOGGER.info("Updater context is invalid.")
        JOptionPane.showMessageDialog(
            null,
            "Updater context is invalid. This usually means that the Games are not installed properly.",
            "Info",
            JOptionPane.INFORMATION_MESSAGE
        )
        return
    }

    try {
        UpdaterContext.context.ensureRequiredDirectoriesExist()
    } catch (e: Exception) {
        LOGGER.log(Level.SEVERE, "Ensure required directories failed with: ${e.message}", e)
        JOptionPane.showMessageDialog(
            null,
            "Could not create required directories. This usually occurs if the games are installed in their default locations as the ${UpdaterContext.APPLICATION_NAME} doesn't have write permissions. Please install the games somewhere else and try again.",
            "Error",
            JOptionPane.ERROR_MESSAGE
        )
        return
    }

    if (!SelfUpdateService.isInCorrectLocation()) {
        LOGGER.info("Updater is in wrong location. Moving to correct location...")
        SelfUpdateService.moveToCorrectLocation()
        return
    }

    when {
        SelfUpdateService.UNINSTALL_CURRENT_PARAMETER in args -> {
            SelfUpdateService.uninstallPreviousVersion()
            return
        }

        SelfUpdateService.INSTALL_PARAMETER in args -> {
            SelfUpdateService.installNewVersion()
            return
        }
    }

    if (!SingleInstance.acquireLock()) {
        return
    }

    SelfUpdateService.updateLinkLocationIfNecessary()
    SelfUpdateService.performCleanup()

    setSystemLookAndFeel()

    UpdaterApplication.start()
}
