package de.darkatra.bfme2

import androidx.compose.material.MaterialTheme
import androidx.compose.material.lightColors
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.window.Tray
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import de.darkatra.bfme2.selfupdate.SelfUpdateService
import de.darkatra.bfme2.ui.MainView
import org.jetbrains.skiko.setSystemLookAndFeel
import java.util.logging.Logger
import kotlin.io.path.absolutePathString

private const val ICON_PATH = "/images/icon.png"
private val logger = Logger.getLogger("updater")

fun main(args: Array<String>) = application {

    if (!UpdaterContext.context.isValid()) {
        logger.info("Updater context is invalid. Existing...")
        exitApplication()
        return@application
    }

    logger.info(
        """------------------------------
        |Starting Updater with:
        |  applicationVersion: ${UpdaterContext.applicationVersion}
        |  applicationHome: ${UpdaterContext.applicationHome.absolutePathString()}
        |------------------------------
        """.trimMargin()
    )

    if (!SelfUpdateService.isInCorrectLocation()) {
        logger.info("Updater is in wrong location. Moving to correct location...")
        SelfUpdateService.moveToCorrectLocation()
        exitApplication()
        return@application
    }

    when {
        args.contains(SelfUpdateService.UNINSTALL_CURRENT_PARAMETER) -> {
            SelfUpdateService.uninstallPreviousVersion()
            exitApplication()
            return@application
        }

        args.contains(SelfUpdateService.INSTALL_PARAMETER) -> {
            SelfUpdateService.installNewVersion()
            exitApplication()
            return@application
        }
    }

    SelfUpdateService.performCleanup()

    setSystemLookAndFeel()

    // TODO: make the tray icon an optional setting
    val (isTrayEnabled, setTrayEnabled) = remember { mutableStateOf(false) }
    val (isVisible, setVisible) = remember { mutableStateOf(true) }

    if (isTrayEnabled) {
        Tray(
            icon = painterResource(ICON_PATH),
            tooltip = UpdaterContext.applicationName,
            onAction = { setVisible(true) },
            menu = {
                Item("Open", onClick = { setVisible(true) })
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
            title = UpdaterContext.applicationName,
            icon = painterResource(ICON_PATH),
            resizable = false,
            onCloseRequest = {
                if (isTrayEnabled) {
                    setVisible(false)
                } else {
                    exitApplication()
                }
            },
            visible = !isTrayEnabled || isVisible
        ) {
            MainView(this@application, this@Window)
        }
    }
}
