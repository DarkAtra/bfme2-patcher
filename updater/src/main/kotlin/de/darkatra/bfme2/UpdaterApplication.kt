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
import de.darkatra.bfme2.ui.UpdaterModel
import de.darkatra.bfme2.ui.UpdaterView
import de.darkatra.bfme2.updater.generated.resources.Res
import de.darkatra.bfme2.updater.generated.resources.icon
import org.jetbrains.compose.resources.painterResource
import org.jetbrains.skiko.setSystemLookAndFeel
import java.awt.Color.WHITE
import javax.swing.UIManager
import javax.swing.border.LineBorder

object UpdaterApplication {

    fun applyLookAndFeel() {
        setSystemLookAndFeel()
        UIManager.put("MenuBar.border", LineBorder(WHITE, 0))
        UIManager.put("PopupMenu.border", LineBorder(WHITE, 0))
    }

    fun start() = application {

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
