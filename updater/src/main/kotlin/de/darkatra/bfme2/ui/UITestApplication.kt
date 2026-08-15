package de.darkatra.bfme2.ui

import androidx.compose.material.MaterialTheme
import androidx.compose.material.lightColors
import androidx.compose.ui.Alignment
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.WindowPosition
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.updater.generated.resources.Res
import de.darkatra.bfme2.updater.generated.resources.icon
import org.jetbrains.compose.resources.painterResource

object UITestApplication {

    fun start() = application {

        val windowState = rememberWindowState(
            position = WindowPosition(alignment = Alignment.Center),
            size = DpSize(1600.dp, 1000.dp)
        )

        Window(
            title = UpdaterContext.APPLICATION_NAME,
            icon = painterResource(Res.drawable.icon),
            state = windowState,
            onCloseRequest = this::exitApplication,
        ) {
            MaterialTheme(
                colors = lightColors(
                    primary = Color.White,
                    onPrimary = Color.Black,
                    secondary = Color(67, 160, 71),
                    onSecondary = Color.Black,
                )
            ) {
                UITestApplicationLayout()
            }
        }
    }
}

fun main() {
    UITestApplication.start()
}
