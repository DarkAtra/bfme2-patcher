package de.darkatra.bfme2

import androidx.compose.material.MaterialTheme
import androidx.compose.material.lightColors
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Tray
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.WindowPosition
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import de.darkatra.bfme2.selfupdate.SelfUpdateService
import de.darkatra.bfme2.ui.UpdaterModel
import de.darkatra.bfme2.ui.UpdaterView
import de.darkatra.bfme2.updater.generated.resources.Res
import de.darkatra.bfme2.updater.generated.resources.icon
import org.jetbrains.compose.resources.painterResource
import org.jetbrains.skiko.setSystemLookAndFeel
import java.awt.Color.WHITE
import java.util.logging.FileHandler
import java.util.logging.Level
import java.util.logging.Logger
import java.util.logging.SimpleFormatter
import javax.swing.JOptionPane
import javax.swing.UIManager
import javax.swing.border.LineBorder
import kotlin.io.path.absolutePathString

val LOGGER: Logger = Logger.getLogger("updater")

fun main(args: Array<String>) {

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
        |------------------------------
        """.trimMargin()
    )

    setSystemLookAndFeel()
    UIManager.put("MenuBar.border", LineBorder(WHITE, 0))
    UIManager.put("PopupMenu.border", LineBorder(WHITE, 0))

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

    runCatching {
        UpdaterContext.context.ensureRequiredDirectoriesExist()
    }.onFailure { e ->
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
    SelfUpdateService.downloadUpdaterIfeoIfNecessary()
    SelfUpdateService.performCleanup()

    setSystemLookAndFeel()

    application {

        val updaterModel = remember { UpdaterModel() }
        val state by updaterModel.state.collectAsState()

        val windowState = rememberWindowState(
            position = WindowPosition(alignment = Alignment.Center),
            size = DpSize(800.dp, 600.dp)
        )

        if (state.trayIconEnabled) {
            Tray(
                icon = painterResource(Res.drawable.icon),
                tooltip = UpdaterContext.APPLICATION_NAME,
                onAction = { updaterModel.setVisible(true) },
                menu = {
                    Item("Open", onClick = { updaterModel.setVisible(true) })
                    Item("Exit", onClick = ::exitApplication)
                }
            )
        }

        MaterialTheme(
            colors = lightColors(
                primary = Color.White,
                onPrimary = Color.Black,
                secondary = Color(67, 160, 71),
                onSecondary = Color.Black
            )
        ) {
            Window(
                title = UpdaterContext.APPLICATION_NAME,
                icon = painterResource(Res.drawable.icon),
                resizable = false,
                state = windowState,
                onCloseRequest = {
                    if (state.trayIconEnabled) {
                        updaterModel.setVisible(false)
                    } else {
                        exitApplication()
                    }
                },
                visible = !state.trayIconEnabled || state.isVisible
            ) {
                UpdaterView(
                    updaterModel = updaterModel,
                    applicationScope = this@application,
                    frameWindowScope = this@Window
                )
            }
        }
    }
}
