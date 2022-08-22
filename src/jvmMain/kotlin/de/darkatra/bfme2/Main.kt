package de.darkatra.bfme2

import androidx.compose.material.MaterialTheme
import androidx.compose.material.lightColors
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import de.darkatra.bfme2.patch.Context
import de.darkatra.bfme2.patch.PatchService
import de.darkatra.bfme2.ui.MainView
import java.nio.file.Paths
import javax.swing.UIManager

fun main() = application {

    val patchService = PatchService(getTestContext())

    // set styles for the menu bar
    try {
        UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName())
    } catch (e: Exception) {
        // noop
    }

    MaterialTheme(
        colors = lightColors(
            primary = Color.White,
            onPrimary = Color.DarkGray,
            secondary = Color(67, 160, 71),
            onSecondary = Color.DarkGray
        )
    ) {
        Window(
            title = "BfME Mod Launcher",
            icon = painterResource("/images/icon.png"),
            resizable = false,
            onCloseRequest = ::exitApplication
        ) {
            MainView(patchService, this)
        }
    }
}

private fun getTestContext(): Context {
    return Context().apply {
        putIfAbsent("serverUrl", "https://darkatra.de")
        putIfAbsent("bfme2HomeDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/bfme2/").normalize().toString())
        putIfAbsent("bfme2UserDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/userDirBfme2/").normalize().toString())
        putIfAbsent("rotwkHomeDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/bfme2ep1/").normalize().toString())
        putIfAbsent("patcherUserDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/.patcher").normalize().toString())
        putIfAbsent("rotwkUserDir", Paths.get(System.getProperty("user.home"), "Desktop/Test/userDirBfme2Ep1/").normalize().toString())
    }
}
