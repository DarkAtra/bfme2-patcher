package de.darkatra.bfme2

import androidx.compose.material.MaterialTheme
import androidx.compose.material.lightColors
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Tray
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.WindowPosition
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import com.arkivanov.decompose.extensions.compose.jetbrains.subscribeAsState
import de.darkatra.bfme2.selfupdate.SelfUpdateService
import de.darkatra.bfme2.ui.UpdaterModel
import de.darkatra.bfme2.ui.UpdaterView
import kotlinx.coroutines.runBlocking
import org.jetbrains.skiko.setSystemLookAndFeel
import java.util.logging.Logger
import kotlin.io.path.absolutePathString

private const val ICON_PATH = "/images/icon.png"
private val logger = Logger.getLogger("updater")

fun main(args: Array<String>) {

    if (!UpdaterContext.context.isValid()) {
        logger.info("Updater context is invalid. Existing...")
        return
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
        return
    }

    when {
        SelfUpdateService.UNINSTALL_CURRENT_PARAMETER in args -> {
            runBlocking {
                SelfUpdateService.uninstallPreviousVersion()
            }
            return
        }

        SelfUpdateService.INSTALL_PARAMETER in args -> {
            runBlocking {
                SelfUpdateService.installNewVersion()
            }
            return
        }
    }

    SelfUpdateService.performCleanup()

    setSystemLookAndFeel()

    application {

        val updaterModel = remember { UpdaterModel() }
        val state by updaterModel.state.subscribeAsState()

        val windowState = rememberWindowState(
            position = WindowPosition(alignment = Alignment.Center),
            size = DpSize(800.dp, 600.dp)
        )

        if (state.trayIconEnabled) {
            Tray(
                icon = painterResource(ICON_PATH),
                tooltip = UpdaterContext.applicationName,
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
                title = UpdaterContext.applicationName,
                icon = painterResource(ICON_PATH),
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
