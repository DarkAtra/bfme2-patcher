package de.darkatra.bfme2.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.window.FrameWindowScope
import androidx.compose.ui.window.MenuBar
import com.arkivanov.decompose.extensions.compose.jetbrains.subscribeAsState
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.game.Game
import de.darkatra.bfme2.game.OptionFileService
import kotlin.io.path.exists
import kotlin.io.path.notExists

@Composable
fun Toolbar(
    updaterModel: UpdaterModel,
    frameWindowScope: FrameWindowScope,
    onCheckForUpdates: () -> Unit
) {

    val state by updaterModel.state.subscribeAsState()

    val (gameToFix, setGameToFix) = remember { mutableStateOf<Game?>(null) }
    val (isFixConfirmationDialogVisible, setFixConfirmationDialogVisible) = remember { mutableStateOf(false) }
    val (isFixSuccessDialogVisible, setFixSuccessDialogVisible) = remember { mutableStateOf(false) }

    fun fixGame(gameToFix: Game, force: Boolean = false) {

        setGameToFix(gameToFix)

        val gameUserDirectory = when (gameToFix) {
            Game.BFME2 -> UpdaterContext.context.getBfme2UserDir()
            Game.BFME2EP1 -> UpdaterContext.context.getRotwkUserDir()
        }

        if (gameUserDirectory.notExists()) {
            gameUserDirectory.toFile().mkdirs()
        }

        val optionsIni = gameUserDirectory.resolve("options.ini")
        if (!force && optionsIni.exists()) {
            setFixConfirmationDialogVisible(true)
            return
        }

        OptionFileService.writeOptionsFile(optionsIni, OptionFileService.buildDefaultOptions())
        setFixSuccessDialogVisible(true)
    }

    with(frameWindowScope) {
        MenuBar {

            Menu(text = "Startup Fix") {
                Item(text = "Fix ${Game.BFME2.displayName}", onClick = { fixGame(Game.BFME2) })
                Item(text = "Fix ${Game.BFME2EP1.displayName}", onClick = { fixGame(Game.BFME2EP1) })
            }

            Menu(text = "Game Settings") {
                Item(
                    text = when (state.hdEditionEnabled) {
                        true -> "Disable HD Edition"
                        false -> "Enable HD Edition"
                    },
                    onClick = {
                        updaterModel.setHdEditionEnabled(!state.hdEditionEnabled)
                    }
                )
            }

            Menu(text = "Updater Settings") {
                Item(
                    text = when (state.trayIconEnabled) {
                        true -> "Disable Tray Icon"
                        false -> "Enable Tray Icon"
                    },
                    onClick = {
                        updaterModel.setTrayIconEnabled(!state.trayIconEnabled)
                    }
                )
            }

            Menu(text = "Version") {
                Item(text = "Version ${UpdaterContext.applicationVersion}", onClick = {})
                Item(text = "Check Updates", onClick = onCheckForUpdates)
            }

            Menu(text = "Credits") {
                Item(text = "BFME: Reforged for providing the icon for this application.", onClick = {})
            }

        }
    }

    if (isFixConfirmationDialogVisible && gameToFix != null) {
        ConfirmationDialog(
            title = "${gameToFix.displayName} seems to be working just fine.",
            text = "Trying to fix the game again could result in the loss of settings. Are you sure that you want to continue?",
            onConfirm = {
                setFixConfirmationDialogVisible(false)
                fixGame(gameToFix, true)
            },
            onDismiss = {
                setFixConfirmationDialogVisible(false)
                setGameToFix(null)
            }
        )
    }

    if (isFixSuccessDialogVisible && gameToFix != null) {
        MessageDialog(
            title = "Fixed ${gameToFix.displayName}.",
            text = "The options.ini file was created successfully.",
            onConfirm = {
                setFixSuccessDialogVisible(false)
                setGameToFix(null)
            }
        )
    }
}
