package de.darkatra.bfme2.ui

import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.material.CircularProgressIndicator
import androidx.compose.material.Icon
import androidx.compose.material.MaterialTheme
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.ApplicationScope
import androidx.compose.ui.window.FrameWindowScope
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.patch.Feature
import de.darkatra.bfme2.patch.PatchService
import de.darkatra.bfme2.selfupdate.SelfUpdateService
import de.darkatra.bfme2.ui.UpdaterModel.State.ErrorDetails
import de.darkatra.bfme2.ui.UpdaterModel.State.SelfUpdateState
import de.darkatra.bfme2.updater.generated.resources.Res
import de.darkatra.bfme2.updater.generated.resources.check
import de.darkatra.bfme2.updater.generated.resources.splash14_1920x1080
import de.darkatra.bfme2.updater.generated.resources.splash15_1920x1080
import de.darkatra.bfme2.updater.generated.resources.splash19_1500x1200
import de.darkatra.bfme2.updater.generated.resources.splash24_1536x1024
import de.darkatra.bfme2.updater.generated.resources.splash25_1536x1024
import de.darkatra.bfme2.updater.generated.resources.splash27_1536x1024
import de.darkatra.bfme2.updater.generated.resources.splash2_1920x1080
import de.darkatra.bfme2.updater.generated.resources.splash8_2560x1600
import de.darkatra.bfme2.util.GameUtils
import kotlinx.coroutines.launch
import org.jetbrains.compose.resources.painterResource
import java.time.Duration

private val imagePaths = arrayOf(
    Res.drawable.splash24_1536x1024,
    Res.drawable.splash2_1920x1080,
    Res.drawable.splash27_1536x1024,
    Res.drawable.splash8_2560x1600,
    Res.drawable.splash14_1920x1080,
    Res.drawable.splash15_1920x1080,
    Res.drawable.splash19_1500x1200,
    Res.drawable.splash25_1536x1024,
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
    val state by updaterModel.state.collectAsState()

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
            updaterModel.setSelfUpdateState(SelfUpdateState.UNKNOWN)
            when (SelfUpdateService.isNewVersionAvailable()) {
                true -> updaterModel.setSelfUpdateState(SelfUpdateState.OUTDATED)
                false -> updaterModel.setSelfUpdateState(SelfUpdateState.UP_TO_DATE)
            }
            if (state.selfUpdateState == SelfUpdateState.OUTDATED || state.selfUpdateState == SelfUpdateState.UP_TO_DATE) {
                setSelfUpdateDialogVisible(true)
            }
        }
    })

    FadingBackground(
        images = imagePaths,
        transitionDelay = Duration.ofSeconds(10),
        transitionDuration = Duration.ofSeconds(2)
    ) {

        UpdaterViewLayout(
            actionsSlot = {
                when (state.selfUpdateState) {
                    SelfUpdateState.OUTDATED -> SmallButton(
                        enabled = !state.gameRunning && !state.selfUpdateInProgress && !state.patchInProgress,
                        onClick = {
                            performSelfUpdate()
                        }
                    ) {
                        Text(text = "Update available", fontSize = 15.sp, fontWeight = FontWeight.W400)
                    }

                    SelfUpdateState.UP_TO_DATE -> SmallSurface {
                        Icon(
                            painter = painterResource(Res.drawable.check),
                            contentDescription = null,
                            tint = MaterialTheme.colors.secondary
                        )

                        Spacer(modifier = Modifier.width(6.dp))

                        Text(text = "Up to Date", fontSize = 15.sp, fontWeight = FontWeight.W400)
                    }

                    SelfUpdateState.UNKNOWN -> SmallSurface {
                        CircularProgressIndicator(
                            modifier = Modifier.height(20.dp).width(20.dp),
                            strokeWidth = 3.dp,
                            color = Color.Gray,
                            backgroundColor = MaterialTheme.colors.surface,
                        )

                        Spacer(modifier = Modifier.width(10.dp))

                        Text(text = "Checking for Updates...", fontSize = 15.sp, fontWeight = FontWeight.W400)
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
                    enabled = state.requirementsMet && !state.gameRunning && !state.selfUpdateInProgress && !state.patchInProgress,
                    modifier = Modifier.weight(1f),
                    onClick = {
                        updaterModel.setPatchInProgress(true)
                        patchScope.launch {
                            runCatching {
                                PatchService.patch(updaterModel, UpdaterContext.applicationVersion, buildSet {
                                    if (state.hdEditionEnabled) {
                                        add(Feature.HD_EDITION)
                                    }
                                    if (state.timerEnabled) {
                                        add(Feature.TIMER)
                                    }
                                    if (state.newMusicEnabled) {
                                        add(Feature.NEW_MUSIC)
                                    }
                                    if (state.skipIntroEnabled) {
                                        add(Feature.SKIP_INTRO)
                                    }
                                    if (state.modEnabled) {
                                        add(Feature.MOD)
                                    }
                                })
                            }.onFailure { e ->
                                updaterModel.setPatchInProgress(false)
                                updaterModel.setErrorDetails(
                                    ErrorDetails(
                                        message = "An unexpected error occurred updating the game: ${e.message}",
                                        cause = e
                                    )
                                )
                            }
                        }
                    }
                ) {
                    Text(text = "Check for Updates", fontSize = 15.sp, fontWeight = FontWeight.W400)
                }
            },
            rightButtonSlot = {
                SmallButton(
                    enabled = state.requirementsMet && !state.gameRunning && !state.selfUpdateInProgress && !state.patchInProgress && state.patchedOnce,
                    modifier = Modifier.weight(1f),
                    onClick = {
                        updaterModel.setGameRunning(true)
                        patchScope.launch {
                            runCatching {
                                when (state.hookEnabled) {
                                    true -> GameUtils.launchGameBypassingDebugger(rotwkHomeDir, patcherUserDir, state.hdEditionEnabled)
                                    false -> GameUtils.launchGame(rotwkHomeDir, patcherUserDir, state.hdEditionEnabled)
                                }
                            }.also {
                                updaterModel.setGameRunning(false)
                                updaterModel.setVisible(true)
                            }.onFailure { e ->
                                updaterModel.setErrorDetails(
                                    ErrorDetails(
                                        message = "An unexpected error occurred launching the game: ${e.message}",
                                        cause = e
                                    )
                                )
                            }
                        }
                    }
                ) {
                    Text(text = "Start Game", fontSize = 15.sp, fontWeight = FontWeight.W400)
                }
            }
        )
    }

    if (isSelfUpdateDialogVisible) {
        when (state.selfUpdateState) {
            SelfUpdateState.OUTDATED -> ConfirmationDialog(
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

            SelfUpdateState.UP_TO_DATE -> MessageDialog(
                title = "No Update available.",
                text = "You're already using the latest version of the updater. Good Job!",
                onConfirm = {
                    setSelfUpdateDialogVisible(false)
                }
            )

            else -> Unit
        }
    }

    val errorDetails = state.errorDetails
    if (errorDetails != null) {
        MessageDialog(
            title = errorDetails.title,
            text = errorDetails.message,
            onConfirm = {
                updaterModel.setErrorDetails(null)
            }
        )
    }

    LaunchedEffect(Unit) {
        updaterModel.setSelfUpdateState(SelfUpdateState.UNKNOWN)
        when (SelfUpdateService.isNewVersionAvailable()) {
            true -> updaterModel.setSelfUpdateState(SelfUpdateState.OUTDATED)
            false -> updaterModel.setSelfUpdateState(SelfUpdateState.UP_TO_DATE)
        }
    }
}
