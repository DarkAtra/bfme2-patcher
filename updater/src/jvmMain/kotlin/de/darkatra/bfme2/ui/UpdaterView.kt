package de.darkatra.bfme2.ui

import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.ApplicationScope
import androidx.compose.ui.window.FrameWindowScope
import com.arkivanov.decompose.extensions.compose.jetbrains.subscribeAsState
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.patch.PatchService
import de.darkatra.bfme2.selfupdate.SelfUpdateService
import de.darkatra.bfme2.util.ProcessUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.time.Duration
import kotlin.io.path.absolutePathString

private val imagePaths = arrayOf(
    "/images/splash2_1920x1080.jpg",
    "/images/splash8_2560x1600.jpg",
    "/images/splash13_1920x1080.jpg",
    "/images/splash14_1920x1080.jpg",
    "/images/splash15_1920x1080.jpg",
    "/images/splash19_1500x1200.jpg"
)

@Composable
fun UpdaterView(
    updaterModel: UpdaterModel,
    applicationScope: ApplicationScope,
    frameWindowScope: FrameWindowScope
) {

    val rotwkHomeDir = UpdaterContext.context.getRotwkHomeDir()
    val patcherUserDir = UpdaterContext.context.getPatcherUserDir()

    val patchScope = rememberCoroutineScope()
    val state by updaterModel.state.subscribeAsState()

    val (isSelfUpdateDialogVisible, setSelfUpdateDialogVisible) = remember { mutableStateOf(false) }

    fun performSelfUpdate() {
        updaterModel.setSelfUpdateInProgress(true)
        updaterModel.setProgress(INDETERMINATE_PROGRESS, "Performing self update...")
        patchScope.launch {
            SelfUpdateService.downloadLatestUpdaterVersion()
            SelfUpdateService.applyUpdate()
            applicationScope.exitApplication()
        }
        updaterModel.setSelfUpdateInProgress(false)
    }

    Toolbar(updaterModel, frameWindowScope, onCheckForUpdates = {
        patchScope.launch {
            updaterModel.setNewVersionAvailable(SelfUpdateService.isNewVersionAvailable())
            if (state.newVersionAvailable) {
                setSelfUpdateDialogVisible(true)
            }
            // TODO: display message if no update is available
        }
    })

    FadingBackground(
        imagePaths = imagePaths,
        transitionDelay = Duration.ofSeconds(10),
        transitionDuration = Duration.ofSeconds(2)
    ) {

        UpdaterViewLayout(
            actionsSlot = {
                if (state.newVersionAvailable) {
                    SmallButton(
                        enabled = !state.gameRunning && !state.selfUpdateInProgress && !state.patchInProgress,
                        onClick = {
                            performSelfUpdate()
                        }
                    ) {
                        Text(text = "Update available", fontSize = 14.sp, fontWeight = FontWeight.W400)
                    }
                }
            },
            progressBarSlot = {
                ProgressBar(
                    progress = state.progress,
                    text = state.progressText,
                )
            },
            leftButtonSlot = {
                SmallButton(
                    enabled = !state.gameRunning && !state.selfUpdateInProgress && !state.patchInProgress,
                    modifier = Modifier.weight(1f),
                    onClick = {
                        updaterModel.setPatchInProgress(true)
                        patchScope.launch {
                            PatchService.patch(updaterModel)
                        }
                    }
                ) {
                    Text(text = "Check for Updates", fontSize = 14.sp, fontWeight = FontWeight.W400)
                }
            },
            rightButtonSlot = {
                SmallButton(
                    enabled = !state.gameRunning && !state.selfUpdateInProgress && !state.patchInProgress && state.patchedOnce,
                    modifier = Modifier.weight(1f),
                    onClick = {
                        updaterModel.setGameRunning(true)
                        patchScope.launch {
                            runCatching {
                                withContext(Dispatchers.IO) {
                                    ProcessUtils.runBypassingDebuggerAndWait(
                                        rotwkHomeDir.resolve("lotrbfme2ep1.exe").normalize(),
                                        when (state.hdEditionEnabled) {
                                            true -> arrayOf(
                                                "-mod",
                                                "\"${patcherUserDir.resolve("HDEdition.big").normalize().absolutePathString()}\""
                                            )

                                            false -> emptyArray()
                                        }
                                    )
                                }
                            }.also {
                                updaterModel.setGameRunning(false)
                                updaterModel.setVisible(true)
                            }.onFailure {
                                // TODO: show error message
                            }
                        }
                    }
                ) {
                    Text(text = "Start Game", fontSize = 14.sp, fontWeight = FontWeight.W400)
                }
            }
        )
    }

    if (isSelfUpdateDialogVisible) {
        if (state.newVersionAvailable) {
            ConfirmationDialog(
                title = "Update available",
                text = "Do you want to proceed and update to the latest version?",
                onConfirm = {
                    if (!state.gameRunning && !state.selfUpdateInProgress && !state.patchInProgress) {
                        performSelfUpdate()
                    }
                },
                onDismiss = {
                    setSelfUpdateDialogVisible(false)
                }
            )
        } else {
            MessageDialog(
                title = "No Update available.",
                text = "You're already using the latest version of the updater. Good Job!",
                onConfirm = {
                    setSelfUpdateDialogVisible(false)
                }
            )
        }
    }

    LaunchedEffect(Unit) {
        updaterModel.setNewVersionAvailable(SelfUpdateService.isNewVersionAvailable())
    }
}
