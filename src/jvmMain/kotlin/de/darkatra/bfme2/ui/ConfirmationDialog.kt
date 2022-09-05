package de.darkatra.bfme2.ui

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material.AlertDialog
import androidx.compose.material.Button
import androidx.compose.material.ExperimentalMaterialApi
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.window.Window

@Composable
@OptIn(ExperimentalMaterialApi::class)
fun ConfirmationDialog(
    title: String,
    text: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
    onCloseRequest: () -> Unit = {}
) = Window(
    title = UpdaterContext.applicationName,
    resizable = false,
    onCloseRequest = onCloseRequest
) {
    AlertDialog(
        onDismissRequest = {},
        title = {
            Text(text = title)
        },
        text = {
            Text(text = text)
        },
        confirmButton = {
            Button(onClick = onConfirm) {
                Text(text = "Yes")
            }
        },
        dismissButton = {
            Button(onClick = onDismiss) {
                Text(text = "No")
            }
        },
        modifier = Modifier.fillMaxWidth(0.6f)
    )
}
