package de.darkatra.bfme2

import androidx.compose.material.MaterialTheme
import androidx.compose.material.lightColors
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import javax.swing.UIManager

fun main() = application {

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
            MainView(this)
        }
    }
}
