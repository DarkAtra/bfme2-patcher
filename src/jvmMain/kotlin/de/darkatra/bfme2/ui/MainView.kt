package de.darkatra.bfme2.ui

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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.FrameWindowScope
import de.darkatra.bfme2.patch.PatchProgress
import de.darkatra.bfme2.patch.PatchProgressListener
import de.darkatra.bfme2.patch.PatchService
import kotlinx.coroutines.launch
import java.time.Duration

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
    patchService: PatchService,
    frameWindowScope: FrameWindowScope
) {

    val patchScope = rememberCoroutineScope()

    val (isUpdateInProgress, setUpdateInProgress) = remember { mutableStateOf(false) }
    val (hasPatchedOnce, setPatchedOnce) = remember { mutableStateOf(false) }
    val (progress, setProgress) = remember { mutableStateOf(0f) }
    val (progressText, setProgressText) = remember { mutableStateOf("Waiting for user input.") }

    Toolbar(frameWindowScope)

    FadingBackground(
        imagePaths = imagePaths,
        transitionDelay = Duration.ofSeconds(10),
        transitionDuration = Duration.ofSeconds(2)
    ) {

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
                    enabled = !isUpdateInProgress,
                    modifier = Modifier.weight(1f).height(32.dp),
                    onClick = {
                        setUpdateInProgress(true)
                        patchScope.launch {
                            patchService.patch(object : PatchProgressListener {

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
                                    setProgressText("${patchProgress.currentDisk}/${patchProgress.totalDisk}")
                                }

                                override fun validatingPacket() {
                                    setProgress(INDETERMINATE_PROGRESS)
                                    setProgressText("Validating the packet...")
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
                    enabled = !isUpdateInProgress && hasPatchedOnce,
                    modifier = Modifier.weight(1f).height(32.dp),
                    onClick = {}
                ) {
                    Text(text = "Start Game", fontSize = 14.sp)
                }
            }
        }
    }
}
