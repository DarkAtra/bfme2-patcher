package de.darkatra.bfme2.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.window.FrameWindowScope
import androidx.compose.ui.window.MenuBar
import de.darkatra.bfme2.LOGGER
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.game.Game
import de.darkatra.bfme2.game.OptionFileService
import de.darkatra.bfme2.util.ProcessUtils
import java.util.Base64
import kotlin.io.path.absolutePathString
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
                Item(text = "Fix ${Game.BFME2.displayName}", onClick = { fixGame(Game.BFME2) })
                Item(text = "Fix ${Game.BFME2EP1.displayName}", onClick = { fixGame(Game.BFME2EP1) })
            }

            Menu(text = "Game Settings") {
                CheckboxItem(
                    text = "HD Edition",
                    checked = state.hdEditionEnabled,
                    onCheckedChange = { hdEditionEnabled ->
                        updaterModel.setHdEditionEnabled(hdEditionEnabled)
                    }
                )
                CheckboxItem(
                    text = "Mod",
                    checked = state.modEnabled,
                    onCheckedChange = { modEnabled ->
                        updaterModel.setModEnabled(modEnabled)
                    }
                )
            }

            Menu(text = "Updater Settings") {
                CheckboxItem(
                    text = "Tray Icon",
                    checked = state.trayIconEnabled,
                    onCheckedChange = { trayIconEnabled ->
                        updaterModel.setTrayIconEnabled(trayIconEnabled)
                    }
                )
                if (state.hookingSupported) {
                    CheckboxItem(
                        text = "Hook Game",
                        checked = state.hookEnabled,
                        onCheckedChange = { hookEnabled ->

                            val exitCode = ProcessUtils.runElevated(
                                UpdaterContext.ifeoHome,
                                when (hookEnabled) {
                                    true -> arrayOf(
                                        "filelog",
                                        "set",
                                        Base64.getEncoder().encodeToString(UpdaterContext.applicationHome.absolutePathString().toByteArray())
                                    )

                                    false -> arrayOf("reset")
                                }
                            ).waitFor()

                            if (exitCode == 0) {
                                LOGGER.info("Successfully ${if (hookEnabled) "hooked" else "unhooked"}")
                                updaterModel.setHookEnabled(hookEnabled)
                            } else {
                                // TODO: display error
                                LOGGER.severe("Could not run updater-ifeo.exe. Exit code: $exitCode")
                            }
                        }
                    )
                }
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
