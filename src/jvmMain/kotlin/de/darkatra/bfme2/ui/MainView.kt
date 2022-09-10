package de.darkatra.bfme2.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material.Button
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.ApplicationScope
import androidx.compose.ui.window.FrameWindowScope
import de.darkatra.bfme2.UpdaterContext
import de.darkatra.bfme2.patch.PatchProgress
import de.darkatra.bfme2.patch.PatchProgressListener
import de.darkatra.bfme2.patch.PatchService
import de.darkatra.bfme2.selfupdate.SelfUpdateService
import de.darkatra.bfme2.util.ProcessUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.text.DecimalFormat
import java.time.Duration
import kotlin.math.log10
import kotlin.math.pow

private val imagePaths = arrayOf(
    "/images/splash2_1920x1080.jpg",
    "/images/splash8_2560x1600.jpg",
    "/images/splash13_1920x1080.jpg",
    "/images/splash14_1920x1080.jpg",
    "/images/splash15_1920x1080.jpg",
    "/images/splash19_1500x1200.jpg"
)

@Composable
fun MainView(
    applicationScope: ApplicationScope,
    frameWindowScope: FrameWindowScope
) {

    val patchScope = rememberCoroutineScope()

    val rotwkHomeDir = UpdaterContext.context.getRotwkHomeDir()

    val (isNewVersionAvailable, setNewVersionAvailable) = remember { mutableStateOf(false) }
    val (isSelfUpdateDialogVisible, setSelfUpdateDialogVisible) = remember { mutableStateOf(false) }
    val (isSelfUpdateInProgress, setSelfUpdateInProgress) = remember { mutableStateOf(false) }
    val (isUpdateInProgress, setUpdateInProgress) = remember { mutableStateOf(false) }
    val (hasPatchedOnce, setPatchedOnce) = remember { mutableStateOf(false) }
    val (isGameRunning, setGameRunning) = remember { mutableStateOf(false) }
    val (progress, setProgress) = remember { mutableStateOf(0f) }
    val (progressText, setProgressText) = remember { mutableStateOf("Waiting for user input.") }

    fun performSelfUpdate() {
        setSelfUpdateInProgress(true)
        setProgress(INDETERMINATE_PROGRESS)
        setProgressText("Performing self update...")
        patchScope.launch {
            SelfUpdateService.downloadLatestUpdaterVersion()
            SelfUpdateService.applyUpdate()
            applicationScope.exitApplication()
        }
        setSelfUpdateInProgress(false)
    }

    Toolbar(frameWindowScope) {
        patchScope.launch {
            setNewVersionAvailable(SelfUpdateService.isNewVersionAvailable())
            setSelfUpdateDialogVisible(true)
        }
    }

    FadingBackground(
        imagePaths = imagePaths,
        transitionDelay = Duration.ofSeconds(10),
        transitionDuration = Duration.ofSeconds(2)
    ) {

        if (isNewVersionAvailable) {
            Box(modifier = Modifier.align(Alignment.TopEnd).padding(10.dp)) {

                Button(
                    enabled = !isGameRunning && !isSelfUpdateInProgress && !isUpdateInProgress,
                    modifier = Modifier.height(32.dp),
                    onClick = {
                        performSelfUpdate()
                    }
                ) {
                    Text(text = "Update available", fontSize = 14.sp)
                }

            }
        }

        Column(
            modifier = Modifier.fillMaxWidth()
                .align(Alignment.BottomCenter)
                .padding(start = 10.dp, end = 10.dp, bottom = 10.dp)
        ) {

            ProgressBar(
                progress = progress,
                text = progressText,
            )

            Spacer(modifier = Modifier.height(5f.dp))

            Row(modifier = Modifier.fillMaxWidth()) {

                Button(
                    enabled = !isGameRunning && !isSelfUpdateInProgress && !isUpdateInProgress,
                    modifier = Modifier.weight(1f).height(32.dp),
                    onClick = {
                        setUpdateInProgress(true)
                        patchScope.launch {
                            PatchService.patch(object : PatchProgressListener {

                                override fun onPatchStarted() {
                                    setProgress(INDETERMINATE_PROGRESS)
                                    setProgressText("Downloading patchlist...")
                                }

                                override fun deletingObsoleteFiles() {
                                    setProgress(INDETERMINATE_PROGRESS)
                                    setProgressText("Deleting obsolete files...")
                                }

                                override fun calculatingDifferences() {
                                    setProgress(INDETERMINATE_PROGRESS)
                                    setProgressText("Calculating differences...")
                                }

                                override fun onPatchProgress(patchProgress: PatchProgress) {
                                    setProgress(patchProgress.currentDisk.toFloat() / patchProgress.totalDisk.toFloat())
                                    setProgressText("${humanReadableSize(patchProgress.currentNetwork)}/${humanReadableSize(patchProgress.totalNetwork)}")
                                }

                                override fun onPatchFinished() {
                                    setProgress(1f)
                                    setProgressText("Ready to start the game.")
                                    setUpdateInProgress(false)
                                    setPatchedOnce(true)
                                }
                            })
                        }
                    }
                ) {
                    Text(text = "Check for Updates", fontSize = 14.sp)
                }

                Spacer(modifier = Modifier.width(5f.dp))

                Button(
                    enabled = !isGameRunning && !isSelfUpdateInProgress && !isUpdateInProgress && hasPatchedOnce,
                    modifier = Modifier.weight(1f).height(32.dp),
                    onClick = {
                        setGameRunning(true)
                        patchScope.launch {
                            runCatching {
                                withContext(Dispatchers.IO) {
                                    ProcessUtils.run(
                                        rotwkHomeDir.resolve("lotrbfme2ep1.exe")
                                    ).waitFor()
                                }
                            }.onSuccess {
                                setGameRunning(false)
                            }.onFailure {
                                // TODO: show error message
                                setGameRunning(false)
                            }
                        }
                    }
                ) {
                    Text(text = "Start Game", fontSize = 14.sp)
                }
            }
        }
    }

    if (isSelfUpdateDialogVisible) {
        if (isNewVersionAvailable) {
            ConfirmationDialog(
                title = "Update available",
                text = "Do you want to proceed and update to the latest version?",
                onConfirm = {
                    performSelfUpdate()
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
        setNewVersionAvailable(SelfUpdateService.isNewVersionAvailable())
    }
}

private fun humanReadableSize(size: Long): String {
    if (size <= 0) {
        return "0 B"
    }
    val units = arrayOf("B", "kB", "MB", "GB", "TB", "EB")
    val digitGroups = (log10(size.toDouble()) / log10(1024.0)).toInt()
    return DecimalFormat("#,##0.0").format(size / 1024.0.pow(digitGroups.toDouble())) + " " + units[digitGroups]
}
