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
import de.darkatra.bfme2.patch.Context
import de.darkatra.bfme2.selfupdate.SelfUpdateService
import de.darkatra.bfme2.ui.MainView
import de.darkatra.bfme2.ui.UpdaterContext
import de.darkatra.bfme2.ui.UpdaterContextProvider
import org.jetbrains.skiko.setSystemLookAndFeel
import java.nio.file.Paths

private const val ICON_PATH = "/images/icon.png"

fun main(args: Array<String>) = application {

    val context = getTestContext()

    if (!context.isValid()) {
        exitApplication()
        return@application
    }

    setSystemLookAndFeel()

    val (isVisible, setVisible) = remember { mutableStateOf(true) }

    UpdaterContextProvider(
        context = context
    ) {

        val selfUpdateService = UpdaterContext.selfUpdateService

        if (!selfUpdateService.isInCorrectLocation()) {
            selfUpdateService.moveToCorrectLocation()
            exitApplication()
            return@UpdaterContextProvider
        }

        when {
            args.contains(SelfUpdateService.UNINSTALL_CURRENT_PARAMETER) -> {
                selfUpdateService.uninstallPreviousVersion()
                exitApplication()
                return@UpdaterContextProvider
            }

            args.contains(SelfUpdateService.INSTALL_PARAMETER) -> {
                selfUpdateService.installNewVersion()
                exitApplication()
                return@UpdaterContextProvider
            }
        }

        selfUpdateService.performCleanup()

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
                MainView(this)
            }
        }
    }
}

private fun getTestContext(): Context {
    return Context().apply {
        putIfAbsent(Context.SERVER_URL_IDENTIFIER, "https://darkatra.de")
        putIfAbsent(Context.BFME2_HOME_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/Test/bfme2/").normalize().toString())
        putIfAbsent(Context.BFME2_USER_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/Test/userDirBfme2/").normalize().toString())
        putIfAbsent(Context.ROTWK_HOME_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/Test/bfme2ep1/").normalize().toString())
        putIfAbsent(Context.ROTWK_USER_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/Test/userDirBfme2Ep1/").normalize().toString())
        putIfAbsent(Context.PATCHER_USER_DIR_IDENTIFIER, Paths.get(System.getProperty("user.home"), "Desktop/Test/.patcher").normalize().toString())
    }
}
