package de.darkatra.bfme2.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.window.FrameWindowScope
import androidx.compose.ui.window.MenuBar

@Composable
fun Toolbar(frameWindowScope: FrameWindowScope) {

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
}
