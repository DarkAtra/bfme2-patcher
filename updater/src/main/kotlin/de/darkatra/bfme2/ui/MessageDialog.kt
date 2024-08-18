package de.darkatra.bfme2.ui

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material.AlertDialog
import androidx.compose.material.Button
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier

@Composable
fun MessageDialog(
    title: String,
    text: String,
    onConfirm: () -> Unit,
) = AlertDialog(
    onDismissRequest = {},
    title = {
        Text(text = title)
    },
    text = {
        Text(text = text)
    },
    confirmButton = {
        Button(onClick = onConfirm) {
            Text(text = "Ok")
        }
    },
    modifier = Modifier.fillMaxWidth(0.6f)
)
