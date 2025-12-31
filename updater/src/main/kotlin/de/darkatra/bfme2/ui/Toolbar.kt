package de.darkatra.bfme2.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.window.FrameWindowScope
import androidx.compose.ui.window.MenuBar
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.game.Game
import de.darkatra.bfme2.game.OptionFileService
import de.darkatra.bfme2.registry.RegistryService
import kotlin.io.path.exists
import kotlin.io.path.notExists

@Composable
fun Toolbar(
    updaterModel: UpdaterModel,
    frameWindowScope: FrameWindowScope,
    onCheckForUpdates: () -> Unit
) {

    val state by updaterModel.state.collectAsState()

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
                Item(
                    text = "Fix ${Game.BFME2.displayName}",
                    enabled = state.errorDetails == null,
                    onClick = { fixGame(Game.BFME2) }
                )
                Item(
                    text = "Fix ${Game.BFME2EP1.displayName}",
                    enabled = state.errorDetails == null,
                    onClick = { fixGame(Game.BFME2EP1) }
                )
            }

            Menu(text = "Game Settings") {
                CheckboxItem(
                    text = "HD Edition",
                    enabled = !state.patchInProgress && state.errorDetails == null,
                    checked = state.hdEditionEnabled,
                    onCheckedChange = { hdEditionEnabled ->
                        updaterModel.setHdEditionEnabled(hdEditionEnabled)
                    }
                )
                CheckboxItem(
                    text = "Timer",
                    enabled = !state.patchInProgress && state.errorDetails == null,
                    checked = state.timerEnabled,
                    onCheckedChange = { timerEnabled ->
                        updaterModel.setTimerEnabled(timerEnabled)
                    }
                )
                CheckboxItem(
                    text = "New Music",
                    enabled = !state.patchInProgress && state.errorDetails == null,
                    checked = state.newMusicEnabled,
                    onCheckedChange = { newMusicEnabled ->
                        updaterModel.setNewMusicEnabled(newMusicEnabled)
                    }
                )
                CheckboxItem(
                    text = "Skip Intro",
                    enabled = !state.patchInProgress && state.errorDetails == null,
                    checked = state.skipIntroEnabled,
                    onCheckedChange = { skipIntroEnabled ->
                        updaterModel.setSkipIntroEnabled(skipIntroEnabled)
                    }
                )
                CheckboxItem(
                    text = "Patch 2.02",
                    enabled = !state.patchInProgress && state.errorDetails == null,
                    checked = state.patch202Enabled,
                    onCheckedChange = { patch202Enabled ->
                        updaterModel.setPatch202Enabled(patch202Enabled)
                    }
                )
                CheckboxItem(
                    text = "Mod",
                    enabled = !state.patchInProgress && state.errorDetails == null,
                    checked = state.modEnabled,
                    onCheckedChange = { modEnabled ->
                        updaterModel.setModEnabled(modEnabled)
                    }
                )
            }

            Menu(text = "Updater Settings") {
                CheckboxItem(
                    text = "Tray Icon",
                    enabled = state.errorDetails == null,
                    checked = state.trayIconEnabled,
                    onCheckedChange = { trayIconEnabled ->
                        updaterModel.setTrayIconEnabled(trayIconEnabled)
                    }
                )
                CheckboxItem(
                    text = "Hook Game",
                    enabled = state.errorDetails == null,
                    checked = state.hookEnabled,
                    onCheckedChange = { hookEnabled ->
                        when {
                            hookEnabled -> RegistryService.setExpansionDebugger(UpdaterContext.applicationHome)
                            else -> RegistryService.resetExpansionDebugger()
                        }
                        updaterModel.setHookEnabled(hookEnabled)
                    }
                )
                CheckboxItem(
                    text = "Debug Mode",
                    enabled = state.errorDetails == null,
                    checked = state.debugModeEnabled,
                    onCheckedChange = { debugModeEnabled ->
                        updaterModel.setDebugModeEnabled(debugModeEnabled)
                    }
                )
            }

            Menu(text = "Version") {
                Item(text = "Version ${UpdaterContext.applicationVersion}", onClick = {})
                Item(
                    text = "Check Updates",
                    enabled = state.errorDetails == null,
                    onClick = onCheckForUpdates
                )
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
