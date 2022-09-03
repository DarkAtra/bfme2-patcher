package de.darkatra.bfme2.ui

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material.AlertDialog
import androidx.compose.material.Button
import androidx.compose.material.ExperimentalMaterialApi
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.window.FrameWindowScope
import androidx.compose.ui.window.MenuBar

@Composable
@OptIn(ExperimentalMaterialApi::class)
fun Toolbar(
    frameWindowScope: FrameWindowScope,
) {

    val (isFixDialogVisible, setFixDialogVisible) = remember { mutableStateOf(false) }

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

    if (isFixDialogVisible) {
        AlertDialog(
            onDismissRequest = {},
            title = {
                Text(text = "Fixed BfME 2")
            },
            text = {
                Text(text = "The options.ini file was created successfully.")
            },
            confirmButton = {
                Button(onClick = { setFixDialogVisible(false) }) {
                    Text(text = "Ok")
                }
            },
            modifier = Modifier.fillMaxWidth(0.6f)
        )
    }
}
