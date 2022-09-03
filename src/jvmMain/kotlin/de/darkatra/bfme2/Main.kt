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
import de.darkatra.bfme2.checksum.HashingService
import de.darkatra.bfme2.download.DownloadService
import de.darkatra.bfme2.patch.Context
import de.darkatra.bfme2.patch.PatchService
import de.darkatra.bfme2.selfupdate.SelfUpdateService
import de.darkatra.bfme2.ui.MainView
import java.nio.file.Paths
import javax.swing.UIManager

const val APPLICATION_NAME = "BfME Mod Launcher"
private const val ICON_PATH = "/images/icon.png"

fun main(args: Array<String>) = application {

    val context = getTestContext()

    if (!context.isValid()) {
        exitApplication()
        return@application
    }

    val downloadService = DownloadService()
    val hashingService = HashingService()
    val patchService = PatchService(context, downloadService, hashingService)
    val selfUpdateService = SelfUpdateService(context, downloadService, hashingService)

    if (!selfUpdateService.isInCorrectLocation()) {
        selfUpdateService.moveToCorrectLocation()
        exitApplication()
        return@application
    }

    when {
        args.contains(SelfUpdateService.UNINSTALL_CURRENT_PARAMETER) -> {
            selfUpdateService.uninstallPreviousVersion()
            exitApplication()
            return@application
        }

        args.contains(SelfUpdateService.INSTALL_PARAMETER) -> {
            selfUpdateService.installNewVersion()
            exitApplication()
            return@application
        }
    }

    selfUpdateService.performCleanup()

    // set styles for the menu bar
    try {
        UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName())
    } catch (e: Exception) {
        // noop
    }

    val (isVisible, setVisible) = remember { mutableStateOf(true) }

    Tray(
        icon = painterResource(ICON_PATH),
        tooltip = APPLICATION_NAME,
        onAction = { setVisible(true) },
        menu = {
            Item("Open", onClick = { setVisible(true) })
            Item("Exit", onClick = ::exitApplication)
        }
    )

    MaterialTheme(
        colors = lightColors(
            primary = Color.White,
            onPrimary = Color.DarkGray,
            secondary = Color(67, 160, 71),
            onSecondary = Color.DarkGray
        )
    ) {
        Window(
            title = APPLICATION_NAME,
            icon = painterResource(ICON_PATH),
            resizable = false,
            onCloseRequest = { setVisible(false) },
            visible = isVisible
        ) {
            MainView(patchService, selfUpdateService, this)
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
