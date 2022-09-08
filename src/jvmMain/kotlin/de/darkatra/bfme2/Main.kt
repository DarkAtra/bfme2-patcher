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
import de.darkatra.bfme2.ui.UpdaterContext
import org.jetbrains.skiko.setSystemLookAndFeel

private const val ICON_PATH = "/images/icon.png"

fun main(args: Array<String>) = application {

    if (!UpdaterContext.context.isValid()) {
        exitApplication()
        return@application
    }

    if (!SelfUpdateService.isInCorrectLocation()) {
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

    val (isVisible, setVisible) = remember { mutableStateOf(true) }

    Tray(
        icon = painterResource(ICON_PATH),
        tooltip = UpdaterContext.applicationName,
        onAction = { setVisible(true) },
        menu = {
            Item("Open", onClick = { setVisible(true) })
            Item("Exit", onClick = ::exitApplication)
        }
    )

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
            onCloseRequest = { setVisible(false) },
            visible = isVisible
        ) {
            MainView(this@application, this@Window)
        }
    }
}
