package de.darkatra.bfme2

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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.FrameWindowScope
import androidx.compose.ui.window.MenuBar
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
fun MainView(frameWindowScope: FrameWindowScope) {

    with(frameWindowScope) {
        MenuBar {

            Menu(text = "Startup Fix") {
                Item(text = "Fix BfME 2", onClick = {})
                Item(text = "Fix BfME 2 RotWK", onClick = {})
            }

            Menu(text = "Game Settings") {
                Item(text = "Enable HD Edition", onClick = {})
            }

            Menu(text = "Patcher Settings") {
                Item(text = "Auto patch on startup", onClick = {})
                Item(text = "Auto launch after patching", onClick = {})
            }

            Menu(text = "Version") {
                Item(text = "Unknown", onClick = {})
                Item(text = "Check Updates", onClick = {})
            }

            Menu(text = "Credits") {
                Item(text = "BFME: Reforged for providing the icon for this application.", onClick = {})
            }

        }
    }

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
                progress = 0f,
                text = "Waiting for user input.",
                isIntermediate = true
            )

            Spacer(modifier = Modifier.height(5f.dp))

            Row(modifier = Modifier.fillMaxWidth()) {

                Button(
                    modifier = Modifier.weight(1f).height(32.dp),
                    onClick = {}
                ) {
                    Text(text = "Check for Updates", fontSize = 14.sp)
                }

                Spacer(modifier = Modifier.width(5f.dp))

                Button(
                    modifier = Modifier.weight(1f).height(32.dp),
                    onClick = {}
                ) {
                    Text(text = "Start Game", fontSize = 14.sp)
                }
            }
        }
    }
}
